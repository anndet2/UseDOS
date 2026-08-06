/* feditor - Simple text editor for UseDOS */
#include "apps.h"

/* Key codes from kernel */
#define KEY_UP 0x100
#define KEY_DOWN 0x101
#define KEY_LEFT 0x102
#define KEY_RIGHT 0x103
#define KEY_HOME 0x104
#define KEY_END 0x105

/* F1 scancode */
#define KEY_F1 0x3B

/* Editor configuration */
#define MAX_LINES 100
#define LINE_WIDTH 70
#define LINE_NUM_WIDTH 4
#define EDIT_START_ROW 1 /* Row 0 is status bar */
#define VISIBLE_ROWS 24  /* Rows 1-24 for editing */

/* Editor state */
static char lines[MAX_LINES][LINE_WIDTH];
static int line_lengths[MAX_LINES];
static int total_lines = 1;
static int cursor_line = 0;
static int cursor_col = 0;
static int edit_mode = 1; /* 1 = edit, 0 = exit */
static char status_msg[64];

/* External functions */
extern int get_key(void);
extern char scancode_to_ascii(unsigned char scancode);
extern void set_cursor(int x, int y);
extern void clear_screen(void);
extern int fs_write(const char *name, const char *content, int length);

/* Convert integer to string */
static void int_to_str(int num, char *buf)
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

/* Draw status bar at top of screen */
static void draw_status_bar(void)
{
    volatile unsigned short *vga = (volatile unsigned short *)0xb8000;
    char line_str[8];
    char col_str[8];
    char bar[80];
    int i;

    /* Build status bar content */
    /* " feditor  F1:Save  ESC:Exit  Ln:X,Y " */
    for (i = 0; i < 80; i++)
        bar[i] = ' ';

    /* Title */
    bar[1] = 'f';
    bar[2] = 'e';
    bar[3] = 'd';
    bar[4] = 'i';
    bar[5] = 't';
    bar[6] = 'o';
    bar[7] = 'r';

    /* F1:Save */
    bar[11] = 'F';
    bar[12] = '1';
    bar[13] = ':';
    bar[14] = 'S';
    bar[15] = 'a';
    bar[16] = 'v';
    bar[17] = 'e';

    /* ESC:Exit */
    bar[21] = 'E';
    bar[22] = 'S';
    bar[23] = 'C';
    bar[24] = ':';
    bar[25] = 'E';
    bar[26] = 'x';
    bar[27] = 'i';
    bar[28] = 't';

    /* Line info */
    bar[32] = 'L';
    bar[33] = 'n';
    bar[34] = ':';
    int_to_str(cursor_line + 1, line_str);
    int_to_str(cursor_col + 1, col_str);
    {
        int pos = 35;
        i = 0;
        while (line_str[i])
            bar[pos++] = line_str[i++];
        bar[pos++] = ',';
        i = 0;
        while (col_str[i])
            bar[pos++] = col_str[i++];
    }

    /* Draw status bar with white-on-blue attribute (0x1F) */
    for (i = 0; i < 80; i++)
    {
        vga[i] = (0x1F << 8) | bar[i];
    }
}

/* Draw a single line with line number */
static void draw_line(int line_idx)
{
    char num_str[8];
    int i;
    volatile unsigned short *vga = (volatile unsigned short *)0xb8000;
    int row = line_idx + EDIT_START_ROW;
    int col = 0;

    /* Draw line number */
    int_to_str(line_idx + 1, num_str);

    /* Right-align line number */
    int len = 0;
    while (num_str[len])
        len++;

    /* Pad with spaces */
    for (i = 0; i < LINE_NUM_WIDTH - len - 1; i++)
    {
        vga[row * 80 + col] = (0x07 << 8) | ' ';
        col++;
    }

    /* Draw number */
    for (i = 0; num_str[i]; i++)
    {
        vga[row * 80 + col] = (0x07 << 8) | num_str[i];
        col++;
    }

    /* Separator */
    vga[row * 80 + col] = (0x07 << 8) | '|';
    col++;

    /* Draw line content */
    for (i = 0; i < LINE_WIDTH && i < line_lengths[line_idx]; i++)
    {
        vga[row * 80 + col] = (0x07 << 8) | lines[line_idx][i];
        col++;
    }

    /* Clear rest of line */
    for (i = col; i < 80; i++)
    {
        vga[row * 80 + i] = (0x07 << 8) | ' ';
    }
}

/* Redraw all lines */
static void redraw_all(void)
{
    int i;
    draw_status_bar();
    for (i = 0; i < total_lines && i < VISIBLE_ROWS; i++)
    {
        draw_line(i);
    }

    /* Clear remaining lines */
    for (i = total_lines; i < VISIBLE_ROWS; i++)
    {
        int j;
        volatile unsigned short *vga = (volatile unsigned short *)0xb8000;
        int row = i + EDIT_START_ROW;
        for (j = 0; j < 80; j++)
        {
            vga[row * 80 + j] = (0x07 << 8) | ' ';
        }
    }
}

