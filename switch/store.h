#ifndef IZ_HOMEBREW_STORE_H
#define IZ_HOMEBREW_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "download.h"

#define STORE_MAX_APPS 24
#define STORE_MAX_FILES 17
#define STORE_PATH 512
#define STORE_MESSAGE 160
#define STORE_DIR "/iz2s/iz2s-store"
#define MODULE_DIR "/iz2s/modules"

void store_migrate(const char *root);

typedef enum { PACKAGE_IZM, PACKAGE_NRO, PACKAGE_IZM_NRO } PackageKind;
typedef enum { NOT_INSTALLED, INSTALLED, UPDATE_AVAILABLE, EXTERNAL, UNAVAILABLE } InstallState;

typedef struct {
    char name[80];
    char digest[65];
    uint64_t bytes;
} PackageFile;

typedef struct {
    char id[48], title[64], author[60], version[24], category[24];
    char description[480];
    char release_url[RELEASE_URL_MAX];
    char repository[RELEASE_URL_MAX];
    PackageKind kind;
    unsigned count;
    uint32_t owned;
    PackageFile files[STORE_MAX_FILES];
} Package;

typedef struct {
    Package package;
    InstallState state;
    char installed_version[24];
    int icon;
} App;

typedef struct {
    char root[STORE_PATH];
    unsigned count, remote_count;
    App apps[STORE_MAX_APPS];
} Store;

typedef struct { char url[RELEASE_URL_MAX]; } StoreSettings;

typedef void (*StoreProgress)(void *, uint64_t done, uint64_t total);
typedef struct { StoreProgress report; void *context; const atomic_bool *cancel; } Progress;

bool store_load(Store *store, const char *root, char message[STORE_MESSAGE]);
void store_settings_read(const char *root, StoreSettings *settings);
bool store_settings_write(const char *root, const StoreSettings *settings, char message[STORE_MESSAGE]);
bool store_fetch_list(Store *store, const StoreSettings *settings, char message[STORE_MESSAGE]);
void store_refresh(const Store *store, App *app);
void store_sync_icon(const Store *store, const App *app, const char *artwork);
void store_sync_icon_pixels(const Store *store, const App *app, const void *rgba);
bool store_install(const Store *store, const App *app, Progress progress, char message[STORE_MESSAGE]);
bool store_remove(const Store *store, const App *app, char message[STORE_MESSAGE]);
const char *store_extension(PackageKind kind);
void store_repository(const Package *package, char url[RELEASE_URL_MAX]);

#endif
