/* apps.h - Application framework for UseDOS */
#ifndef APPS_H
#define APPS_H

/* Application entry point type */
typedef int (*app_main_t)(int argc, char *argv[]);

/* Application descriptor structure */
typedef struct
{
    const char *name; /* Application name (used with run command) */
    app_main_t main;  /* Entry point function */
    const char *note; /* Note for app */
} app_t;

/* Define fs_entry_t here so it's complete */
#define FILE_CONTENT_SIZE 512
typedef struct fs_entry_s
{
    char name[32];
    int is_directory;
    int used;
    char content[FILE_CONTENT_SIZE];
    int content_length;
} fs_entry_t;

/* Kernel functions available to applications */
extern int fs_find(const char *name);
extern int fs_count;
extern fs_entry_t fs[];
extern void print(const char *str);
extern void putchar(char c);
extern void clear_screen(void);
extern void set_cursor(int x, int y);
extern int get_key(void);
extern char scancode_to_ascii(unsigned char scancode);
extern int fs_write(const char *name, const char *content, int length);

#endif /* APPS_H */
