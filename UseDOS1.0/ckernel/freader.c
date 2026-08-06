/*
freader.c is abandoned you can use feditor.c instead.
you can only read files in this usedosapp.

freader - File reader application for UseDOS
*/
#include "apps.h"

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

/* Add line numbers to content */
static void add_line_numbers(const char *content)
{
    int line_num = 1;
    char line_str[16];
    int i;

    int_to_str(line_num, line_str);
    print(line_str);
    print(" ");

    for (i = 0; content[i]; i++)
    {
        putchar(content[i]);
        if (content[i] == '\n')
        {
            line_num++;
            int_to_str(line_num, line_str);
            print(line_str);
            print(" ");
        }
    }
}

/* Main entry point for freader */
int freader_main(int argc, char *argv[])
{
    char filename[64];
    int idx;
    int i, j;
    int pos;

    if (argc < 2)
    {
        print("Usage: freader <filename>\n");
        return 1;
    }

    /* Concatenate all arguments from argv[1] as filename */
    pos = 0;
    for (i = 1; i < argc && pos < 63; i++)
    {
        if (i > 1 && pos < 62)
            filename[pos++] = ' ';
        for (j = 0; argv[i][j] && pos < 63; j++)
        {
            filename[pos++] = argv[i][j];
        }
    }
    filename[pos] = 0;

    /* Find the file in our virtual file system */
    idx = fs_find(filename);
    if (idx < 0)
    {
        print("Error: File '");
        print(filename);
        print("' not found\n");
        return 1;
    }

    if (fs[idx].is_directory)
    {
        print("Error: '");
        print(filename);
        print("' is a directory, not a file\n");
        return 1;
    }

    /* Read file content and display with line numbers */
    print("File: ");
    print(filename);
    print("\n");
    print("---\n");

    /* Display file content with line numbers */
    add_line_numbers(fs[idx].content);
    print("\n---\n");

    return 0;
}

/* Application is registered via the static table in kernel.c */
