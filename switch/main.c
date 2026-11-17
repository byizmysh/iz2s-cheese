#include "ui.h"
#include <switch.h>
#include <arpa/inet.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#ifdef IZ_STORE_HOSTED
#include <sb4/native-app.h>
#endif

#define SD_ROOT "sdmc:"
#define CHECK_REQUEST SD_ROOT STORE_DIR "/network-check.request"
#define CHECK_REPORT SD_ROOT STORE_DIR "/network-check.txt"

typedef struct {
    const Store *current;
    Store *result;
    const App *app;
    Command command;
    atomic_bool done, cancel;
    atomic_uint percent;
    bool ok;
    char message[STORE_MESSAGE];
    char network[STORE_MESSAGE], addresses[STORE_MESSAGE], github[STORE_MESSAGE];
    StoreSettings settings;
} Task;

static Store catalogs[2];
static Artwork artwork;

static void network_status(Task *task) {
    task->addresses[0] = '\0';
    Result rc = nifmInitialize(NifmServiceType_User);
    if (R_FAILED(rc)) {
        snprintf(task->network, sizeof task->network, "Switch network status unavailable (0x%08x).", rc);
        return;
    }
    NifmInternetConnectionType type;
    NifmInternetConnectionStatus state;
    u32 strength, ip, mask, gateway, dns, secondary_dns;
    rc = nifmGetInternetConnectionStatus(&type, &strength, &state);
    if (R_FAILED(rc)) snprintf(task->network, sizeof task->network, "No active Switch connection (0x%08x).", rc);
    else if (type == NifmInternetConnectionType_WiFi)
        snprintf(task->network, sizeof task->network, "Wi-Fi, signal %u/3 | Switch internet %s", strength,
            state == NifmInternetConnectionStatus_Connected ? "connected" : "pending");
    else
        snprintf(task->network, sizeof task->network, "%s | Switch internet %s",
            type == NifmInternetConnectionType_Ethernet ? "Ethernet" : "System network",
            state == NifmInternetConnectionStatus_Connected ? "connected" : "pending");
    if (R_SUCCEEDED(nifmGetCurrentIpConfigInfo(&ip, &mask, &gateway, &dns, &secondary_dns))) {
        char ip_text[16], gateway_text[16], dns_text[16];
        if (inet_ntop(AF_INET, &ip, ip_text, sizeof ip_text) &&
            inet_ntop(AF_INET, &gateway, gateway_text, sizeof gateway_text) &&
            inet_ntop(AF_INET, &dns, dns_text, sizeof dns_text))
            snprintf(task->addresses, sizeof task->addresses, "IP %s   Gateway %s   DNS %s",
                ip_text, gateway_text, dns_text);
    }
    nifmExit();
}

static bool check_github(Task *task) {
    const char *url = NULL;
    for (unsigned i = 0; i < task->current->count; ++i) {
        const char *release = task->current->apps[i].package.release_url;
        if (*release) { url = release; break; }
    }
    bool ok = github_probe(url, task->github);
    if (ok) snprintf(task->github, sizeof task->github, "%s", url ?
        "GitHub and its release CDN responded over verified HTTPS." : "GitHub responded over verified HTTPS.");
    return ok;
}

static void write_report(const Task *task, const char *catalog) {
    FILE *file = fopen(CHECK_REPORT, "w");
    if (!file) return;
    bool ok = fprintf(file, "IZ2S Store network check\nNetwork: %s\n%s\nGitHub: %s\nCatalog URL: %s\nCatalog: %s\n",
        task->network, task->addresses, task->github, task->settings.url, catalog) > 0;
    if (fclose(file)) ok = false;
    if (ok && R_SUCCEEDED(fsdevCommitDevice("sdmc")) && task->command == STARTUP_CHECK) {
        unlink(CHECK_REQUEST);
        fsdevCommitDevice("sdmc");
    }
}

static void refresh_apps(Store *store) {
    for (unsigned i = 0; i < store->count; ++i) store_refresh(store, &store->apps[i]);
}

static void progress(void *context, uint64_t done, uint64_t total) {
    Task *task = context;
    if (total) atomic_store(&task->percent, (unsigned)(done * 99 / total));
}

