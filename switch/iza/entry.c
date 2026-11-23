#include "native-state.h"

static int import_is(const char *actual, const char *expected) {
    while (*actual && *actual == *expected) { ++actual; ++expected; }
    return *actual == *expected;
}

/* Keep the import in the application manifest for launchers that export it.
 * Earlier service tables omit this libc diagnostic; a failed assertion is
 * still fatal, but its absence must not reject an otherwise usable app. */
__attribute__((noreturn)) static void store_assert_fallback(
    const char *file, int line, const char *function, const char *expression) {
    (void)file; (void)line; (void)function; (void)expression;
    __builtin_trap();
}

uint32_t iz_store_native_event(IzNativeApp *app, uint32_t event) {
    const Sb4Compat *api = sb4ModuleCompat(app);
    if (!api || !app->data || app->data_bytes < sizeof(StoreNativeState))
        return event == IZ_NATIVE_STOP ? 0 : 0xe870;
    StoreNativeState *state = app->data;
    if (event == IZ_NATIVE_INIT) {
        if (state->session) return 0xe875;
        /* Resolve each required service independently of the runtime release. */
        for (unsigned i = 0; i < STORE_IMPORT_COUNT; ++i) {
            state->imports[i] = api->resolve_c ? api->resolve_c(store_import_names[i]) : 0;
            if (!state->imports[i] && import_is(store_import_names[i], "__assert_func"))
                state->imports[i] = (Sb4CFunction)store_assert_fallback;
            if (!state->imports[i]) {
                unsigned bytes = 0;
                while (store_import_names[i][bytes]) ++bytes;
                if (app->write_file) app->write_file(app, "missing-sdk-import.txt", store_import_names[i], bytes);
                return 0xe872;
            }
        }
    }
    return iz_store_iza_event(app, event);
}
