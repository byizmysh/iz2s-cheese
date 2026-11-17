#define _POSIX_C_SOURCE 200809L
#include "store.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __SWITCH2__
#include <switch.h>
typedef Sha256Context Hash;
static bool hash_open(Hash *hash) { sha256ContextCreate(hash); return true; }
static bool hash_write(Hash *hash, const void *bytes, size_t size) {
    sha256ContextUpdate(hash, bytes, size);
    return true;
}
static bool hash_close(Hash *hash, uint8_t *digest) { sha256ContextGetHash(hash, digest); return true; }
#else
#include <openssl/evp.h>
typedef EVP_MD_CTX *Hash;
static bool hash_open(Hash *hash) {
    *hash = EVP_MD_CTX_new();
    if (*hash && EVP_DigestInit_ex(*hash, EVP_sha256(), NULL) == 1) return true;
    EVP_MD_CTX_free(*hash);
    return false;
}
static bool hash_write(Hash *hash, const void *bytes, size_t size) {
    return EVP_DigestUpdate(*hash, bytes, size) == 1;
}
static bool hash_close(Hash *hash, uint8_t *digest) {
    bool ok = EVP_DigestFinal_ex(*hash, digest, NULL) == 1;
    EVP_MD_CTX_free(*hash);
    return ok;
}
#endif

static const char signature[] = "IZSTORE1";
enum { BLOCK_SIZE = 128 * 1024 };
enum { ICON_BYTES = STORE_ICON_SIZE * STORE_ICON_SIZE * 4, ICON_ID_BYTES = 48 };
static const char artwork_signature[] = "IZART1\n";
typedef enum { ABSENT, REGULAR, OTHER } FileState;

#ifdef IZ_STORE_STANDALONE
#define INSTALL_DIR "/switch"
#else
#define INSTALL_DIR MODULE_DIR
#endif

static bool path(char out[STORE_PATH], const char *pattern, ...) {
    va_list args;
    va_start(args, pattern);
    int size = vsnprintf(out, STORE_PATH, pattern, args);
    va_end(args);
    return size >= 0 && size < STORE_PATH;
}

static bool message(char out[STORE_MESSAGE], const char *text) {
    snprintf(out, STORE_MESSAGE, "%s", text);
    return false;
}

static FileState file_state(const char *name) {
    struct stat st;
    if (lstat(name, &st) == 0) return S_ISREG(st.st_mode) ? REGULAR : OTHER;
    return errno == ENOENT ? ABSENT : OTHER;
}