static void run_task(void *context) {
    Task *task = context;
    if (task->command == REFRESH) {
        task->ok = store_refresh_catalog(task->result, SD_ROOT, &task->settings, task->message);
        if (task->ok) refresh_apps(task->result);
        network_status(task);
    } else if (task->command == FETCH_LIST || task->command == STARTUP_CHECK || task->command == CHECK_NETWORK) {
        network_status(task);
        if (task->command == CHECK_NETWORK || task->command == STARTUP_CHECK) {
            bool online = check_github(task);
            if (task->command == CHECK_NETWORK) {
                task->ok = online;
                snprintf(task->message, sizeof task->message, "%s", task->github);
                write_report(task, "Catalog was not requested.");
            }
        }
        if (task->command == FETCH_LIST || task->command == STARTUP_CHECK) {
            *task->result = *task->current;
            snprintf(task->result->root, sizeof task->result->root, "%s", SD_ROOT);
            task->ok = store_fetch_list(task->result, &task->settings, task->message);
            if (task->ok) refresh_apps(task->result);
            if (task->command == STARTUP_CHECK) write_report(task, task->message);
        }
    } else {
        if (task->command == INSTALL)
            task->ok = store_install(task->current, task->app, (Progress){.report = progress, .context = task, .cancel = &task->cancel}, task->message);
        else task->ok = store_remove(task->current, task->app, task->message);
        *task->result = *task->current;
        unsigned index = (unsigned)(task->app - task->current->apps);
        store_refresh(task->result, &task->result->apps[index]);
    }
    atomic_store(&task->done, true);
}

static bool start_task(Task *task, Thread *thread, View *view, const Store *store, Store *next, Command command) {
    if (view->busy) return false;
    view->minimized = false;
    task->current = store;
    task->result = next;
    task->command = command;
    task->settings = view->settings;
    task->app = view->count ? &store->apps[view->visible[view->cursor]] : NULL;
    atomic_store(&task->done, false);
    atomic_store(&task->cancel, false);
    view->cancellable = command == INSTALL && task->app && task->app->package.release_url[0];
    snprintf(view->source, sizeof view->source, "%s", view->cancellable ? task->app->package.release_url : "");
    snprintf(view->download_title, sizeof view->download_title, "%s", view->cancellable ? task->app->package.title : "");
    atomic_store(&task->percent, 0);
    Result result = threadCreate(thread, run_task, task, NULL, 0x10000, 0x32, -2);
    if (R_SUCCEEDED(result)) {
        result = threadStart(thread);
        if (R_FAILED(result)) threadClose(thread);
    }
    view->busy = R_SUCCEEDED(result);
    view->failure = R_FAILED(result);
    view->percent = 0;
    if (view->failure) snprintf(view->message, sizeof view->message, "Could not start SD operation (0x%08x).", result);
    else snprintf(view->message, sizeof view->message, "%s",
        command == REFRESH ? "Loading catalog..." :
        command == REMOVE ? "Removing the application..." :
        command == FETCH_LIST ? "Fetching catalog and thumbnails..." :
        command == CHECK_NETWORK || command == STARTUP_CHECK ? "Checking the network..." :
        task->app && task->app->package.release_url[0] ? "Downloading" :
        "Copying and verifying files...");
    return view->busy;
}

static unsigned buttons(u64 keys) {
    const u64 platform[] = {
        HidNpadButton_AnyUp, HidNpadButton_AnyDown, HidNpadButton_AnyLeft, HidNpadButton_AnyRight,
        HidNpadButton_A, HidNpadButton_B, HidNpadButton_X, HidNpadButton_Y,
        HidNpadButton_L, HidNpadButton_R, HidNpadButton_Plus, HidNpadButton_Minus, HidNpadButton_ZL,
    };
    unsigned result = 0;
    for (unsigned i = 0; i < sizeof platform / sizeof platform[0]; ++i)
        if (keys & platform[i]) result |= 1u << i;
    return result;
}

static bool edit_catalog_url(View *view) {
    SwkbdConfig keyboard;
    Result rc = swkbdCreate(&keyboard, 0);
    if (R_FAILED(rc)) {
        view->failure = true;
        snprintf(view->message, sizeof view->message, "Could not open the keyboard (0x%08x).", rc);
        return false;
    }
    StoreSettings settings = {0};
    swkbdConfigMakePresetDefault(&keyboard);
    swkbdConfigSetType(&keyboard, SwkbdType_QWERTY);
    swkbdConfigSetGuideText(&keyboard, "Catalog URL: raw GitHub or local HTTP");
    swkbdConfigSetStringLenMax(&keyboard, 500);
    swkbdConfigSetInitialText(&keyboard, view->settings.url);
    rc = swkbdShow(&keyboard, settings.url, sizeof settings.url);
    swkbdClose(&keyboard);
    if (R_FAILED(rc) || !strcmp(settings.url, view->settings.url)) return false;
    view->failure = !store_settings_write(SD_ROOT, &settings, view->message);
    if (view->failure) return false;
    view->settings = settings;
    return true;
}

typedef struct {
    View view;
    Task task;
    Thread thread;
    unsigned active;
    bool touched, check_on_start, dirty;
    const unsigned char *art_records;
    unsigned art_count;
    uint64_t held;
} Session;

