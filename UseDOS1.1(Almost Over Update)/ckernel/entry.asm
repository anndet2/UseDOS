bits 32

section .text
global _start

extern _kernel_main
extern __bss_start
extern __bss_end

_start:
    ; Zero out .bss section
    ; __bss_start and __bss_end are defined in linker.ld
    mov ecx, __bss_start
    mov edx, __bss_end
.bss_loop:
    cmp ecx, edx
    jge .bss_done
    mov byte [ecx], 0
    inc ecx
    jmp .bss_loop
.bss_done:

    call _kernel_main
    cli
.hang:
    hlt
    jmp .hang