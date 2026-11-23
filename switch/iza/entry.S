#include "native-state.h"

.arch armv8-a
.section .text.iz_app,"ax",%progbits
.global iz_app
.type iz_app,%function
iz_app:
    bti c
    stp x28, x30, [sp, #-16]!
    cbz x0, 1f
    ldr x28, [x0, #STORE_APP_DATA_OFFSET]
1:
    bl iz_store_native_event
    ldp x28, x30, [sp], #16
    ret
.size iz_app, .-iz_app

.section .text.iz_store_worker,"ax",%progbits
.global iz_store_worker
.hidden iz_store_worker
.type iz_store_worker,%function
iz_store_worker:
    bti c
    stp x28, x30, [sp, #-16]!
    ldr x28, [x0, #STORE_TASK_STATE_OFFSET]
    bl iz_store_run_task
    ldp x28, x30, [sp], #16
    ret
.size iz_store_worker, .-iz_store_worker
