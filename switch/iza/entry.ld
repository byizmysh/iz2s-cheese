ENTRY(iz_app)
PHDRS { code PT_LOAD FLAGS(5); }
SECTIONS {
    . = 0;
    .text : { KEEP(*(.text.iz_app)) *(.text*) *(.rodata*) . = ALIGN(4); } :code
    .data : { *(.data*) *(.sdata*) }
    .bss (NOLOAD) : { *(.bss*) *(.sbss*) *(COMMON) }
    .got : { *(.got*) *(.igot*) }
    .tls : { *(.tdata*) *(.tbss*) }
    /DISCARD/ : { *(.comment*) *(.note*) *(.eh_frame*) }
    ASSERT(iz_app == ADDR(.text), "native entry must be the first instruction")
    ASSERT(SIZEOF(.data) == 0, "native image must not contain writable static data")
    ASSERT(SIZEOF(.bss) == 0, "native state must live in the app allocation")
    ASSERT(SIZEOF(.got) == 0, "native image must not require a GOT")
    ASSERT(SIZEOF(.tls) == 0, "native image must not require static TLS")
}
