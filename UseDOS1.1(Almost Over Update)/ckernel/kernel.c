/* UseDOS C Kernel */

#define VGA_BUFFER ((volatile unsigned char *)0xb8000)
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define KEYBOARD_PORT 0x60

/* File system definitions */
#define MAX_FILES 128
#define MAX_FILENAME 16

/* Application definitions */
#define MAX_APPS 16
#define MAX_ARGS 8

/* Include shared application header (defines fs_entry_t, app_t, etc.) */
#include "apps.h"

/* Forward declarations of application entry points */
extern int freader_main(int argc, char *argv[]);
extern int feditor_main(int argc, char *argv[]);
extern int nasmc_main(int argc, char *argv[]);

/* Built-in application table */
static app_t app_table[] = {
    {"freader", freader_main, "you can only read files in this usedosapp.Is abandoned."},
    {"feditor", feditor_main, "you can only edit files in this usedosapp."},
    {"nasm", nasmc_main, "x86 assembler. Usage: nasm <file.asm>"},
    {0, 0, 0} /* Terminator */
};

/* Simple in-memory file system (global for apps to access) */
fs_entry_t fs[MAX_FILES];
int fs_count = 0;

static int cursor_x = 0;
static int cursor_y = 0;
static int prompt_x = 0;
static int prompt_y = 0;

/* Current directory */
static char current_dir[1024] = "A:/";

/* Command buffer */
static char cmd_buffer[256];
static int cmd_len = 0;

/* Forward declarations */
void strcpy(char *dest, const char *src);
void int_to_str(int num, char *buf);
void float_to_str(float num, char *buf);
void unit_change_bits(int bits_number, int which, char *buf);

/* Extended key codes for special keys */
#define KEY_UP 0x100
#define KEY_DOWN 0x101
#define KEY_LEFT 0x102
#define KEY_RIGHT 0x103
#define KEY_HOME 0x104
#define KEY_END 0x105

static inline void outb(unsigned short port, unsigned char val)
{
    __asm__ __volatile__("outb %0, %1" ::"a"(val), "Nd"(port));
}

