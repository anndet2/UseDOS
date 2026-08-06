org 0x7c00
bits 16

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00

    ; Save boot drive number
    mov [boot_drive], dl

    ; Read kernel from disk to 0x7E00
    ; Kernel is at LBA sector 1 (right after boot sector)
    ; Sector count will be patched by create_floppy.py after build
    mov bx, 0x7e00          ; Buffer address
    mov ah, 0x02            ; Read sectors
    mov al, 0x00            ; PLACEHOLDER - will be patched to sector count
    mov ch, 0x00            ; Cylinder 0
    mov cl, 0x02            ; Sector 2 (1-based, so sector 2 = LBA 1)
    mov dh, 0x00            ; Head 0
    mov dl, [boot_drive]    ; Drive number
    int 0x13
    jc disk_error

    ; Enter protected mode
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp CODE_SEG:init_pm

disk_error:
    mov si, disk_err_msg
    mov ah, 0x0e
.print_err:
    lodsb
    test al, al
    jz .hang
    int 0x10
    jmp .print_err
.hang:
    jmp .hang

bits 32
init_pm:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    jmp 0x7e00

gdt_start:
gdt_null:
    dd 0x0
    dd 0x0
gdt_code:
    dw 0xffff
    dw 0x0
    db 0x0
    db 0b10011010
    db 0b11001111
    db 0x0
gdt_data:
    dw 0xffff
    dw 0x0
    db 0x0
    db 0b10010010
    db 0b11001111
    db 0x0
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

boot_drive db 0
disk_err_msg db 'Disk read error!', 0

times 510 - ($ - $$) db 0
dw 0xaa55

; Kernel binary starts at sector 1 in the floppy image
incbin "ckernel/kernel.bin"