static void sync_artwork(Session *session, Store *store) {
    uint32_t online = store_read_artwork(store, &session->view.settings, artwork.icons);
    for (unsigned i = 0; i < STORE_MAX_APPS; ++i)
        artwork.loaded[i] = (online & (1u << i)) != 0;
    if (session->art_records) {
        const size_t record_size = 48 + sizeof artwork.icons[0];
        for (unsigned i = 0; i < store->count; ++i) {
            if (artwork.loaded[i]) continue;
            for (unsigned j = 0; j < session->art_count; ++j) {
                const unsigned char *record = session->art_records + record_size * j;
                if (strncmp((const char *)record, store->apps[i].package.id, 48)) continue;
                memcpy(artwork.icons[i], record + 48, sizeof artwork.icons[i]);
                artwork.loaded[i] = true;
                break;
            }
        }
    }
    char folder[STORE_PATH];
    int length = snprintf(folder, sizeof folder, "%s" STORE_DIR, store->root);
    if (length >= 0 && (size_t)length < sizeof folder) artwork_update(&artwork, folder, store);
    for (unsigned i = 0; i < store->count; ++i)
        if (artwork.loaded[i]) store_sync_icon_pixels(store, &store->apps[i], artwork.icons[i]);
}

static void open_session(Session *session, bool hosted) {
    memset(catalogs, 0, sizeof catalogs);
    session->view.inside_iz2s = hosted;
    store_migrate(SD_ROOT);
    session->check_on_start = access(CHECK_REQUEST, F_OK) == 0;
    session->dirty = true;
    store_settings_read(SD_ROOT, &session->view.settings);
    start_task(&session->task, &session->thread, &session->view, &catalogs[0], &catalogs[1], REFRESH);
}

static bool tick_session(Session *session, unsigned pressed, bool touching, int x, int y) {
    View *view = &session->view;
    Task *task = &session->task;
    Store *store = &catalogs[session->active];
    if (view->busy && atomic_load(&task->done)) {
        threadWaitForExit(&session->thread);
        threadClose(&session->thread);
        unsigned selected = view->count ? view->visible[view->cursor] : STORE_MAX_APPS;
        bool app_action = task->command == INSTALL || task->command == REMOVE;
        bool changed = app_action || (task->ok && task->command != CHECK_NETWORK);
        if (changed) session->active ^= 1u;
        store = &catalogs[session->active];
        if (changed) sync_artwork(session, store);
        view->busy = false;
        view->failure = !task->ok && !atomic_load(&task->cancel);
        view->cancellable = false;
        view->minimized = false;
        snprintf(view->message, sizeof view->message, "%s", task->message);
        snprintf(view->network, sizeof view->network, "%s", task->network);
        snprintf(view->addresses, sizeof view->addresses, "%s", task->addresses);
        snprintf(view->github, sizeof view->github, "%s", task->github);
        if (task->command == REFRESH) view->cursor = 0;
        view_filter(view, store);
        if (app_action && (view->screen == DETAILS || view->screen == REPOSITORY)) {
            unsigned cursor = 0;
            while (cursor < view->count && view->visible[cursor] != selected) ++cursor;
            if (cursor < view->count) view->cursor = cursor;
            else view->screen = CATALOG;
        }
        if (task->command == CHECK_NETWORK || task->command == FETCH_LIST || task->command == STARTUP_CHECK) {
            view->screen = SETTINGS_RESULT;
            snprintf(view->result_title, sizeof view->result_title, "%s",
                task->command == CHECK_NETWORK ? "GitHub connection test" : "Catalog download");
        }
        if (session->check_on_start && task->command == REFRESH) {
            session->check_on_start = false;
            view->screen = SETTINGS;
            start_task(task, &session->thread, view, store, &catalogs[session->active ^ 1u], STARTUP_CHECK);
        }
        session->dirty = true;
    }
    unsigned percent = atomic_load(&task->percent);
    if (view->busy && percent != view->percent) { view->percent = percent; session->dirty = true; }
    Command command = view_input(view, store, pressed);
    if (touching && !session->touched && !pressed) command = view_touch(view, store, x, y);
    if (command == CANCEL_DOWNLOAD) {
        atomic_store(&task->cancel, true);
        view->cancellable = false;
        snprintf(view->message, sizeof view->message, "Cancelling...");
        session->dirty = true;
        command = NO_JOB;
    }
    if (command == EXIT) return false;
    if (command == EDIT_URL) {
        command = NO_JOB;
        if (edit_catalog_url(view)) command = view->settings.url[0] ? FETCH_LIST : REFRESH;
    }
    if (command != NO_JOB && !view->busy)
        start_task(task, &session->thread, view, store, &catalogs[session->active ^ 1u], command);
    if (pressed || (touching && !session->touched) || command != NO_JOB) session->dirty = true;
    session->touched = touching;
    return true;
}