static inline unsigned char inb(unsigned short port)
{
    unsigned char val;
    __asm__ __volatile__("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

void set_cursor(int x, int y)
{
    unsigned short pos = y * 80 + x;
    outb(0x3D4, 0x0E);
    outb(0x3D5, pos >> 8);
    outb(0x3D4, 0x0F);
    outb(0x3D5, pos & 0xFF);
}

void clear_screen(void)
{
    volatile unsigned short *vga = (volatile unsigned short *)VGA_BUFFER;
    int i;
    for (i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
    {
        vga[i] = 0x0720;
    }
    cursor_x = 0;
    cursor_y = 0;
    set_cursor(0, 0);
}

void scroll_screen(void)
{
    volatile unsigned short *vga = (volatile unsigned short *)VGA_BUFFER;
    int i, j;

    /* Move all lines up by one */
    for (i = 0; i < VGA_HEIGHT - 1; i++)
    {
        for (j = 0; j < VGA_WIDTH; j++)
        {
            vga[i * VGA_WIDTH + j] = vga[(i + 1) * VGA_WIDTH + j];
        }
    }

    /* Clear the last line */
    for (j = 0; j < VGA_WIDTH; j++)
    {
        vga[(VGA_HEIGHT - 1) * VGA_WIDTH + j] = 0x0720;
    }

    /* Update prompt position after scroll */
    if (prompt_y > 0)
    {
        prompt_y--;
    }
}

void putchar(char c)
{
    volatile unsigned short *vga = (volatile unsigned short *)VGA_BUFFER;
    int index;

    if (c == '\n')
    {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= VGA_HEIGHT)
        {
            scroll_screen();
            cursor_y = VGA_HEIGHT - 1;
        }
        set_cursor(cursor_x, cursor_y);
        return;
    }

    if (c == '\b')
    {
        /* Don't allow backspace past prompt */
        if (cursor_y == prompt_y && cursor_x <= prompt_x)
            return;
        if (cursor_x > 0)
        {
            cursor_x--;
            index = cursor_y * VGA_WIDTH + cursor_x;
            vga[index] = 0x0720;
            set_cursor(cursor_x, cursor_y);
        }
        return;
    }

    if (cursor_x >= VGA_WIDTH)
    {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= VGA_HEIGHT)
        {
            scroll_screen();
            cursor_y = VGA_HEIGHT - 1;
        }
    }

    if (cursor_y >= VGA_HEIGHT)
    {
        scroll_screen();
        cursor_y = VGA_HEIGHT - 1;
    }

    index = cursor_y * VGA_WIDTH + cursor_x;
    vga[index] = (0x07 << 8) | c;
    cursor_x++;
    set_cursor(cursor_x, cursor_y);
}

void print(const char *str)
{
    while (*str)
    {
        putchar(*str);
        str++;
    }
}

void print_prompt(void)
{
    print(current_dir);
    print("> ");
}

unsigned char get_scancode(void)
{
    unsigned char status = inb(0x64);
    if (!(status & 0x01))
        return 0;

    return inb(KEYBOARD_PORT);
}

/* Get extended key code (includes arrow keys) */
int get_key(void)
{
    unsigned char scancode;

    scancode = get_scancode();
    if (scancode == 0)
        return 0;

    /* Extended key prefix */
    if (scancode == 0xE0)
    {
        /* Wait for next byte */
        while (!(inb(0x64) & 0x01))
            ;
        scancode = inb(KEYBOARD_PORT);

        /* Map extended scancodes */
        switch (scancode)
        {
        case 0x48:
            return KEY_UP;
        case 0x50:
            return KEY_DOWN;
        case 0x4B:
            return KEY_LEFT;
        case 0x4D:
            return KEY_RIGHT;
        case 0x47:
            return KEY_HOME;
        case 0x4F:
            return KEY_END;
        default:
            return 0;
        }
    }

    return scancode;
}

char scancode_to_ascii(unsigned char scancode)
{
    static int shift = 0;
    char key;

    /* Shift pressed */
    if (scancode == 0x2A || scancode == 0x36)
    {
        shift = 1;
        return 0;
    }
    /* Shift released */
    if (scancode == 0xAA || scancode == 0xB6)
    {
        shift = 0;
        return 0;
    }

    if (scancode >= 0x80)
        return 0;

    switch (scancode)
    {
    case 0x02:
        key = shift ? '!' : '1';
        break;
    case 0x03:
        key = shift ? '@' : '2';
        break;
    case 0x04:
        key = shift ? '#' : '3';
        break;
    case 0x05:
        key = shift ? '$' : '4';
        break;
    case 0x06:
        key = shift ? '%' : '5';
        break;
    case 0x07:
        key = shift ? '^' : '6';
        break;
    case 0x08:
        key = shift ? '&' : '7';
        break;
    case 0x09:
        key = shift ? '*' : '8';
        break;
    case 0x0A:
        key = shift ? '(' : '9';
        break;
    case 0x0B:
        key = shift ? ')' : '0';
        break;
    case 0x0C:
        key = shift ? '_' : '-';
        break;
    case 0x0D:
        key = shift ? '+' : '=';
        break;
    case 0x10:
        key = shift ? 'Q' : 'q';
        break;
    case 0x11:
        key = shift ? 'W' : 'w';
        break;
    case 0x12:
        key = shift ? 'E' : 'e';
        break;
    case 0x13:
        key = shift ? 'R' : 'r';
        break;
    case 0x14:
        key = shift ? 'T' : 't';
        break;
    case 0x15:
        key = shift ? 'Y' : 'y';
        break;
    case 0x16:
        key = shift ? 'U' : 'u';
        break;
    case 0x17:
        key = shift ? 'I' : 'i';
        break;
    case 0x18:
        key = shift ? 'O' : 'o';
        break;
    case 0x19:
        key = shift ? 'P' : 'p';
        break;
    case 0x1A:
        key = shift ? '{' : '[';
        break;
    case 0x1B:
        key = shift ? '}' : ']';
        break;
    case 0x1C:
        key = '\n';
        break;
    case 0x2B:
        key = shift ? '|' : '\\';
        break;
    case 0x1E:
        key = shift ? 'A' : 'a';
        break;
    case 0x1F:
        key = shift ? 'S' : 's';
        break;
    case 0x20:
        key = shift ? 'D' : 'd';
        break;
    case 0x21:
        key = shift ? 'F' : 'f';
        break;
    case 0x22:
        key = shift ? 'G' : 'g';
        break;
    case 0x23:
        key = shift ? 'H' : 'h';
        break;
    case 0x24:
        key = shift ? 'J' : 'j';
        break;
    case 0x25:
        key = shift ? 'K' : 'k';
        break;
    case 0x26:
        key = shift ? 'L' : 'l';
        break;
    case 0x27:
        key = shift ? ':' : ';';
        break;
    case 0x28:
        key = shift ? '"' : '\'';
        break;
    case 0x29:
        key = shift ? '~' : '`';
        break;
    case 0x2C:
        key = shift ? 'Z' : 'z';
        break;
    case 0x2D:
        key = shift ? 'X' : 'x';
        break;
    case 0x2E:
        key = shift ? 'C' : 'c';
        break;
    case 0x2F:
        key = shift ? 'V' : 'v';
        break;
    case 0x30:
        key = shift ? 'B' : 'b';
        break;
    case 0x31:
        key = shift ? 'N' : 'n';
        break;
    case 0x32:
        key = shift ? 'M' : 'm';
        break;
    case 0x33:
        key = shift ? '<' : ',';
        break;
    case 0x34:
        key = shift ? '>' : '.';
        break;
    case 0x35:
        key = shift ? '?' : '/';
        break;
    case 0x39:
        key = ' ';
        break;
    case 0x0E:
        key = '\b';
        break;
    default:
        key = 0;
        break;
    }
    return key;
}

/* File system operations */
int fs_create(const char *name, int is_directory)
{
    int i;
    /* Check if name already exists */
    for (i = 0; i < MAX_FILES; i++)
    {
        if (fs[i].used)
        {
            int j;
            int match = 1;
            for (j = 0; j < MAX_FILENAME && name[j] && fs[i].name[j]; j++)
            {
                if (name[j] != fs[i].name[j])
                {
                    match = 0;
                    break;
                }
            }
            if (match && name[j] == 0 && fs[i].name[j] == 0)
            {
                return -1; /* Already exists */
            }
        }
    }

    /* Find free slot */
    for (i = 0; i < MAX_FILES; i++)
    {
        if (!fs[i].used)
        {
            int j;
            fs[i].used = 1;
            fs[i].is_directory = is_directory;
            for (j = 0; j < MAX_FILENAME && name[j]; j++)
            {
                fs[i].name[j] = name[j];
            }
            fs[i].name[j] = 0;
            fs_count++;
            return 0;
        }
    }
    return -2; /* No space */
}

/* Write content to a file. If file doesn't exist, create it. */
int fs_write(const char *name, const char *content, int length)
{
    int idx;
    int i;

    /* Find existing file */
    idx = fs_find(name);

    /* If not found, create it */
    if (idx < 0)
    {
        if (fs_create(name, 0) < 0)
            return -1; /* Create failed */
        idx = fs_find(name);
        if (idx < 0)
            return -1;
    }

    /* Truncate length if needed */
    if (length > FILE_CONTENT_SIZE - 1)
        length = FILE_CONTENT_SIZE - 1;

    /* Copy content */
    for (i = 0; i < length; i++)
    {
        fs[idx].content[i] = content[i];
    }
    fs[idx].content[length] = 0;
    fs[idx].content_length = length;

    return 0;
}

/* ==================== Floppy Disk I/O ==================== */

#define FDC_DOR 0x3F2 /* Digital Output Register */
#define FDC_MSR 0x3F4 /* Main Status Register */
#define FDC_DR 0x3F5  /* Data Register */
#define FDC_DIR 0x3F7 /* Digital Input Register */

#define FLOPPY_SECTOR_SIZE 512
#define FLOPPY_FS_START_SECTOR 12 /* Store FS at sector 12 */

/* Wait for FDC to be ready */
static int fdc_wait_ready(void)
{
    int timeout = 1000000;
    while (timeout--)
    {
        if (inb(FDC_MSR) & 0x80) /* RQM bit set */
            return 1;
    }
    return 0;
}

/* Wait for FDC to become non-busy */
static int fdc_wait_not_busy(void)
{
    int timeout = 1000000;
    while (timeout--)
    {
        if (!(inb(FDC_MSR) & 0x10)) /* BSY bit clear */
            return 1;
    }
    return 0;
}

/* Turn on floppy motor and select drive 0 */
static void floppy_motor_on(void)
{
    outb(FDC_DOR, 0x1C); /* Motor on, drive 0, normal operation */
}

/* Turn off floppy motor */
static void floppy_motor_off(void)
{
    outb(FDC_DOR, 0x00);
}

/* Seek to a track */
static int floppy_seek(int track)
{
    if (!fdc_wait_not_busy())
        return -1;

    outb(FDC_DR, 0x0F);  /* Seek command */
    outb(FDC_DR, 0x00);  /* Drive 0 */
    outb(FDC_DR, track); /* Track number */

    /* Wait for seek complete */
    if (!fdc_wait_not_busy())
        return -1;

    /* Check status */
    {
        int st0 = inb(FDC_DR);
        if ((st0 & 0xC0) != 0x20) /* Seek complete */
            return -1;
    }

    return 0;
}

/* Read one sector (CHS addressing) */
static int floppy_read_sector(int cyl, int head, int sector, unsigned char *buffer)
{
    int i;
    int result;

    if (!fdc_wait_not_busy())
        return -1;

    /* Send read command */
    outb(FDC_DR, 0xE6);       /* Read normal, MFM mode */
    outb(FDC_DR, head);       /* Head number */
    outb(FDC_DR, cyl);        /* Cylinder */
    outb(FDC_DR, sector);     /* Sector */
    outb(FDC_DR, sector + 1); /* EOT (sector to read until) */
    outb(FDC_DR, 0x00);       /* Gap length */
    outb(FDC_DR, 0x00);       /* Data length (0 = 512 bytes) */

    /* Wait for data */
    for (i = 0; i < FLOPPY_SECTOR_SIZE; i++)
    {
        int timeout = 100000;
        while (timeout--)
        {
            if (inb(FDC_MSR) & 0x80) /* RQM */
            {
                if (inb(FDC_MSR) & 0x40) /* DIO = input */
                {
                    buffer[i] = inb(FDC_DR);
                    break;
                }
            }
        }
        if (timeout <= 0)
            return -1;
    }

    /* Read status bytes */
    if (!fdc_wait_not_busy())
        return -1;

    result = inb(FDC_DR); /* ST0 */
    result = inb(FDC_DR); /* ST1 */
    result = inb(FDC_DR); /* ST2 */
    result = inb(FDC_DR); /* Actual cylinder */

    return 0;
}

/* Write one sector (CHS addressing) */
static int floppy_write_sector(int cyl, int head, int sector, const unsigned char *buffer)
{
    int i;
    int result;

    if (!fdc_wait_not_busy())
        return -1;

    /* Send write command */
    outb(FDC_DR, 0xC5);       /* Write, MFM mode */
    outb(FDC_DR, head);       /* Head number */
    outb(FDC_DR, cyl);        /* Cylinder */
    outb(FDC_DR, sector);     /* Sector */
    outb(FDC_DR, sector + 1); /* EOT */
    outb(FDC_DR, 0x00);       /* Gap length */
    outb(FDC_DR, 0x00);       /* Data length */

    /* Wait for data */
    for (i = 0; i < FLOPPY_SECTOR_SIZE; i++)
    {
        int timeout = 100000;
        while (timeout--)
        {
            if (inb(FDC_MSR) & 0x80) /* RQM */
            {
                if (!(inb(FDC_MSR) & 0x40)) /* DIO = output */
                {
                    outb(FDC_DR, buffer[i]);
                    break;
                }
            }
        }
        if (timeout <= 0)
            return -1;
    }

    /* Read status bytes */
    if (!fdc_wait_not_busy())
        return -1;

    result = inb(FDC_DR); /* ST0 */
    result = inb(FDC_DR); /* ST1 */
    result = inb(FDC_DR); /* ST2 */
    result = inb(FDC_DR); /* Actual cylinder */

    return 0;
}

/* Convert LBA sector to CHS for 8MB floppy */
static void lba_to_chs(int lba, int *cyl, int *head, int *sector)
{
    /* 8MB floppy: 80 cylinders, 2 heads, 63 sectors per track */
    int sectors_per_track = 63;
    int heads = 2;

    *cyl = lba / (sectors_per_track * heads);
    *head = (lba / sectors_per_track) % heads;
    *sector = (lba % sectors_per_track) + 1;
}

/* ==================== Filesystem Save/Load ==================== */

#define FS_MAGIC 0x5553444F /* "USDF" - UseDOS Filesystem */

/* Save filesystem to floppy (sectors 12+) */
int fs_save_to_disk(void)
{
    unsigned char *buffer;
    int total_size;
    int sectors_needed;
    int i, j;
    int sector_offset;
    int saved;
    unsigned int magic = FS_MAGIC;

    /* Calculate size: magic(4) + count(4) + fs data */
    total_size = 8 + sizeof(fs);
    sectors_needed = (total_size + FLOPPY_SECTOR_SIZE - 1) / FLOPPY_SECTOR_SIZE;

    print("Saving filesystem to floppy...\n");

    buffer = (unsigned char *)0x20000; /* Use 128KB buffer */

    /* Copy magic and count to buffer */
    buffer[0] = (magic >> 24) & 0xFF;
    buffer[1] = (magic >> 16) & 0xFF;
    buffer[2] = (magic >> 8) & 0xFF;
    buffer[3] = magic & 0xFF;

    buffer[4] = (fs_count >> 24) & 0xFF;
    buffer[5] = (fs_count >> 16) & 0xFF;
    buffer[6] = (fs_count >> 8) & 0xFF;
    buffer[7] = fs_count & 0xFF;

    /* Copy fs entries */
    for (i = 0; i < (int)sizeof(fs); i++)
        buffer[8 + i] = ((unsigned char *)&fs)[i];

    /* Write sectors */
    floppy_motor_on();

    saved = 0;
    for (i = 0; i < sectors_needed; i++)
    {
        int lba = FLOPPY_FS_START_SECTOR + i;
        int cyl, head, sector;
        unsigned char *sect_buf;

        lba_to_chs(lba, &cyl, &head, &sector);

        sect_buf = &buffer[i * FLOPPY_SECTOR_SIZE];
        if (floppy_seek(cyl) != 0)
        {
            print("Error: Seek failed at cylinder ");
            {
                char num[8];
                int_to_str(cyl, num);
                print(num);
                print("\n");
            }
            floppy_motor_off();
            return -1;
        }

        if (floppy_write_sector(cyl, head, sector, sect_buf) != 0)
        {
            print("Error: Write failed at sector ");
            {
                char num[8];
                int_to_str(lba, num);
                print(num);
                print("\n");
            }
            floppy_motor_off();
            return -1;
        }
        saved++;
    }

    floppy_motor_off();

    print("Saved ");
    {
        char num[8];
        int_to_str(saved, num);
        print(num);
    }
    print(" sectors.\n");
    return 0;
}

/* Load filesystem from floppy (sectors 12+) */
int fs_load_from_disk(void)
{
    unsigned char *buffer;
    int total_size;
    int sectors_needed;
    int i;
    unsigned int magic;

    buffer = (unsigned char *)0x20000;

    /* First read magic and count */
    floppy_motor_on();

    if (floppy_seek(0) != 0)
    {
        floppy_motor_off();
        return -1;
    }

    /* Read first sector to get magic */
    if (floppy_read_sector(0, 0, FLOPPY_FS_START_SECTOR, buffer) != 0)
    {
        floppy_motor_off();
        return -1;
    }

    floppy_motor_off();

    /* Check magic */
    magic = ((unsigned int)buffer[0] << 24) |
            ((unsigned int)buffer[1] << 16) |
            ((unsigned int)buffer[2] << 8) |
            ((unsigned int)buffer[3]);

    if (magic != FS_MAGIC)
    {
        return -1; /* No saved filesystem */
    }

    /* Read fs_count */
    fs_count = ((int)buffer[4] << 24) |
               ((int)buffer[5] << 16) |
               ((int)buffer[6] << 8) |
               ((int)buffer[7]);

    if (fs_count > MAX_FILES)
        return -1;

    /* Calculate sectors needed */
    total_size = 8 + sizeof(fs);
    sectors_needed = (total_size + FLOPPY_SECTOR_SIZE - 1) / FLOPPY_SECTOR_SIZE;

    /* Read all sectors */
    floppy_motor_on();

    for (i = 0; i < sectors_needed; i++)
    {
        int lba = FLOPPY_FS_START_SECTOR + i;
        int cyl, head, sector;
        unsigned char *sect_buf;

        lba_to_chs(lba, &cyl, &head, &sector);

        if (floppy_seek(cyl) != 0)
        {
            floppy_motor_off();
            return -1;
        }

        sect_buf = &buffer[i * FLOPPY_SECTOR_SIZE];
        if (floppy_read_sector(cyl, head, sector, sect_buf) != 0)
        {
            floppy_motor_off();
            return -1;
        }
    }

    floppy_motor_off();

    /* Copy fs entries */
    for (i = 0; i < (int)sizeof(fs); i++)
        ((unsigned char *)&fs)[i] = buffer[8 + i];

    return 0;
}

int fs_has_extension(const char *name)
{
    int i;
    for (i = 0; name[i]; i++)
    {
        if (name[i] == '.')
            return 1;
    }
    return 0;
}

int fs_find(const char *name)
{
    int i;
    int j;
    for (i = 0; i < MAX_FILES; i++)
    {
        if (fs[i].used)
        {
            int match = 1;
            for (j = 0; j < MAX_FILENAME && name[j] && fs[i].name[j]; j++)
            {
                if (name[j] != fs[i].name[j])
                {
                    match = 0;
                    break;
                }
            }
            if (match && name[j] == 0 && fs[i].name[j] == 0)
            {
                return i;
            }
        }
    }
    return -1;
}

void change_dir(const char *new_dir)
{
    int i;
    if (strcmp(new_dir, "..") == 0)
    {
        /* Go up one level */
        int len = 0;
        int slash_pos = -1;
        int j;
        for (j = 0; current_dir[j]; j++)
        {
            len++;
            if (current_dir[j] == '/' && j < len - 1)
                slash_pos = j;
        }
        if (slash_pos > 0)
        {
            current_dir[slash_pos] = 0;
        }
        else
        {
            strcpy(current_dir, "A:/");
        }
    }
    else if (strcmp(new_dir, "/") == 0 || strcmp(new_dir, "a:/") == 0 || strcmp(new_dir, "A:/") == 0)
    {
        strcpy(current_dir, "A:/");
    }
    else
    {
        i = fs_find(new_dir);
        if (i < 0)
        {
            print("Error: Directory not found: ");
            print(new_dir);
            print("\n");
            return;
        }
        if (!fs[i].is_directory)
        {
            print("Error: ");
            print(new_dir);
            print(" is a file, not a directory\n");
            return;
        }
        /* Append directory to current path */
        {
            int k;
            int len = 0;
            for (k = 0; current_dir[k]; k++)
                len++;
            /* Remove trailing slash if exists (but keep the root slash) */
            if (len > 3 && current_dir[len - 1] == '/')
                len--;
            current_dir[len] = '/';
            len++;
            for (k = 0; new_dir[k]; k++)
            {
                current_dir[len] = new_dir[k];
                len++;
            }
            current_dir[len] = '/';
            len++;
            current_dir[len] = 0;
        }
    }
}

/* Find and run an application by name */
static int run_app(const char *name, int argc, char *argv[])
{
    int i;

    for (i = 0; app_table[i].name != 0; i++)
    {
        if (strcmp(app_table[i].name, name) == 0)
        {
            print("Running ");
            print(name);
            print("...\n");
            return app_table[i].main(argc, argv);
        }
    }

    print("Error: Application '");
    print(name);
    print("' not found\n");
    return -1;
}

/* Command processing */
void process_command(void)
{
    char cmd[32];
    char arg[MAX_FILENAME];
    char arg_buf[MAX_ARGS][MAX_FILENAME];
    char *argv[MAX_ARGS];
    int argc = 0;
    int i, j, k;
    int is_directory;

    if (cmd_len == 0)
        return;

    /* Initialize buffers */
    for (i = 0; i < 32; i++)
        cmd[i] = 0;
    for (i = 0; i < MAX_FILENAME; i++)
        arg[i] = 0;
    for (i = 0; i < MAX_ARGS; i++)
    {
        for (j = 0; j < MAX_FILENAME; j++)
            arg_buf[i][j] = 0;
        argv[i] = 0;
    }

    /* Parse command */
    i = 0;
    while (i < cmd_len && cmd_buffer[i] != ' ' && i < 31)
    {
        cmd[i] = cmd_buffer[i];
        i++;
    }
    cmd[i] = 0;

    /* Skip space */
    while (i < cmd_len && cmd_buffer[i] == ' ')
        i++;

    /* Parse arguments (support multiple args separated by spaces) */
    argc = 0;
    while (i < cmd_len && argc < MAX_ARGS)
    {
        /* Skip spaces */
        while (i < cmd_len && cmd_buffer[i] == ' ')
            i++;
        if (i >= cmd_len)
            break;

        /* Find end of this argument */
        j = i;
        while (j < cmd_len && cmd_buffer[j] != ' ')
            j++;

        /* Copy argument to buffer */
        k = 0;
        while (i < j && k < MAX_FILENAME - 1)
        {
            arg_buf[argc][k] = cmd_buffer[i];
            i++;
            k++;
        }
        arg_buf[argc][k] = 0;
        argv[argc] = arg_buf[argc];
        argc++;
    }
    argv[argc] = 0; /* Null terminate for exec-style calls */

    /* Extract first argument for simple commands */
    if (argc > 0)
    {
        j = 0;
        while (argv[0][j])
        {
            arg[j] = argv[0][j];
            j++;
        }
        arg[j] = 0;
    }
    else
    {
        arg[0] = 0;
    }

    /* Convert command to lowercase for comparison */
    for (i = 0; cmd[i]; i++)
    {
        if (cmd[i] >= 'A' && cmd[i] <= 'Z')
            cmd[i] = cmd[i] + 32;
    }

    if (strcmp(cmd, "help") == 0)
    {
        int i;
        int count;

        print("Available commands:\n");
        print("  new <name>   - Create file or folder\n");
        print("  inf <dir>    - Enter a folder\n");
        print("  inf ..       - Go to parent directory\n");
        print("  inf /        - Go to root directory\n");
        print("  ls           - List files\n");
        print("  clear        - Clear screen\n");
        print("  run <app>    - Run an application\n");
        print("  runbin <bin> - Run a binary file\n");
        print("  smem         - Save filesystem to disk\n");
        print("  lmem         - Load filesystem from disk\n");
        print("  glof <file> [bits|bytes|kb] - Get length of file\n");
        print("  help         - Show this help\n");

        /* List available applications */
        count = 0;
        for (i = 0; app_table[i].name != 0; i++)
        {
            if (count == 0)
            {
                print("\nAvailable applications:\n");
            }
            print("  ");
            print(app_table[i].name);
            if (app_table[i].note != 0)
            {
                print(" - ");
                print(app_table[i].note);
            }
            print("\n");
            count++;
        }
        if (count == 0)
        {
            print("\nNo applications available.\n");
        }

        print("\nRules:\n");
        print("  - Names with extension = file (e.g. test.txt)\n");
        print("  - Names without extension = folder (e.g. docs)\n");
    }
    else if (strcmp(cmd, "new") == 0)
    {
        if (arg[0] == 0)
        {
            print("Usage: new <filename>\n");
        }
        else
        {
            is_directory = !fs_has_extension(arg);
            if (fs_create(arg, is_directory) == 0)
            {
                if (is_directory)
                {
                    print("Created folder: ");
                    print(arg);
                    print("/\n");
                }
                else
                {
                    print("Created file: ");
                    print(arg);
                    print("\n");
                }
            }
            else
            {
                print("Error: Cannot create ");
                print(arg);
                print("\n");
            }
        }
    }
    else if (strcmp(cmd, "ls") == 0)
    {
        int i;
        int found = 0;
        print("Directory contents:\n");
        for (i = 0; i < MAX_FILES; i++)
        {
            if (fs[i].used)
            {
                found = 1;
                print("  ");
                if (fs[i].is_directory)
                    print("<DIR>  ");
                else
                    print("FILE   ");
                print(fs[i].name);
                if (fs[i].is_directory)
                    print("/");
                print("\n");
            }
        }
        if (!found)
            print("  (empty)\n");
    }
    else if (strcmp(cmd, "clear") == 0)
    {
        clear_screen();
    }
    else if (strcmp(cmd, "inf") == 0)
    {
        if (arg[0] == 0)
        {
            print("Usage: inf <folder> or inf .. or inf /\n");
        }
        else
        {
            change_dir(arg);
        }
    }
    else if (strcmp(cmd, "run") == 0)
    {
        if (arg[0] == 0)
        {
            print("Usage: run <application> [args...]\n");
            print("Type 'help' to see available applications.\n");
        }
        else
        {
            run_app(arg, argc, argv);
        }
    }
    else if (strcmp(cmd, "runbin") == 0)
    {
        int idx;
        int load_addr = 0x10000; /* Load binary at 64KB */
        unsigned char *code;
        int i;
        int (*entry)(void);

        if (arg[0] == 0)
        {
            print("Usage: runbin <binary.bin>\n");
        }
        else
        {
            idx = fs_find(arg);
            if (idx < 0)
            {
                print("Error: File '");
                print(arg);
                print("' not found\n");
            }
            else if (fs[idx].is_directory)
            {
                print("Error: '");
                print(arg);
                print("' is a directory\n");
            }
            else if (fs[idx].content_length == 0)
            {
                print("Error: File is empty\n");
            }
            else
            {
                code = (unsigned char *)load_addr;

                /* Copy binary content to load address */
                for (i = 0; i < fs[idx].content_length; i++)
                {
                    code[i] = (unsigned char)fs[idx].content[i];
                }

                print("Running binary: ");
                print(arg);
                print(" (");
                {
                    char num_str[8];
                    int_to_str(fs[idx].content_length, num_str);
                    print(num_str);
                }
                print(" bytes at 0x10000)\n");

                /* Execute the binary */
                entry = (int (*)(void))load_addr;
                entry();

                print("\nBinary execution finished.\n");
            }
        }
    }
    else if (strcmp(cmd, "smem") == 0)
    {
        if (fs_save_to_disk() == 0)
        {
            print("Filesystem saved successfully.\n");
        }
        else
        {
            print("Error: Failed to save filesystem.\n");
        }
    }
    else if (strcmp(cmd, "lmem") == 0)
    {
        if (fs_load_from_disk() == 0)
        {
            print("Filesystem loaded successfully.\n");
        }
        else
        {
            print("No saved filesystem found.\n");
        }
    }
    else if (strcmp(cmd, "glof") == 0)
    {
        int idx;
        int length;
        char result[16];
        const char *unit;

        if (arg[0] == 0)
        {
            print("Usage: glof <filename> [bits|bytes|kb]\n");
        }
        else
        {
            idx = fs_find(arg);
            if (idx < 0)
            {
                print("Error: File '");
                print(arg);
                print("' not found\n");
            }
            else if (fs[idx].is_directory)
            {
                print("Error: '");
                print(arg);
                print("' is a directory\n");
            }
            else
            {
                length = fs[idx].content_length;
                unit = "bits";

                if (argc >= 2 && argv[1] != 0)
                    unit = argv[1];

                if (strcmp(unit, "bytes") == 0)
                {
                    unit_change_bits(length, 1, result);
                }
                else if (strcmp(unit, "kb") == 0)
                {
                    unit_change_bits(length, 2, result);
                }
                else
                {
                    unit_change_bits(length, 0, result);
                }
                print(result);
                print("\n");
            }
        }
    }
    else
    {
        print("Unknown command: ");
        print(cmd);
        print("\nType 'help' for available commands.\n");
    }
}

int strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

void strcpy(char *dest, const char *src)
{
    while (*src)
    {
        *dest = *src;
        dest++;
        src++;
    }
    *dest = 0;
}

/* Convert integer to string */
void int_to_str(int num, char *buf)
{
    char temp[16];
    int i = 0;
    int j;

    if (num == 0)
    {
        buf[0] = '0';
        buf[1] = 0;
        return;
    }

    while (num > 0)
    {
        temp[i++] = '0' + (num % 10);
        num /= 10;
    }

    for (j = 0; j < i; j++)
    {
        buf[j] = temp[i - 1 - j];
    }
    buf[i] = 0;
}

void float_to_str(float num, char *buf)
{
    char int_part[16];
    int i = 0;
    int j;
    int negative;
    int ip;
    int dp;
    int pos;
    int ii;

    negative = 0;
    if (num < 0)
    {
        negative = 1;
        num = -num;
    }

    ip = (int)num;
    dp = (int)((num - (float)ip) * 100.0f);

    if (ip == 0)
    {
        int_part[0] = '0';
        i = 1;
    }
    else
    {
        ii = ip;
        while (ii > 0)
        {
            int_part[i++] = (char)('0' + (ii % 10));
            ii /= 10;
        }
    }

    pos = 0;
    if (negative)
        buf[pos++] = '-';

    for (j = i - 1; j >= 0; j--)
        buf[pos++] = int_part[j];

    buf[pos++] = '.';
    buf[pos++] = (char)('0' + (dp / 10));
    buf[pos++] = (char)('0' + (dp % 10));
    buf[pos] = 0;
}

void unit_change_bits(int bits_number, int which, char *buf)
{
    float val;
    char bits_str[16];
    char bytes_str[16];
    char kb_str[16];

    val = (float)bits_number;
    float_to_str(val, bits_str);

    val = (float)bits_number / 8.0f;
    float_to_str(val, bytes_str);

    val = ((float)bits_number / 8.0f) / 1024.0f;
    float_to_str(val, kb_str);

    if (which == 0)
        strcpy(buf, bits_str);
    else if (which == 1)
        strcpy(buf, bytes_str);
    else if (which == 2)
        strcpy(buf, kb_str);
}

void kernel_main(void)
{
    unsigned char scancode;
    char key;
    int i;
    int save_loaded;

    /* Initialize file system */
    for (i = 0; i < MAX_FILES; i++)
    {
        fs[i].used = 0;
    }

    /* Try to load saved filesystem from floppy */
    save_loaded = fs_load_from_disk();

    clear_screen();
    print("Welcome to UseDOS!\n");
    print("C kernel loaded successfully.\n");
    print("Drive A: 8MB floppy disk\n");
    if (save_loaded == 0)
    {
        print("Loaded saved filesystem.\n");
    }
    print("Type 'help' for commands.\n");
    print_prompt();

    /* Save prompt position */
    prompt_x = cursor_x;
    prompt_y = cursor_y;
    cmd_len = 0;

    while (1)
    {
        scancode = get_scancode();
        if (scancode == 0)
            continue;

        key = scancode_to_ascii(scancode);
        if (key == '\n')
        {
            putchar('\n');
            process_command();
            cmd_len = 0;
            print_prompt();
            prompt_x = cursor_x;
            prompt_y = cursor_y;
        }
        else if (key == '\b')
        {
            if (cmd_len > 0)
            {
                cmd_len--;
                cmd_buffer[cmd_len] = 0;
                putchar('\b');
            }
        }
        else if (key != 0 && cmd_len < 255)
        {
            cmd_buffer[cmd_len] = key;
            cmd_len++;
            putchar(key);
        }
    }
}