/* Update cursor position on screen */
static void update_cursor(void)
{
    int screen_x = LINE_NUM_WIDTH + 1 + cursor_col;
    int screen_y = cursor_line + EDIT_START_ROW;
    set_cursor(screen_x, screen_y);
}

/* Show message at bottom line */
static void show_message(const char *msg)
{
    volatile unsigned short *vga = (volatile unsigned short *)0xb8000;
    int row = 24; /* Last usable line */
    int i;

    /* Clear the line first */
    for (i = 0; i < 80; i++)
    {
        vga[row * 80 + i] = (0x07 << 8) | ' ';
    }

    /* Draw message */
    for (i = 0; msg[i] && i < 80; i++)
    {
        vga[row * 80 + i] = (0x0A << 8) | msg[i]; /* Green text */
    }
}

/* Draw input prompt at bottom line and return input string */
static int get_input(const char *prompt, char *buf, int max_len)
{
    int len = 0;
    int key;
    char c;
    volatile unsigned short *vga = (volatile unsigned short *)0xb8000;
    int row = 24;
    int i;
    int prompt_len = 0;

    /* Clear bottom line */
    for (i = 0; i < 80; i++)
        vga[row * 80 + i] = (0x07 << 8) | ' ';

    /* Draw prompt */
    while (prompt[prompt_len] && prompt_len < 70)
    {
        vga[row * 80 + prompt_len] = (0x0B << 8) | prompt[prompt_len];
        prompt_len++;
    }

    buf[0] = 0;
    set_cursor(prompt_len, row);

    while (1)
    {
        key = get_key();
        if (key == 0)
            continue;

        /* ESC - cancel */
        if (key == 1)
        {
            return -1;
        }

        /* Enter - confirm */
        if (key == 0x1C)
        {
            buf[len] = 0;
            return len;
        }

        /* Backspace */
        if (key == 0x0E)
        {
            if (len > 0)
            {
                len--;
                vga[row * 80 + prompt_len + len] = (0x07 << 8) | ' ';
                set_cursor(prompt_len + len, row);
            }
            continue;
        }

        /* Regular character */
        c = scancode_to_ascii((unsigned char)key);
        if (c >= 32 && c < 127 && len < max_len - 1)
        {
            buf[len] = c;
            vga[row * 80 + prompt_len + len] = (0x07 << 8) | c;
            len++;
            set_cursor(prompt_len + len, row);
        }
    }
}

/* Save file dialog */
static void save_file(void)
{
    char filename[32];
    char content[FILE_CONTENT_SIZE];
    int content_len = 0;
    int i, j;
    int ret;

    /* Get filename from user */
    ret = get_input("Save as: ", filename, 32);
    if (ret < 0 || ret == 0)
    {
        show_message("Save cancelled.");
        return;
    }

    /* Check filename has extension */
    {
        int has_dot = 0;
        for (i = 0; filename[i]; i++)
        {
            if (filename[i] == '.')
                has_dot = 1;
        }
        if (!has_dot)
        {
            show_message("Error: Filename must have extension (e.g. .txt)");
            return;
        }
    }

    /* Build content string from editor lines */
    for (i = 0; i < total_lines && content_len < FILE_CONTENT_SIZE - 2; i++)
    {
        for (j = 0; j < line_lengths[i] && content_len < FILE_CONTENT_SIZE - 2; j++)
        {
            content[content_len++] = lines[i][j];
        }
        /* Add newline between lines (except possibly after last line) */
        if (i < total_lines - 1 && content_len < FILE_CONTENT_SIZE - 1)
        {
            content[content_len++] = '\n';
        }
    }
    content[content_len] = 0;

    /* Write to file system */
    ret = fs_write(filename, content, content_len);
    if (ret == 0)
    {
        /* Build success message */
        char msg[64];
        int pos = 0;
        const char *saved = "Saved: ";
        const char *bytes = " bytes";

        for (i = 0; saved[i]; i++)
            msg[pos++] = saved[i];
        for (i = 0; filename[i] && pos < 50; i++)
            msg[pos++] = filename[i];
        msg[pos++] = ' ';
        msg[pos++] = '(';
        {
            char num_str[8];
            int_to_str(content_len, num_str);
            for (i = 0; num_str[i] && pos < 60; i++)
                msg[pos++] = num_str[i];
        }
        msg[pos++] = ')';
        msg[pos] = 0;
        show_message(msg);
    }
    else
    {
        show_message("Error: Could not save file (disk full?)");
    }
}

