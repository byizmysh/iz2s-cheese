#include <sb4/native-app.h>
typedef uint32_t (*StoreEvent)(IzNativeApp *, uint32_t);

__attribute__((section(".text.iz_app")))
uint32_t iz_app(IzNativeApp *app, uint32_t event) {
    const Sb4Compat *services = sb4ModuleCompat(app);
    if (!services) return event == IZ_NATIVE_STOP ? 0 : 1;
    StoreEvent dispatch = (StoreEvent)services->resolve_c("sb4IZ2SStoreEvent");
    if (!dispatch) return event == IZ_NATIVE_STOP ? 0 : 1;
    return dispatch(app, event);
}