static bool folder(const char *name) {
    struct stat st;
    if (mkdir(name, 0755) == 0) return true;
    return errno == EEXIST && lstat(name, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool commit(const char *directory) {
#ifdef __SWITCH2__
    (void)directory;
    return R_SUCCEEDED(fsdevCommitDevice("sdmc"));
#else
    int fd = open(directory, O_RDONLY);
    if (fd < 0) return false;
    bool ok = fsync(fd) == 0;
    return close(fd) == 0 && ok;
#endif
}

static bool flush(FILE *file) { return fflush(file) == 0 && fsync(fileno(file)) == 0; }

void store_migrate(const char *root) {
    char from[STORE_PATH], to[STORE_PATH], parent[STORE_PATH];
    struct stat st;
    if (!path(from, "%s/iz2s/store", root) || !path(to, "%s" STORE_DIR, root) ||
        !path(parent, "%s/iz2s", root) || lstat(from, &st) || !S_ISDIR(st.st_mode) ||
        lstat(to, &st) == 0 || errno != ENOENT) return;
    if (rename(from, to) == 0) commit(parent);
}

const char *store_extension(PackageKind kind) { return kind == PACKAGE_IZM ? "iza" : "nro"; }

static const char *kind_field(PackageKind kind) {
    if (kind == PACKAGE_NRO) return "nro";
    return kind == PACKAGE_IZM_NRO ? "izm-nro" : "izm";
}

static bool identifier(const char *id) {
    size_t size = strlen(id);
    if (!size || size >= 48 || *id == '-') return false;
    for (; *id; ++id)
        if (!((*id >= 'a' && *id <= 'z') || (*id >= '0' && *id <= '9') || *id == '-')) return false;
    return true;
}

static bool hexadecimal(const char *value) {
    return strlen(value) == 64 && strspn(value, "0123456789abcdef") == 64;
}

static bool field(char *out, size_t capacity, const char *value) {
    size_t size = strlen(value);
    if (!size || size >= capacity) return false;
    for (const unsigned char *ch = (const unsigned char *)value; *ch; ++ch)
        if (*ch < 32 || *ch == 127) return false;
    memcpy(out, value, size + 1);
    return true;
}

static bool repository_url(const char *url) {
    if (strncmp(url, "https://", 8) || strlen(url) >= RELEASE_URL_MAX) return false;
    const char *host = url + 8, *slash = strchr(host, '/');
    if (!slash || slash == host || !slash[1]) return false;
    for (const unsigned char *p = (const unsigned char *)host; *p; ++p)
        if (*p <= 32 || *p >= 127 || *p == '\\') return false;
    return !memchr(host, '@', (size_t)(slash - host));
}

void store_repository(const Package *package, char url[RELEASE_URL_MAX]) {
    snprintf(url, RELEASE_URL_MAX, "%s", package->repository);
    if (!url[0] && package->release_url[0]) {
        snprintf(url, RELEASE_URL_MAX, "%s", package->release_url);
        char *release = strstr(url, "/releases/");
        if (release) *release = 0;
    }
}

static bool parts(char *line, char *values[], unsigned count) {
    for (unsigned index = 0; index < count; ++index) {
        values[index] = line;
        char *tab = strchr(line, '\t');
        if (index + 1 == count) return !tab;
        if (!tab) return false;
        *tab = '\0';
        line = tab + 1;
    }
    return false;
}

static bool app_line(Package *app, char *line) {
    char *values[7];
    if (!parts(line, values, 7) || !identifier(values[0]) ||
        !strcmp(values[0], "iz2s-store")) return false;
    if (!strcmp(values[1], "nro")) app->kind = PACKAGE_NRO;
    else if (!strcmp(values[1], "izm-nro")) app->kind = PACKAGE_IZM_NRO;
    else if (!strcmp(values[1], "izm")) app->kind = PACKAGE_IZM;
    else return false;
    return field(app->id, sizeof app->id, values[0]) &&
           field(app->version, sizeof app->version, values[2]) &&
           field(app->title, sizeof app->title, values[3]) &&
           field(app->author, sizeof app->author, values[4]) &&
           field(app->category, sizeof app->category, values[5]) &&
           field(app->description, sizeof app->description, values[6]);
}

static bool file_line(Package *app, char *line, bool receipt) {
    char *values[4];
    bool executable = app->count == 0;
    if (app->count >= STORE_MAX_FILES || (!executable && app->kind != PACKAGE_IZM) ||
        !parts(line, values, receipt ? 4 : 3) || !hexadecimal(values[1])) return false;
    char expected[80];
    if (executable) snprintf(expected, sizeof expected, "launcher.%s", store_extension(app->kind));
    else snprintf(expected, sizeof expected, "asset-%s.izdata", values[1]);
    if (strcmp(values[2], expected)) return false;
    for (unsigned i = 0; i < app->count; ++i)
        if (!strcmp(app->files[i].name, expected)) return false;
    size_t digits = strspn(values[0], "0123456789");
    if (!digits || digits > 9 || values[0][digits]) return false;
    uint64_t bytes = strtoull(values[0], NULL, 10);
    if (!bytes || bytes > 128u * 1024u * 1024u) return false;
    if (receipt && strcmp(values[3], "0") && strcmp(values[3], "1")) return false;
    if (receipt && *values[3] == '1') app->owned |= UINT32_C(1) << app->count;
    PackageFile *file = &app->files[app->count++];
    strcpy(file->name, expected);
    strcpy(file->digest, values[1]);
    file->bytes = bytes;
    return true;
}

static bool trim_line(char line[768]) {
    char *end = strchr(line, '\n');
    if (!end) return false;
    if (end != line && end[-1] == '\r') --end;
    *end = '\0';
    return true;
}

static bool load_cached_list(Store *store);

static bool parse(FILE *file, App *apps, unsigned limit, unsigned *count, bool receipt) {
    char line[768];
    *count = 0;
    if (!fgets(line, sizeof line, file) || !trim_line(line)) return false;
    bool online_catalog = !receipt && !strcmp(line, "IZSTORE2");
    if (!online_catalog && strcmp(line, signature)) return false;
    while (fgets(line, sizeof line, file)) {
        if (!trim_line(line)) return false;
        if (!strncmp(line, "app\t", 4)) {
            if (*count == limit || (*count && !apps[*count - 1].package.count)) return false;
            App *app = &apps[*count];
            memset(app, 0, sizeof *app);
            app->icon = (int)*count;
            if (!app_line(&app->package, line + 4)) return false;
            for (unsigned i = 0; i < *count; ++i)
                if (!strcmp(apps[i].package.id, app->package.id)) return false;
            ++*count;
        } else {
            if (!*count) return false;
            Package *package = &apps[*count - 1].package;
            if (online_catalog && !strncmp(line, "repository\t", 11)) {
                if (package->repository[0] || !repository_url(line + 11)) return false;
                strcpy(package->repository, line + 11);
            } else if (online_catalog && !strncmp(line, "github\t", 7)) {
                if (package->kind != PACKAGE_NRO || package->count != 1 ||
                    package->release_url[0] || !github_release_url(line + 7)) return false;
                strcpy(package->release_url, line + 7);
            } else if (strncmp(line, "file\t", 5) || !file_line(package, line + 5, receipt)) return false;
        }
    }
    return feof(file) && !ferror(file) && *count && apps[*count - 1].package.count &&
        (!receipt || (apps[0].package.owned & 1u));
}

static bool load_local_list(Store *store, const char *source, char out[STORE_MESSAGE]) {
    store->count = 0;
    store->remote_count = 0;
    FileState state = file_state(source);
    if (state == ABSENT) return true;
    FILE *file = state == REGULAR ? fopen(source, "rb") : NULL;
    if (!file) return message(out, "The local catalog could not be read.");
    bool ok = parse(file, store->apps, STORE_MAX_APPS, &store->count, false);
    fclose(file);
    if (ok) return true;
    store->count = 0;
    return message(out, "The local catalog is damaged or unsupported.");
}

bool store_load(Store *store, const char *root, char out[STORE_MESSAGE]) {
    store->count = 0;
    store->remote_count = 0;
    if (!path(store->root, "%s", root) || strlen(root) + 256 >= STORE_PATH)
        return message(out, "SD path is too long.");
    char source[STORE_PATH];
    path(source, "%s" STORE_DIR "/catalog.tsv", root);
    if (!load_local_list(store, source, out)) return false;
    load_cached_list(store);
    out[0] = '\0';
    return true;
}

static bool cancelled(Progress progress) {
    return progress.cancel && atomic_load(progress.cancel);
}

static bool transfer(const char *source, FILE *destination, uint64_t bytes, char digest[65],
                     Progress progress, uint64_t done, uint64_t total) {
    if (file_state(source) != REGULAR || bytes > 128u * 1024u * 1024u) return false;
    FILE *input = fopen(source, "rb");
    if (!input) return false;
    struct stat st;
    uint8_t *block = malloc(BLOCK_SIZE);
    Hash hash;
    bool ok = block && fstat(fileno(input), &st) == 0 &&
              st.st_size > 0 && (uint64_t)st.st_size == bytes && hash_open(&hash);
    if (ok) {
        for (uint64_t offset = 0; offset < bytes;) {
            if (cancelled(progress)) { ok = false; break; }
            size_t count = bytes - offset < BLOCK_SIZE ? (size_t)(bytes - offset) : BLOCK_SIZE;
            if (fread(block, 1, count, input) != count || !hash_write(&hash, block, count) ||
                (destination && fwrite(block, 1, count, destination) != count)) {
                ok = false;
                break;
            }
            offset += count;
            if (progress.report) progress.report(progress.context, done + offset, total);
        }
        if (fgetc(input) != EOF || ferror(input)) ok = false;
        uint8_t binary[32];
        if (!hash_close(&hash, binary)) ok = false;
        if (ok) for (unsigned index = 0; index < 32; ++index)
            snprintf(digest + 2 * index, 3, "%02x", binary[index]);
    }
    free(block);
    return fclose(input) == 0 && ok;
}

static bool matches(const char *name, const PackageFile *file) {
    char digest[65];
    return transfer(name, NULL, file->bytes, digest, (Progress){0}, 0, 0) &&
           !strcmp(digest, file->digest);
}

static uint32_t read_word(const uint8_t *value) {
    return (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
        ((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24);
}

static bool valid_image(const char *file, const Package *app) {
    uint8_t head[4096];
    size_t need = app->kind == PACKAGE_IZM ? 512 : sizeof head;
    if (app->files[0].bytes < need || file_state(file) != REGULAR) return false;
    FILE *input = fopen(file, "rb");
    if (!input) return false;
    bool ok = fread(head, 1, need, input) == need;
    fclose(input);
    if (!ok) return false;
    if (app->kind != PACKAGE_IZM) {
        if (memcmp(head + 16, "NRO0", 4)) return false;
#ifdef __SWITCH2__
        NroOunceInfo info;
        return nroGetOunceInfo(head, need, &info) && info.platform == NroPlatform_Switch2;
#else
        uint32_t mod = read_word(head + 4);
        if (mod < 128 || mod > need - 28 || memcmp(head + mod, "MOD0", 4)) return false;
        for (unsigned offset = mod + 28; offset + 12 <= need && offset + 12 <= mod + 128; offset += 4)
            if (!memcmp(head + offset, "OUN0", 4) && read_word(head + offset + 4) == 2 &&
                read_word(head + offset + 8) == 1) return true;
        return false;
#endif
    }
    uint32_t version = read_word(head + 8), flags = read_word(head + 20);
    uint64_t companions = 0;
    for (unsigned i = 1; i < app->count; ++i) companions += app->files[i].bytes;
    return !memcmp(head, "IZSDAPP1", 8) && (version == 1 || version == 2) &&
        read_word(head + 12) == 512 && read_word(head + 16) == app->files[0].bytes &&
        !memcmp(head + 24, app->id, strlen(app->id) + 1) &&
        ((flags & 8u) ? read_word(head + 160) == app->count - 1 && read_word(head + 164) == companions
                      : app->count == 1);
}

static bool read_receipt(const char *directory, const Package *wanted, const char *file, App *receipt) {
    struct stat st;
    char digest[65], name[STORE_PATH];
    if (stat(file, &st) || st.st_size <= 0 ||
        !transfer(file, NULL, (uint64_t)st.st_size, digest, (Progress){0}, 0, 0) ||
        !path(name, "%s/.izstore-%s.tsv", directory, digest) || file_state(name) != REGULAR) return false;
    FILE *stream = fopen(name, "rb");
    if (!stream) return false;
    unsigned count;
    bool ok = parse(stream, receipt, 1, &count, true);
    fclose(stream);
    const Package *old = &receipt->package;
    return ok && count == 1 && !strcmp(old->id, wanted->id) &&
           !strcmp(store_extension(old->kind), store_extension(wanted->kind)) &&
           !strcmp(old->files[0].digest, digest) && old->files[0].bytes == (uint64_t)st.st_size;
}

static void module_paths(const Store *store, const Package *app, char dir[STORE_PATH],
                         char active[STORE_PATH], char backup[STORE_PATH]) {
    path(dir, "%s" INSTALL_DIR "/%s", store->root, app->id);
#ifdef IZ_STORE_STANDALONE
    path(active, "%s/%s.nro", dir, app->id);
    path(backup, "%s/.izstore-backup", dir);
#else
    path(active, "%s/%s", dir, app->files[0].name);
    path(backup, "%s/.izstore-backup.%s", dir, store_extension(app->kind));
#endif
}

static void cleanup_old(const char *directory, const Package *previous, const Package *next);

static bool recover(const char *directory, const char *active, const char *backup,
                    const Package *package) {
    if (file_state(backup) == ABSENT) return true;
    App old;
    if (!read_receipt(directory, package, backup, &old)) return false;
    if (file_state(active) == ABSENT)
        return rename(backup, active) == 0 && commit(directory);
    App current;
    if (!read_receipt(directory, package, active, &current) || unlink(backup) || !commit(directory)) return false;
    cleanup_old(directory, &old.package, &current.package);
    return commit(directory);
}

#ifdef IZ_STORE_STANDALONE
static FileState flat_install(const Store *store, const Package *app) {
    char name[STORE_PATH];
    path(name, "%s" INSTALL_DIR "/%s.nro", store->root, app->id);
    return file_state(name);
}
#else
static bool conflict(const char *directory, const Package *app) {
    char other[STORE_PATH];
    path(other, "%s/launcher.%s", directory, app->kind == PACKAGE_IZM ? "nro" : "iza");
    return file_state(other) != ABSENT;
}
#endif

void store_refresh(const Store *store, App *app) {
    app->installed_version[0] = '\0';
#ifdef IZ_STORE_STANDALONE
    if (app->package.kind == PACKAGE_IZM) { app->state = UNAVAILABLE; return; }
#endif
    char directory[STORE_PATH], active[STORE_PATH], backup[STORE_PATH];
    module_paths(store, &app->package, directory, active, backup);
    if (!recover(directory, active, backup, &app->package)) { app->state = UNAVAILABLE; return; }
    FileState state = file_state(active);
#ifdef IZ_STORE_STANDALONE
    if (state == ABSENT) {
        FileState flat = flat_install(store, &app->package);
        app->state = flat == REGULAR ? EXTERNAL : flat == ABSENT ? NOT_INSTALLED : UNAVAILABLE;
        return;
    }
    if (state != REGULAR) { app->state = UNAVAILABLE; return; }
#else
    if (state == ABSENT) {
        char icon[STORE_PATH];
        if (path(icon, "%s/.izstore-icon.rgba", directory) && file_state(icon) == REGULAR && unlink(icon) == 0)
            commit(directory);
        app->state = conflict(directory, &app->package) ? UNAVAILABLE : NOT_INSTALLED;
        return;
    }
    if (state != REGULAR || conflict(directory, &app->package)) { app->state = UNAVAILABLE; return; }
#endif
    App receipt;
    if (!read_receipt(directory, &app->package, active, &receipt)) { app->state = EXTERNAL; return; }
    strcpy(app->installed_version, receipt.package.version);
    app->state = strcmp(receipt.package.files[0].digest, app->package.files[0].digest)
        ? UPDATE_AVAILABLE : INSTALLED;
    for (unsigned i = 1; i < receipt.package.count; ++i) {
        char resource[STORE_PATH];
        path(resource, "%s/%s", directory, receipt.package.files[i].name);
        if (file_state(resource) != REGULAR) app->state = UPDATE_AVAILABLE;
    }
}

static FILE *open_new(const char *name) {
    int fd = open(name, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) return NULL;
    FILE *file = fdopen(fd, "wb");
    if (!file) { close(fd); unlink(name); }
    return file;
}

static FILE *control_read(const char *name) {
    char backup[STORE_PATH];
    if (file_state(name) == REGULAR) return fopen(name, "rb");
    if (file_state(name) == ABSENT && path(backup, "%s.bak", name) && file_state(backup) == REGULAR)
        return fopen(backup, "rb");
    return NULL;
}

static bool control_publish(const char *stage, const char *final, const char *directory) {
    char backup[STORE_PATH];
    if (!path(backup, "%s.bak", final) || file_state(backup) == OTHER || file_state(final) == OTHER)
        return false;
    bool existing = file_state(final) == REGULAR;
    if (existing && file_state(backup) == REGULAR && unlink(backup)) return false;
    if (existing && rename(final, backup)) return false;
    if (rename(stage, final)) {
        if (existing) rename(backup, final);
        return false;
    }
    if (!commit(directory)) return false;
    if (file_state(backup) == REGULAR) unlink(backup);
    return commit(directory);
}

void store_sync_icon_pixels(const Store *store, const App *app, const void *rgba) {
#ifdef IZ_STORE_STANDALONE
    // hbmenu reads the executable's embedded icon.
    (void)store;
    (void)app;
    (void)rgba;
#else
    if (app->state != INSTALLED || app->package.kind == PACKAGE_IZM) return;
    const unsigned char *pixels = rgba;
    char directory[STORE_PATH], active[STORE_PATH], backup[STORE_PATH], to[STORE_PATH], stage[STORE_PATH];
    module_paths(store, &app->package, directory, active, backup);
    if (!path(to, "%s/.izstore-icon.rgba", directory) ||
        !path(stage, "%s/.izstore-icon.tmp", directory)) return;
    if (file_state(to) == REGULAR) {
        FILE *old = fopen(to, "rb");
        if (old) {
            unsigned char block[1024];
            bool same = true;
            for (size_t at = 0; at < ICON_BYTES && same; at += sizeof block)
                same = fread(block, 1, sizeof block, old) == sizeof block &&
                    !memcmp(block, pixels + at, sizeof block);
            same = same && fgetc(old) == EOF && !ferror(old);
            fclose(old);
            if (same) return;
        }
    }
    if (file_state(stage) == REGULAR && unlink(stage)) return;
    FILE *output = open_new(stage);
    if (!output) return;
    bool ok = fwrite(pixels, 1, ICON_BYTES, output) == ICON_BYTES && flush(output);
    if (fclose(output)) ok = false;
    if (!ok || !control_publish(stage, to, directory)) unlink(stage);
#endif
}

void store_sync_icon(const Store *store, const App *app, const char *artwork) {
    if (app->state != INSTALLED || app->package.kind == PACKAGE_IZM) return;
    char from[STORE_PATH];
    if (!path(from, "%s/icons/%s.rgba", artwork, app->package.id) || file_state(from) != REGULAR) return;
    FILE *input = fopen(from, "rb");
    if (!input) return;
    unsigned char *pixels = malloc(ICON_BYTES);
    bool ok = pixels && fread(pixels, 1, ICON_BYTES, input) == ICON_BYTES &&
        fgetc(input) == EOF && !ferror(input);
    fclose(input);
    if (ok) store_sync_icon_pixels(store, app, pixels);
    free(pixels);
}

void store_settings_read(const char *root, StoreSettings *settings) {
    settings->url[0] = '\0';
    char name[STORE_PATH], line[768];
    if (!path(name, "%s" STORE_DIR "/settings.txt", root)) return;
    FILE *file = control_read(name);
    if (!file) return;
    if (fgets(line, sizeof line, file) && trim_line(line) && (!*line || catalog_url(line)))
        strcpy(settings->url, line);
    fclose(file);
}

static bool settings_folder(const char *root, const char *directory) {
    char parent[STORE_PATH];
    if (!path(parent, "%s/iz2s", root) || !folder(parent)) return false;
    return folder(directory);
}

bool store_settings_write(const char *root, const StoreSettings *settings, char out[STORE_MESSAGE]) {
    if (settings->url[0] && !catalog_url(settings->url))
        return message(out, "Use a raw GitHub HTTPS URL or a private LAN HTTP URL.");
    char directory[STORE_PATH], stage[STORE_PATH], name[STORE_PATH];
    if (!path(directory, "%s" STORE_DIR, root) || !path(stage, "%s/.settings.tmp", directory) ||
        !path(name, "%s/settings.txt", directory) || !settings_folder(root, directory))
        return message(out, "Could not open the store settings folder on SD.");
    if (file_state(stage) == REGULAR && unlink(stage)) return message(out, "Could not clear temporary settings.");
    FILE *file = open_new(stage);
    if (!file) return message(out, "Could not open temporary settings on SD.");
    bool ok = fprintf(file, "%s\n", settings->url) > 0 && flush(file);
    if (fclose(file)) ok = false;
    if (ok && control_publish(stage, name, directory)) return true;
    unlink(stage);
    return message(out, "Could not save settings on SD.");
}

static bool read_remote(FILE *file, const char *url, Store *store) {
    char line[768];
    if (!fgets(line, sizeof line, file) || !trim_line(line) || strcmp(line, url)) return false;
    App *remote = calloc(STORE_MAX_APPS, sizeof *remote);
    if (!remote) return false;
    int slots[STORE_MAX_APPS];
    unsigned count, next = store->count;
    bool ok = parse(file, remote, STORE_MAX_APPS, &count, false);
    for (unsigned i = 0; ok && i < count; ++i) {
        const Package *app = &remote[i].package;
        if (!app->release_url[0]) { ok = false; break; }
        unsigned slot = 0;
        while (slot < store->count && strcmp(store->apps[slot].package.id, app->id)) ++slot;
        if (slot == store->count) slot = next++;
        else if (!store->apps[slot].package.release_url[0]) { ok = false; break; }
        if (slot >= STORE_MAX_APPS) { ok = false; break; }
        slots[i] = (int)slot;
    }
    if (ok) {
        for (unsigned i = 0; i < count; ++i) {
            if (!remote[i].package.repository[0] && (unsigned)slots[i] < store->count)
                strcpy(remote[i].package.repository, store->apps[slots[i]].package.repository);
            store->apps[slots[i]] = remote[i];
            store->apps[slots[i]].icon = slots[i];
        }
        store->count = next;
        store->remote_count = count;
    }
    free(remote);
    return ok;
}

static bool load_cached_list(Store *store) {
    StoreSettings settings;
    store_settings_read(store->root, &settings);
    if (!settings.url[0]) return false;
    char name[STORE_PATH];
    if (!path(name, "%s" STORE_DIR "/remote.tsv", store->root)) return false;
    FILE *file = control_read(name);
    if (!file) return false;
    bool ok = read_remote(file, settings.url, store);
    fclose(file);
    return ok;
}

static bool catalog_sink(void *context, const void *bytes, size_t count) {
    return fwrite(bytes, 1, count, context) == count;
}

static uint32_t read_artwork(FILE *file, const Store *store, const char *url,
                             uint32_t pixels[][STORE_ICON_SIZE * STORE_ICON_SIZE]) {
    char line[768], magic[sizeof artwork_signature - 1];
    if (!fgets(line, sizeof line, file) || !trim_line(line) || strcmp(line, url) ||
        fread(magic, 1, sizeof magic, file) != sizeof magic ||
        memcmp(magic, artwork_signature, sizeof magic)) return 0;
    int count = fgetc(file);
    if (count < 1 || count > STORE_MAX_APPS) return 0;
    struct stat info;
    long start = ftell(file);
    if (start < 0 || fstat(fileno(file), &info) ||
        info.st_size != start + count * (ICON_ID_BYTES + ICON_BYTES)) return 0;
    uint32_t slots = 0;
    for (int item = 0; item < count; ++item) {
        char id[ICON_ID_BYTES];
        if (fread(id, 1, sizeof id, file) != sizeof id ||
            !memchr(id, 0, sizeof id) || !identifier(id)) return 0;
        unsigned slot = 0;
        while (slot < store->count && strcmp(store->apps[slot].package.id, id)) ++slot;
        if (slot == store->count) {
            if (fseek(file, ICON_BYTES, SEEK_CUR)) return 0;
            continue;
        }
        if (slots & (1u << slot)) return 0;
        if (pixels ? fread(pixels[slot], 1, ICON_BYTES, file) != ICON_BYTES :
                     fseek(file, ICON_BYTES, SEEK_CUR) != 0) return 0;
        slots |= 1u << slot;
    }
    return slots;
}

uint32_t store_read_artwork(const Store *store, const StoreSettings *settings,
                            uint32_t pixels[][STORE_ICON_SIZE * STORE_ICON_SIZE]) {
    char name[STORE_PATH];
    if (!store->remote_count || !settings->url[0] ||
        !path(name, "%s" STORE_DIR "/remote-artwork.bin", store->root)) return 0;
    FILE *file = control_read(name);
    if (!file) return 0;
    uint32_t slots = read_artwork(file, store, settings->url, pixels);
    fclose(file);
    return slots;
}

unsigned store_fetch_artwork(const Store *store, const StoreSettings *settings) {
    char url[RELEASE_URL_MAX], directory[STORE_PATH], stage[STORE_PATH], name[STORE_PATH];
    const char *end = strchr(settings->url, '?');
    if (!end) end = settings->url + strlen(settings->url);
    const char *slash = end;
    while (slash > settings->url && slash[-1] != '/') --slash;
    if (!store->remote_count || !catalog_url(settings->url) ||
        snprintf(url, sizeof url, "%.*sartwork.bin", (int)(slash-settings->url), settings->url) >= (int)sizeof url ||
        !path(directory, "%s" STORE_DIR, store->root) ||
        !path(stage, "%s/.artwork.tmp", directory) ||
        !path(name, "%s/remote-artwork.bin", directory)) return 0;
    if (file_state(stage) == REGULAR && unlink(stage)) return 0;
    FILE *file = open_new(stage);
    if (!file) return 0;
    char error[DOWNLOAD_MESSAGE];
    bool ok = fprintf(file, "%s\n", settings->url) > 0 &&
        catalog_download(url, sizeof artwork_signature + STORE_MAX_APPS * (ICON_ID_BYTES + ICON_BYTES),
                         catalog_sink, file, error) && flush(file);
    if (fclose(file)) ok = false;
    uint32_t slots = 0;
    if (ok && (file = fopen(stage, "rb")) != NULL) {
        slots = read_artwork(file, store, settings->url, NULL);
        fclose(file);
    }
    if (!slots || !control_publish(stage, name, directory)) {
        unlink(stage);
        return 0;
    }
    unsigned count = 0;
    for (; slots; slots &= slots - 1) ++count;
    return count;
}

bool store_fetch_list(Store *store, const StoreSettings *settings, char out[STORE_MESSAGE]) {
    char directory[STORE_PATH], stage[STORE_PATH], name[STORE_PATH], root[STORE_PATH];
    if (!settings->url[0]) return message(out, "Enter a catalog URL first.");
    strcpy(root, store->root);
    // Merge each download with the SD index, not a previously merged remote list.
    char base[STORE_PATH];
    if (!path(directory, "%s" STORE_DIR, root) || !path(base, "%s/catalog.tsv", directory) ||
        !path(name, "%s/remote.tsv", directory) || !path(stage, "%s/.remote.tmp", directory))
        return message(out, "The SD path is too long.");
    if (!load_local_list(store, base, out)) return false;
    if (!settings_folder(root, directory)) return message(out, "Could not open the catalog folder on SD.");
    if (file_state(stage) == REGULAR && unlink(stage)) return message(out, "Could not clear the temporary catalog.");
    FILE *file = open_new(stage);
    if (!file) return message(out, "Could not open a temporary catalog on SD.");
    bool ok = fprintf(file, "%s\n", settings->url) > 0;
    if (ok) ok = catalog_download(settings->url, 48u * 1024u, catalog_sink, file, out);
    else message(out, "Could not write the catalog on SD.");
    if (ok && !flush(file)) ok = message(out, "Could not save the catalog on SD.");
    if (fclose(file) && ok) ok = message(out, "Could not close the temporary catalog on SD.");
    if (ok) {
        file = fopen(stage, "rb");
        ok = file && read_remote(file, settings->url, store);
        if (file) fclose(file);
        if (!ok) message(out, "Invalid list, local ID conflict, or more than 24 apps.");
    }
    if (ok && !control_publish(stage, name, directory)) ok = message(out, "Could not publish the catalog on SD.");
    if (!ok) { unlink(stage); return false; }
    unsigned thumbnails = store_fetch_artwork(store, settings);
    if (thumbnails)
        snprintf(out, STORE_MESSAGE, "Catalog updated: %u online entries, %u thumbnail%s.",
                 store->remote_count, thumbnails, thumbnails == 1 ? "" : "s");
    else
        snprintf(out, STORE_MESSAGE, "Catalog updated: %u online entries. No new thumbnails received.", store->remote_count);
    return true;
}

static bool stage_file(const char *source, const char *stage, const PackageFile *file,
                       Progress progress, uint64_t done, uint64_t total) {
    FILE *output = open_new(stage);
    if (!output) return false;
    char digest[65];
    bool ok = transfer(source, output, file->bytes, digest, progress, done, total) &&
              !strcmp(file->digest, digest) && flush(output);
    if (fclose(output) != 0) ok = false;
    if (ok) ok = matches(stage, file);
    if (!ok) unlink(stage);
    return ok;
}

typedef struct {
    FILE *output;
    Progress progress;
    uint64_t done, total;
    bool write_failed;
} DownloadStage;

static bool stage_write(void *context, const void *bytes, size_t count) {
    DownloadStage *transfer = context;
    if (cancelled(transfer->progress)) return false;
    if (fwrite(bytes, 1, count, transfer->output) != count) {
        transfer->write_failed = true;
        return false;
    }
    transfer->done += count;
    if (transfer->progress.report)
        transfer->progress.report(transfer->progress.context, transfer->done, transfer->total);
    return true;
}

static bool stage_github(const Package *package, const char *stage, Progress progress, char out[STORE_MESSAGE]) {
    FILE *output = open_new(stage);
    if (!output) return message(out, "Could not open a temporary file on SD.");
    const PackageFile *file = &package->files[0];
    DownloadStage transfer = {.output = output, .progress = progress, .total = file->bytes};
    bool ok = github_download(package->release_url, file->bytes, stage_write, &transfer, progress.cancel, out);
    if (transfer.write_failed || (ok && !flush(output))) ok = message(out, "Could not write the download to SD.");
    if (fclose(output) && ok) ok = message(out, "Could not finish writing the download to SD.");
    if (ok && !matches(stage, file)) ok = message(out, "The download failed its SHA-256 check.");
    if (cancelled(progress)) ok = message(out, "Download cancelled.");
    if (!ok) unlink(stage);
    return ok;
}

static bool write_receipt(const char *directory, const Package *package, const char *executable) {
    char final[STORE_PATH], stage[STORE_PATH];
    path(final, "%s/.izstore-%s.tsv", directory, package->files[0].digest);
    if (file_state(final) != ABSENT) {
        App recorded;
        if (!read_receipt(directory, package, executable, &recorded) || recorded.package.count != package->count)
            return false;
        for (unsigned i = 0; i < package->count; ++i) {
            const PackageFile *old = &recorded.package.files[i], *file = &package->files[i];
            if (strcmp(old->name, file->name) || strcmp(old->digest, file->digest) || old->bytes != file->bytes) return false;
        }
        return true;
    }
    path(stage, "%s/.izstore-receipt.tmp", directory);
    if (file_state(stage) == REGULAR && unlink(stage)) return false;
    FILE *file = open_new(stage);
    if (!file) return false;
    bool ok = fprintf(file, "%s\napp\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", signature, package->id,
        kind_field(package->kind), package->version, package->title,
        package->author, package->category, package->description) > 0;
    for (unsigned i = 0; ok && i < package->count; ++i) {
        const PackageFile *entry = &package->files[i];
        ok = fprintf(file, "file\t%llu\t%s\t%s\t%u\n", (unsigned long long)entry->bytes,
            entry->digest, entry->name, (package->owned >> i) & 1u) > 0;
    }
    ok = ok && flush(file);
    if (fclose(file) != 0) ok = false;
    if (ok) ok = rename(stage, final) == 0 && commit(directory);
    if (!ok) unlink(stage);
    return ok;
}

static bool owns(const Package *package, const char *name) {
    for (unsigned i = 1; i < package->count; ++i)
        if ((package->owned & (UINT32_C(1) << i)) && !strcmp(package->files[i].name, name)) return true;
    return false;
}

static bool uses(const Package *package, const char *name) {
    if (package)
        for (unsigned i = 1; i < package->count; ++i)
            if (!strcmp(package->files[i].name, name)) return true;
    return false;
}

static void cleanup_old(const char *directory, const Package *previous, const Package *next) {
    char location[STORE_PATH];
    for (unsigned i = 1; i < previous->count; ++i) {
        if (!(previous->owned & (UINT32_C(1) << i)) || uses(next, previous->files[i].name)) continue;
        path(location, "%s/%s", directory, previous->files[i].name);
        if (matches(location, &previous->files[i])) unlink(location);
    }
    if (!next || strcmp(previous->files[0].digest, next->files[0].digest)) {
        path(location, "%s/.izstore-%s.tsv", directory, previous->files[0].digest);
        if (file_state(location) == REGULAR) unlink(location);
    }
}

bool store_install(const Store *store, const App *app, Progress progress, char out[STORE_MESSAGE]) {
    Package package = app->package;
#ifdef IZ_STORE_STANDALONE
    if (package.kind == PACKAGE_IZM) return message(out, "IZA apps require IZlauncher.");
#endif
    char directory[STORE_PATH], active[STORE_PATH], backup[STORE_PATH], stage[STORE_PATH], base[STORE_PATH];
    module_paths(store, &package, directory, active, backup);
    path(base, "%s" INSTALL_DIR, store->root);
#ifdef IZ_STORE_STANDALONE
    if (file_state(active) == ABSENT && file_state(backup) == ABSENT && flat_install(store, &package) != ABSENT)
        return message(out, "A separate copy already exists in /switch.");
#endif
    if (!folder(base) || !folder(directory)) return message(out, "Could not create the application folder.");
#ifndef IZ_STORE_STANDALONE
    if (conflict(directory, &package)) return message(out, "The application folder has conflicting files.");
#endif
    if (!recover(directory, active, backup, &package))
        return message(out, "The application folder has conflicting files.");
    bool exists = file_state(active) != ABSENT;
    App old = {0};
    if (exists && !read_receipt(directory, &package, active, &old))
        return message(out, "This copy is managed outside this store.");

    uint64_t total = 0;
    for (unsigned i = 0; i < package.count; ++i) total += package.files[i].bytes;
    path(stage, "%s/.izstore-launcher.tmp", directory);
    if (file_state(stage) == REGULAR && unlink(stage))
        return message(out, "Could not clear an interrupted transfer.");
    char payload[STORE_PATH], source[STORE_PATH], target[STORE_PATH], copy[STORE_PATH];
    path(payload, "%s" STORE_DIR "/payloads/%s/%s", store->root, package.id, package.files[0].digest);
    path(source, "%s/%s", payload, package.files[0].name);
    if (package.release_url[0]) {
        if (!stage_github(&package, stage, progress, out)) return false;
    } else if (!stage_file(source, stage, &package.files[0], progress, 0, total))
        return message(out, "The local package is missing, damaged, or SD is full.");
    if (cancelled(progress)) { unlink(stage); return message(out, "Download cancelled."); }
    if (!valid_image(stage, &package)) {
        unlink(stage);
        return message(out, "The executable is incompatible with this catalog entry.");
    }
    package.owned = 1;
    path(copy, "%s/.izstore-resource.tmp", directory);
    if (file_state(copy) == REGULAR && unlink(copy)) {
        unlink(stage);
        return message(out, "Could not clear an interrupted resource transfer.");
    }
    uint64_t done = package.files[0].bytes;
    uint32_t added = 0;
    const char *failure = NULL;
    for (unsigned i = 1; i < package.count; ++i) {
        const PackageFile *file = &package.files[i];
        path(target, "%s/%s", directory, file->name);
        if (file_state(target) != ABSENT) {
            if (!matches(target, file)) { failure = "An installed resource failed its checksum."; break; }
            if (owns(&old.package, file->name)) package.owned |= UINT32_C(1) << i;
        } else {
            path(source, "%s/%s", payload, file->name);
            if (!stage_file(source, copy, file, progress, done, total)) {
                failure = "A local resource is missing, damaged, or SD is full.";
                break;
            }
            if (rename(copy, target)) { failure = "Could not publish the verified resource."; break; }
            added |= UINT32_C(1) << i;
            package.owned |= UINT32_C(1) << i;
            if (!commit(directory)) { failure = "Could not confirm the resource on SD."; break; }
        }
        done += file->bytes;
    }
    if (cancelled(progress)) failure = "Download cancelled.";
    if (!failure && !write_receipt(directory, &package, stage)) failure = "Could not record the verified package.";
    if (failure) {
        for (unsigned i = 1; i < package.count; ++i) {
            if (!(added & (UINT32_C(1) << i))) continue;
            path(target, "%s/%s", directory, package.files[i].name);
            if (matches(target, &package.files[i])) unlink(target);
        }
        if (file_state(copy) == REGULAR) unlink(copy);
        unlink(stage);
        commit(directory);
        return message(out, failure);
    }
    if (exists && (rename(active, backup) || !commit(directory)))
        return message(out, "Could not keep the previous version.");
    if (rename(stage, active)) {
        if (exists) recover(directory, active, backup, &package);
        return message(out, "Could not activate the package.");
    }
    if (!commit(directory)) return message(out, "The SD card did not confirm the installation.");
    if (exists && unlink(backup) == 0) cleanup_old(directory, &old.package, &package);
    if (!commit(directory)) return message(out, "The SD card did not confirm cleanup.");
    snprintf(out, STORE_MESSAGE, "%s %s", exists ? "Updated to" : "Installed", package.version);
    return true;
}

bool store_remove(const Store *store, const App *app, char out[STORE_MESSAGE]) {
#ifdef IZ_STORE_STANDALONE
    if (app->package.kind == PACKAGE_IZM) return message(out, "IZA apps require IZlauncher.");
#endif
    char directory[STORE_PATH], active[STORE_PATH], backup[STORE_PATH];
    module_paths(store, &app->package, directory, active, backup);
    App receipt;
    if (!recover(directory, active, backup, &app->package) ||
        !read_receipt(directory, &app->package, active, &receipt))
        return message(out, "This copy is managed outside this store.");
    if (unlink(active) || !commit(directory)) return message(out, "Could not remove the application.");
    cleanup_old(directory, &receipt.package, NULL);
    if (!commit(directory)) return message(out, "The SD card did not confirm cleanup.");
    snprintf(out, STORE_MESSAGE, "Removed. Saved data is still on SD.");
    return true;
}