static bool step(Session *session, unsigned pressed) {
    HidTouchScreenState touch = {0};
    bool touching = hidGetTouchScreenStates(&touch, 1) > 0 && touch.count > 0;
    return tick_session(session, pressed, touching,
        touching ? (int)touch.touches[0].x : 0, touching ? (int)touch.touches[0].y : 0);
}

static void close_session(Session *session) {
    if (session->view.busy) {
        threadWaitForExit(&session->thread);
        threadClose(&session->thread);
        session->view.busy = false;
    }
}

#ifdef IZ_STORE_HOSTED
enum { STORE_FRAME_OFFSET = 0x200000 };
static IzNativeApp *host_owner;
static Session hosted_session;

static bool hosted_artwork(Session *session, const IzNativeApp *app) {
    const unsigned char *start = (const unsigned char *)app->controls + 1024;
    if (memcmp(start,"IZSTOREA",8) || start[8] > STORE_MAX_APPS) return false;
    session->art_count = start[8];
    const unsigned char *fonts = start + 12;
    const unsigned char *brand = fonts + sizeof artwork.fonts;
    const unsigned char *records = brand + sizeof artwork.brand;
    if (12 + sizeof artwork.fonts + sizeof artwork.brand +
        session->art_count * (48 + sizeof artwork.icons[0]) > STORE_FRAME_OFFSET - 1024) return false;
    memcpy(artwork.fonts, fonts, sizeof artwork.fonts);
    memcpy(artwork.brand, brand, sizeof artwork.brand);
    artwork.brand_loaded = true;
    session->art_records = records;
    return true;
}

uint32_t iz_store_iza_event(IzNativeApp *app, uint32_t event) {
    Session *session = &hosted_session;
    if (event == IZ_NATIVE_INIT) {
        if (host_owner || !app || !app->controls || !app->buttons || !app->request_exit ||
            app->controls_bytes < STORE_FRAME_OFFSET + SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t)) return 1;
        memset(session, 0, sizeof *session);
        if (!hosted_artwork(session, app)) return 1;
        host_owner = app;
        session->held = app->buttons(app);
        open_session(session, true);
    } else if (app != host_owner || !app) return 0;
    else if (event == IZ_NATIVE_STOP) {
        if (session->view.busy && !atomic_load(&session->task.done)) return IZ_NATIVE_STOP_PENDING;
        close_session(session);
        host_owner = NULL;
        return 0;
    } else if (event == IZ_NATIVE_FRAME) {
        u64 held = app->buttons(app);
        unsigned pressed = buttons(held & ~session->held);
        session->held = held;
        if (!step(session, pressed)) app->request_exit(app);
    }
    if (session->dirty) {
        uint32_t *pixels = (void *)((unsigned char *)app->controls + STORE_FRAME_OFFSET);
        view_draw(pixels, SCREEN_WIDTH, &artwork, &catalogs[session->active], &session->view);
        session->dirty = false;
    }
    return 0;
}
#else
int main(void) {
    bool mounted = !fsdevGetDeviceFileSystem("sdmc");
    if (mounted && R_FAILED(fsdevMountSdmc())) return 1;
    Result result = romfsInit();
    bool have_romfs = R_SUCCEEDED(result);
    Framebuffer framebuffer = {0};
    bool have_framebuffer = false;
    if (have_romfs && artwork_open(&artwork, "romfs:")) {
        result = framebufferCreate(&framebuffer, nwindowGetDefault(), SCREEN_WIDTH, SCREEN_HEIGHT, PIXEL_FORMAT_RGBA_8888, 2);
        have_framebuffer = R_SUCCEEDED(result);
        if (have_framebuffer) result = framebufferMakeLinear(&framebuffer);
    } else result = 1;
    if (R_SUCCEEDED(result)) {
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        PadState pad;
        padInitializeDefault(&pad);
        hidInitializeTouchScreen();
        Session session = {0};
        open_session(&session, false);
        while (appletMainLoop()) {
            padUpdate(&pad);
            if (!step(&session, buttons(padGetButtonsDown(&pad)))) break;
            if (session.dirty) {
                uint32_t stride;
                uint32_t *pixels = framebufferBegin(&framebuffer, &stride);
                if (!pixels) { result = 1; break; }
                view_draw(pixels, stride / sizeof *pixels, &artwork, &catalogs[session.active], &session.view);
                framebufferEnd(&framebuffer);
                session.dirty = false;
            }
            svcSleepThread(12000000);
        }
        close_session(&session);
    }
    if (have_framebuffer) framebufferClose(&framebuffer);
    if (have_romfs) romfsExit();
    if (mounted) fsdevUnmountDevice("sdmc");
    return R_FAILED(result) ? 1 : 0;
}
#endif
