# UseDOS

A simple operating system built from scratch in C and x86 Assembly.

## News

### version 1.1

- **Fixed cursor position of feditor app**
- **Added glof(get length of file) command**



## Features

- **32-bit Protected Mode Kernel** - Runs in protected mode with flat memory model
- **VGA Text Mode Display** - Direct framebuffer writing with hardware cursor support
- **PS/2 Keyboard Driver** - Full scancode handling with shift key support
- **In-Memory File System** - Up to 64 files/folders with 32-character filenames
- **Built-in Applications** - Text editor, assembler, file reader
- **Floppy Disk Persistence** - Save/load filesystem to 8MB floppy image

## Quick Start

### Prerequisites

- Windows 10/11
- [NASM](https://www.nasm.us/) 2.15+
- [MinGW-w64](https://www.mingw-w64.org/) (GCC 8+ with 32-bit support)
- [QEMU](https://www.qemu.org/) 6.0+

### Build

```batch
build.bat
```

This will:
1. Assemble the bootloader and kernel entry
2. Compile the C kernel and applications
3. Link everything into a flat binary
4. Create 8MB floppy image (`os-image.bin`)
5. Create bootable ISO (`useDOS.iso`)

### Run

```batch
run.bat
```

Or manually with QEMU:

```batch
qemu-system-i386 -drive file=os-image.bin,format=raw -boot a
```

## Commands

| Command | Description |
|---------|-------------|
| `new <name>` | Create file (with extension) or folder (no extension) |
| `inf <dir>` | Enter a folder |
| `inf ..` | Go to parent directory |
| `inf /` | Go to root directory |
| `ls` | List files in current directory |
| `clear` | Clear screen |
| `run <app> [args]` | Run a built-in application |
| `runbin <file.bin>` | Run a binary file (loads at 0x10000) |
| `smem` | Save filesystem to floppy disk |
| `lmem` | Load filesystem from floppy disk |
| `help` | Show available commands |

## Built-in Applications

### feditor

Simple text editor with line numbers.

- **F1** - Save as (prompts for filename)
- **ESC** - Exit editor
- **Arrow keys** - Navigate
- **Home/End** - Jump to line start/end
- **Tab** - Insert spaces (align to 4-column boundary)
- **Enter** - New line (max 100 lines)
- **Backspace** - Delete character or merge lines

```
run feditor
```

### nasm

x86 assembler for 16-bit/32-bit real mode code.

```
run feditor          # Create assembly file
; Write code and save as test.asm
run nasm test.asm    # Assemble to test.bin
runbin test.bin      # Execute binary
```

Supported instructions:
- Data movement: `mov`, `push`, `pop`
- Arithmetic: `add`, `sub`, `cmp`, `inc`, `dec`
- Logic: `and`, `or`, `xor`
- Control flow: `jmp`, `call`, `ret`, conditional jumps
- I/O: `in`, `out`, `int`
- String: `stosw`, `lodsw`, `movsb`, `rep`
- Pseudo: `label:`, `db`

### freader

Read and display file contents with line numbers.

```
run freader test.txt
```

## Project Structure

```
UseDOS/
├── boot.asm           # Bootloader (loads kernel to 0x7E00)
├── build.bat          # Build script
├── run.bat            # QEMU launch script
├── create_floppy.py   # Floppy image generator
├── os-image.bin       # 8MB floppy image (output)
├── useDOS.iso         # Bootable ISO (output)
│
├── ckernel/           # C kernel source
│   ├── entry.asm      # Kernel entry (BSS clearing)
│   ├── kernel.c       # Main kernel (VGA, keyboard, FS)
│   ├── feditor.c      # Text editor app
│   ├── freader.c      # File reader app
│   ├── nasmc.c        # Assembler app
│   ├── apps.h         # Application framework header
│   └── linker.ld      # Linker script
│
└── dist/              # Tools
    └── iso.exe        # ISO creator
```

## Technical Details

### Boot Process

1. BIOS loads sector 0 to `0x7C00` (bootloader)
2. Bootloader reads sectors 1-66 to `0x7E00` (kernel)
3. Sets up GDT and enables protected mode
4. Jumps to kernel entry point

### Memory Map

| Address | Size | Usage |
|---------|------|-------|
| 0x7C00 | 512B | Bootloader |
| 0x7E00 | ~33KB | Kernel code + data |
| 0x10000 | - | Binary execution area |
| 0x20000 | 128KB | Disk I/O buffer |
| 0xB8000 | 4KB | VGA text buffer |

### File System Format

```c
struct fs_entry {
    char name[32];        // Filename
    int is_directory;     // 1 = folder, 0 = file
    int used;             // Entry in use
    char content[512];    // File content
    int content_length;   // Actual content length
};
```

Storage: 64 entries maximum (~37KB total)

### Persistence Format

- **Magic**: `0x5553444F` ("USDF")
- **Location**: Floppy sectors 12-85 (LBA)
- **Format**: Magic(4B) + Count(4B) + FS data

## Development

### Adding New Applications

1. Create `ckernel/yourapp.c` with:
```c
#include "apps.h"

int yourapp_main(int argc, char *argv[]) {
    print("Hello from your app!\n");
    return 0;
}
```

2. Register in `kernel.c`:
```c
extern int yourapp_main(int argc, char *argv[]);

static app_t app_table[] = {
    {"yourapp", yourapp_main, "Description"},
    ...
};
```

3. Add to `build.bat`:
```batch
gcc -m32 -ffreestanding -nostdlib -fno-pic -fno-pie -c ckernel\yourapp.c -o ckernel\yourapp.o
ld ... ckernel\yourapp.o ...
```

### Kernel Functions Available to Apps

```c
void print(const char *str);
void putchar(char c);
void clear_screen(void);
void set_cursor(int x, int y);
int get_key(void);
int fs_find(const char *name);
int fs_write(const char *name, const char *content, int length);
```

## Limitations

- Single-tasking (no process scheduler)
- No memory protection
- File content limited to 512 bytes
- Max 64 files/folders
- No subdirectory navigation (flat filesystem)
- 32-bit only (no 64-bit support)

## License

MIT License - See [LICENSE](LICENSE) file.

## Author

UseDOS Project - An educational operating system for learning x86 low-level programming.