/* Handle character insertion */
static void insert_char(char c)
{
    int i;

    if (c == '\n' || c == '\r')
    {
        /* Enter: split line and create new line */
        if (total_lines >= MAX_LINES)
            return;

        /* Move lines down */
        for (i = total_lines; i > cursor_line + 1; i--)
        {
            int j;
            for (j = 0; j < LINE_WIDTH; j++)
            {
                lines[i][j] = lines[i - 1][j];
            }
            line_lengths[i] = line_lengths[i - 1];
        }

        /* Split current line */
        {
            int split_len = line_lengths[cursor_line] - cursor_col;
            for (i = 0; i < split_len && i < LINE_WIDTH; i++)
            {
                lines[cursor_line + 1][i] = lines[cursor_line][cursor_col + i];
            }
            line_lengths[cursor_line + 1] = split_len > 0 ? split_len : 0;
            line_lengths[cursor_line] = cursor_col;
        }

        total_lines++;
        cursor_line++;
        cursor_col = 0;
    }
    else if (c == '\b')
    {
        /* Backspace */
        if (cursor_col > 0)
        {
            /* Delete character in current line */
            for (i = cursor_col - 1; i < line_lengths[cursor_line] - 1; i++)
            {
                lines[cursor_line][i] = lines[cursor_line][i + 1];
            }
            line_lengths[cursor_line]--;
            cursor_col--;
        }
        else if (cursor_line > 0)
        {
            /* Merge with previous line */
            int prev_len = line_lengths[cursor_line - 1];
            int cur_len = line_lengths[cursor_line];

            /* Move content from current line to previous */
            for (i = 0; i < cur_len && prev_len + i < LINE_WIDTH; i++)
            {
                lines[cursor_line - 1][prev_len + i] = lines[cursor_line][i];
            }
            line_lengths[cursor_line - 1] = prev_len + cur_len;
            if (line_lengths[cursor_line - 1] > LINE_WIDTH)
                line_lengths[cursor_line - 1] = LINE_WIDTH;

            cursor_col = prev_len;
            cursor_line--;

            /* Move lines up */
            for (i = cursor_line + 1; i < total_lines - 1; i++)
            {
                int j;
                for (j = 0; j < LINE_WIDTH; j++)
                {
                    lines[i][j] = lines[i + 1][j];
                }
                line_lengths[i] = line_lengths[i + 1];
            }
            total_lines--;
        }
    }
    else if (c >= 32 && c < 127)
    {
        /* Insert printable character */
        if (line_lengths[cursor_line] >= LINE_WIDTH)
            return;

        /* Shift characters right */
        for (i = line_lengths[cursor_line]; i > cursor_col; i--)
        {
            lines[cursor_line][i] = lines[cursor_line][i - 1];
        }

        lines[cursor_line][cursor_col] = c;
        line_lengths[cursor_line]++;
        cursor_col++;
    }
}

/* Handle arrow key movement */
static void handle_arrow(int key)
{
    switch (key)
    {
    case KEY_UP:
        if (cursor_line > 0)
        {
            cursor_line--;
            if (cursor_col > line_lengths[cursor_line])
                cursor_col = line_lengths[cursor_line];
        }
        break;
    case KEY_DOWN:
        if (cursor_line < total_lines - 1)
        {
            cursor_line++;
            if (cursor_col > line_lengths[cursor_line])
                cursor_col = line_lengths[cursor_line];
        }
        break;
    case KEY_LEFT:
        if (cursor_col > 0)
        {
            cursor_col--;
        }
        else if (cursor_line > 0)
        {
            cursor_line--;
            cursor_col = line_lengths[cursor_line];
        }
        break;
    case KEY_RIGHT:
        if (cursor_col < line_lengths[cursor_line])
        {
            cursor_col++;
        }
        else if (cursor_line < total_lines - 1)
        {
            cursor_line++;
            cursor_col = 0;
        }
        break;
    case KEY_HOME:
        cursor_col = 0;
        break;
    case KEY_END:
        cursor_col = line_lengths[cursor_line];
        break;
    }
}

/* Main editor loop */
int feditor_main(int argc, char *argv[])
{
    int key;
    char c;
    int i;

    /* Initialize editor state */
    for (i = 0; i < MAX_LINES; i++)
    {
        line_lengths[i] = 0;
        lines[i][0] = 0;
    }
    total_lines = 1;
    cursor_line = 0;
    cursor_col = 0;
    edit_mode = 1;
    status_msg[0] = 0;

    /* Clear screen and draw initial state */
    clear_screen();
    redraw_all();
    update_cursor();

    /* Editor main loop */
    while (edit_mode)
    {
        key = get_key();

        if (key == 0)
            continue;

        /* ESC key (scancode 1) - exit editor */
        if (key == 1)
        {
            edit_mode = 0;
            break;
        }

        /* F1 key - save file */
        if (key == KEY_F1)
        {
            save_file();
            /* Redraw editor after save dialog */
            redraw_all();
            update_cursor();
            /* Keep the bottom message visible for a moment */
            continue;
        }

        /* TAB key (scancode 0x0F) - insert 4 spaces */
        if (key == 0x0F)
        {
            int spaces_to_insert = 4 - (cursor_col % 4);
            int s;
            for (s = 0; s < spaces_to_insert; s++)
            {
                insert_char(' ');
            }
            redraw_all();
            update_cursor();
            continue;
        }

        /* Arrow keys */
        if (key >= KEY_UP && key <= KEY_END)
        {
            handle_arrow(key);
            redraw_all();
            update_cursor();
            continue;
        }

        /* Regular character */
        c = scancode_to_ascii((unsigned char)key);
        if (c != 0)
        {
            insert_char(c);
            redraw_all();
            update_cursor();
        }
    }

    /* Exit: clear screen and return */
    clear_screen();
    print("Exited feditor.\n");

    return 0;
}
