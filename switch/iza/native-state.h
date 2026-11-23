#ifndef IZ_STORE_NATIVE_STATE_H
#define IZ_STORE_NATIVE_STATE_H

#define STORE_APP_DATA_OFFSET 16
#define STORE_TASK_STATE_OFFSET 0

#ifndef __ASSEMBLER__
#include <sb4/native-app.h>
#include "store-imports.h"

typedef struct {
    Sb4CFunction imports[STORE_IMPORT_COUNT];
    struct Session *session;
} StoreNativeState;

_Static_assert(offsetof(IzNativeApp, data) == STORE_APP_DATA_OFFSET, "SDK app data offset");
_Static_assert(offsetof(StoreNativeState, imports) == 0, "Import table offset");

uint32_t iz_store_iza_event(IzNativeApp *app, uint32_t event);
__attribute__((visibility("hidden"))) void iz_store_worker(void *task);
void iz_store_run_task(void *task);
#endif

#endif
