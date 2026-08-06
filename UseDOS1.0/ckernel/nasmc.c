/* nasmc.c - Simple NASM-like assembler for UseDOS */
#include "apps.h"

#define MAX_CODE_SIZE 512
#define MAX_LABELS 32
#define MAX_LINE_LEN 128
#define MAX_TOKENS 16
#define TOKEN_LEN 32

/* Label table */
static char label_names[MAX_LABELS][32];
static int label_addrs[MAX_LABELS];
static int label_count;

/* Unresolved jump table (for pass 2) */
static char jump_labels[MAX_LABELS][32];
static int jump_offsets[MAX_LABELS];     /* offset in output where rel value goes */
static int jump_is_short[MAX_LABELS];    /* 1 = short (1 byte), 0 = near (2 bytes) */
static int jump_instr_start[MAX_LABELS]; /* start of instruction in output */
static int jump_count;

/* Output buffer */
static unsigned char output[MAX_CODE_SIZE];
static int output_len;

/* Error flag */
static int has_error;

/* External functions */
extern int fs_find(const char *name);
extern int fs_write(const char *name, const char *content, int length);

/* String helpers */
static int str_eq(const char *a, const char *b)
{
    int i;
    for (i = 0; a[i] && b[i]; i++)
    {
        if (a[i] != b[i])
            return 0;
    }
    return a[i] == b[i];
}

static void str_copy(char *dst, const char *src)
{
    int i;
    for (i = 0; src[i]; i++)
        dst[i] = src[i];
    dst[i] = 0;
}

static int str_len(const char *s)
{
    int i;
    for (i = 0; s[i]; i++)
        ;
    return i;
}

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
        buf[j] = temp[i - 1 - j];
    buf[i] = 0;
}

/* Parse hex string to integer */
static int parse_hex(const char *s, int *val)
{
    int result = 0;
    int i;

    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2;
    else if (s[0] == '0' && (s[1] == 'h' || s[1] == 'H'))
        s += 2;

    if (!s[0])
        return 0;

    for (i = 0; s[i]; i++)
    {
        if (s[i] >= '0' && s[i] <= '9')
            result = result * 16 + (s[i] - '0');
        else if (s[i] >= 'a' && s[i] <= 'f')
            result = result * 16 + (s[i] - 'a' + 10);
        else if (s[i] >= 'A' && s[i] <= 'F')
            result = result * 16 + (s[i] - 'A' + 10);
        else
            return 0;
    }
    *val = result;
    return 1;
}

/* Parse number (hex with 0x, decimal, or hex with h suffix) */
static int parse_num(const char *s, int *val)
{
    int result = 0;
    int i;
    int neg = 0;
    int len;

    if (s[0] == '-')
    {
        neg = 1;
        s++;
    }

    /* Hex with 0x prefix */
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        return parse_hex(s, val);

    /* Check for h suffix (e.g. 10h) */
    len = str_len(s);
    if (len > 1 && (s[len - 1] == 'h' || s[len - 1] == 'H'))
    {
        return parse_hex(s, val);
    }

    /* Decimal */
    for (i = 0; s[i]; i++)
    {
        if (s[i] >= '0' && s[i] <= '9')
            result = result * 10 + (s[i] - '0');
        else
            return 0;
    }
    *val = neg ? -result : result;
    return 1;
}

/* Register lookups */
static int reg16_lookup(const char *s)
{
    if (str_eq(s, "ax"))
        return 0;
    if (str_eq(s, "cx"))
        return 1;
    if (str_eq(s, "dx"))
        return 2;
    if (str_eq(s, "bx"))
        return 3;
    if (str_eq(s, "sp"))
        return 4;
    if (str_eq(s, "bp"))
        return 5;
    if (str_eq(s, "si"))
        return 6;
    if (str_eq(s, "di"))
        return 7;
    return -1;
}

static int reg8_lookup(const char *s)
{
    if (str_eq(s, "al"))
        return 0;
    if (str_eq(s, "cl"))
        return 1;
    if (str_eq(s, "dl"))
        return 2;
    if (str_eq(s, "bl"))
        return 3;
    if (str_eq(s, "ah"))
        return 4;
    if (str_eq(s, "ch"))
        return 5;
    if (str_eq(s, "dh"))
        return 6;
    if (str_eq(s, "bh"))
        return 7;
    return -1;
}

/* Emit a byte to output */
static void emit_byte(unsigned char b)
{
    if (output_len < MAX_CODE_SIZE)
    {
        output[output_len] = b;
        output_len++;
    }
}

static void emit_word(int w)
{
    emit_byte((unsigned char)(w & 0xFF));
    emit_byte((unsigned char)((w >> 8) & 0xFF));
}

/* Tokenize a line into tokens */
static int tokenize(char *line, char tokens[][TOKEN_LEN], int max_tokens)
{
    int count = 0;
    int i = 0;
    int ti;
    int in_token = 0;

    ti = 0;
    while (line[i] && count < max_tokens)
    {
        char c = line[i];

        /* Comment */
        if (c == ';')
            break;

        /* Whitespace */
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
        {
            if (in_token)
            {
                tokens[count][ti] = 0;
                count++;
                ti = 0;
                in_token = 0;
            }
        }
        /* Comma separates operands */
        else if (c == ',')
        {
            if (in_token)
            {
                tokens[count][ti] = 0;
                count++;
                ti = 0;
                in_token = 0;
            }
            /* Add comma as its own token */
            tokens[count][0] = ',';
            tokens[count][1] = 0;
            count++;
        }
        /* Colon (label) */
        else if (c == ':')
        {
            if (in_token)
            {
                tokens[count][ti] = 0;
                count++;
                ti = 0;
                in_token = 0;
            }
            tokens[count][0] = ':';
            tokens[count][1] = 0;
            count++;
        }
        else
        {
            /* Convert to lowercase for mnemonics/registers */
            if (c >= 'A' && c <= 'Z')
                c = c + 32;
            tokens[count][ti] = c;
            ti++;
            in_token = 1;
        }

        i++;
    }

    if (in_token && count < max_tokens)
    {
        tokens[count][ti] = 0;
        count++;
    }

    return count;
}

/* Check if token is a number */
static int is_number(const char *s)
{
    int val;
    return parse_num(s, &val);
}

/* Check if string ends with suffix */
static int ends_with(const char *s, const char *suffix)
{
    int slen = str_len(s);
    int suflen = str_len(suffix);
    int i;

    if (slen < suflen)
        return 0;

    for (i = 0; i < suflen; i++)
    {
        if (s[slen - suflen + i] != suffix[i])
            return 0;
    }
    return 1;
}

/* Find label by name, returns address or -1 */
static int find_label(const char *name)
{
    int i;
    for (i = 0; i < label_count; i++)
    {
        if (str_eq(label_names[i], name))
            return label_addrs[i];
    }
    return -1;
}

/* Add a label */
static void add_label(const char *name, int addr)
{
    if (label_count < MAX_LABELS)
    {
        str_copy(label_names[label_count], name);
        label_addrs[label_count] = addr;
        label_count++;
    }
}

/* Record a jump to resolve later */
static void record_jump(const char *label, int offset_pos, int is_short, int instr_start)
{
    if (jump_count < MAX_LABELS)
    {
        str_copy(jump_labels[jump_count], label);
        jump_offsets[jump_count] = offset_pos;
        jump_is_short[jump_count] = is_short;
        jump_instr_start[jump_count] = instr_start;
        jump_count++;
    }
}

/* Calculate instruction size for pass 1 */
static int instr_size(char tokens[][TOKEN_LEN], int ntokens)
{
    char *mnemonic;
    int r1, r2;
    int val;

    if (ntokens == 0)
        return 0;

    mnemonic = tokens[0];

    /* 1-byte instructions */
    if (str_eq(mnemonic, "nop"))
        return 1;
    if (str_eq(mnemonic, "hlt"))
        return 1;
    if (str_eq(mnemonic, "ret"))
        return 1;
    if (str_eq(mnemonic, "cli"))
        return 1;
    if (str_eq(mnemonic, "sti"))
        return 1;
    if (str_eq(mnemonic, "cld"))
        return 1;
    if (str_eq(mnemonic, "std"))
        return 1;
    if (str_eq(mnemonic, "leave"))
        return 1;
    if (str_eq(mnemonic, "pushf"))
        return 1;
    if (str_eq(mnemonic, "popf"))
        return 1;

    /* push/pop/inc/dec reg16 */
    if (ntokens >= 2 &&
        (str_eq(mnemonic, "push") || str_eq(mnemonic, "pop") ||
         str_eq(mnemonic, "inc") || str_eq(mnemonic, "dec")))
    {
        if (reg16_lookup(tokens[1]) >= 0)
            return 1;
        if (reg8_lookup(tokens[1]) >= 0)
            return 2; /* inc/dec reg8 needs REX prefix in 32-bit... actually 0xFE /0 */
        return 1;
    }

    /* int imm8 */
    if (str_eq(mnemonic, "int"))
        return 2;

    /* mov reg16, imm16 */
    if (str_eq(mnemonic, "mov") && ntokens >= 4)
    {
        r1 = reg16_lookup(tokens[1]);
        if (r1 >= 0 && tokens[2][0] == ',' && is_number(tokens[3]))
            return 3; /* B8+r + imm16 */

        r1 = reg8_lookup(tokens[1]);
        if (r1 >= 0 && tokens[2][0] == ',' && is_number(tokens[3]))
            return 2; /* B0+r + imm8 */

        /* mov reg, reg */
        r1 = reg16_lookup(tokens[1]);
        r2 = reg16_lookup(tokens[3]);
        if (r1 >= 0 && tokens[2][0] == ',' && r2 >= 0)
            return 2;

        r1 = reg8_lookup(tokens[1]);
        r2 = reg8_lookup(tokens[3]);
        if (r1 >= 0 && tokens[2][0] == ',' && r2 >= 0)
            return 2;

        return 3; /* default guess */
    }

    /* add/sub/cmp/and/or reg, imm */
    if (ntokens >= 4 &&
        (str_eq(mnemonic, "add") || str_eq(mnemonic, "sub") ||
         str_eq(mnemonic, "cmp") || str_eq(mnemonic, "and") ||
         str_eq(mnemonic, "or")) &&
        reg16_lookup(tokens[1]) >= 0 &&
        tokens[2][0] == ',' &&
        is_number(tokens[3]))
    {
        return 3; /* 81 /r imm16 */
    }

    /* xor reg, reg */
    if (str_eq(mnemonic, "xor") && ntokens >= 4 &&
        reg16_lookup(tokens[1]) >= 0 &&
        tokens[2][0] == ',' &&
        reg16_lookup(tokens[3]) >= 0)
    {
        return 2;
    }

    /* add/sub/cmp/and/or reg, reg */
    if (ntokens >= 4 &&
        (str_eq(mnemonic, "add") || str_eq(mnemonic, "sub") ||
         str_eq(mnemonic, "cmp") || str_eq(mnemonic, "and") ||
         str_eq(mnemonic, "or") || str_eq(mnemonic, "mov")) &&
        reg16_lookup(tokens[1]) >= 0 &&
        tokens[2][0] == ',' &&
        reg16_lookup(tokens[3]) >= 0)
    {
        return 2;
    }

    /* in/out */
    if (str_eq(mnemonic, "in") || str_eq(mnemonic, "out"))
        return 2;

    /* Jumps - default to short jump (2 bytes) */
    if (str_eq(mnemonic, "jmp") || str_eq(mnemonic, "call"))
    {
        /* jmp short = 2, jmp near = 3, call = 3 */
        if (ends_with(tokens[1], ":") || str_eq(tokens[1], "short"))
            return 2;
        if (str_eq(mnemonic, "call"))
            return 3;
        return 2; /* default short */
    }

    /* Conditional jumps - short (2 bytes) */
    if (str_eq(mnemonic, "je") || str_eq(mnemonic, "jz") ||
        str_eq(mnemonic, "jne") || str_eq(mnemonic, "jnz") ||
        str_eq(mnemonic, "jg") || str_eq(mnemonic, "jge") ||
        str_eq(mnemonic, "jl") || str_eq(mnemonic, "jle") ||
        str_eq(mnemonic, "ja") || str_eq(mnemonic, "jae") ||
        str_eq(mnemonic, "jb") || str_eq(mnemonic, "jbe") ||
        str_eq(mnemonic, "jc") || str_eq(mnemonic, "jnc") ||
        str_eq(mnemonic, "jo") || str_eq(mnemonic, "jno") ||
        str_eq(mnemonic, "js") || str_eq(mnemonic, "jns"))
    {
        return 2;
    }

    /* lodsb, lodsw, stosb, stosw, movsb, movsw, rep */
    if (str_eq(mnemonic, "lodsb") || str_eq(mnemonic, "lodsw") ||
        str_eq(mnemonic, "stosb") || str_eq(mnemonic, "stosw") ||
        str_eq(mnemonic, "movsb") || str_eq(mnemonic, "movsw") ||
        str_eq(mnemonic, "rep") || str_eq(mnemonic, "clc") ||
        str_eq(mnemonic, "stc") || str_eq(mnemonic, "cmc"))
    {
        return 1;
    }

    /* out dx, al etc */
    if (str_eq(mnemonic, "out") && ntokens >= 4)
        return 1;

    /* db - define byte */
    if (str_eq(mnemonic, "db"))
    {
        /* Count operands (each comma-separated value = 1 byte) */
        int count = 0;
        int i;
        for (i = 1; i < ntokens; i++)
        {
            if (tokens[i][0] != ',')
                count++;
        }
        return count;
    }

    return 0; /* Unknown */
}

/* Encode instruction in pass 2 */
static void encode_instr(char tokens[][TOKEN_LEN], int ntokens)
{
    char *mnemonic;
    int r1, r2;
    int val;

    if (ntokens == 0)
        return;

    mnemonic = tokens[0];

    /* Simple 1-byte instructions */
    if (str_eq(mnemonic, "nop"))
    {
        emit_byte(0x90);
        return;
    }
    if (str_eq(mnemonic, "hlt"))
    {
        emit_byte(0xF4);
        return;
    }
    if (str_eq(mnemonic, "ret"))
    {
        emit_byte(0xC3);
        return;
    }
    if (str_eq(mnemonic, "cli"))
    {
        emit_byte(0xFA);
        return;
    }
    if (str_eq(mnemonic, "sti"))
    {
        emit_byte(0xFB);
        return;
    }
    if (str_eq(mnemonic, "cld"))
    {
        emit_byte(0xFC);
        return;
    }
    if (str_eq(mnemonic, "std"))
    {
        emit_byte(0xFD);
        return;
    }
    if (str_eq(mnemonic, "leave"))
    {
        emit_byte(0xC9);
        return;
    }
    if (str_eq(mnemonic, "pushf"))
    {
        emit_byte(0x9C);
        return;
    }
    if (str_eq(mnemonic, "popf"))
    {
        emit_byte(0x9D);
        return;
    }
    if (str_eq(mnemonic, "clc"))
    {
        emit_byte(0xF8);
        return;
    }
    if (str_eq(mnemonic, "stc"))
    {
        emit_byte(0xF9);
        return;
    }
    if (str_eq(mnemonic, "cmc"))
    {
        emit_byte(0xF5);
        return;
    }
    if (str_eq(mnemonic, "lodsb"))
    {
        emit_byte(0xAC);
        return;
    }
    if (str_eq(mnemonic, "lodsw"))
    {
        emit_byte(0xAD);
        return;
    }
    if (str_eq(mnemonic, "stosb"))
    {
        emit_byte(0xAA);
        return;
    }
    if (str_eq(mnemonic, "stosw"))
    {
        emit_byte(0xAB);
        return;
    }
    if (str_eq(mnemonic, "movsb"))
    {
        emit_byte(0xA4);
        return;
    }
    if (str_eq(mnemonic, "movsw"))
    {
        emit_byte(0xA5);
        return;
    }
    if (str_eq(mnemonic, "rep"))
    {
        emit_byte(0xF3);
        /* Next token should be the instruction */
        if (ntokens >= 2)
        {
            char sub_tokens[1][TOKEN_LEN];
            str_copy(sub_tokens[0], tokens[1]);
            /* Emit the rep prefix, then the actual instruction follows */
            /* For simplicity, just emit rep and let next line handle it */
            /* Actually, "rep movsb" is on one line */
            if (str_eq(tokens[1], "movsb"))
                emit_byte(0xA4);
            else if (str_eq(tokens[1], "movsw"))
                emit_byte(0xA5);
            else if (str_eq(tokens[1], "stosb"))
                emit_byte(0xAA);
            else if (str_eq(tokens[1], "stosw"))
                emit_byte(0xAB);
            else if (str_eq(tokens[1], "lodsb"))
                emit_byte(0xAC);
            else if (str_eq(tokens[1], "lodsw"))
                emit_byte(0xAD);
        }
        return;
    }

    /* push/pop/inc/dec reg16 */
    if (str_eq(mnemonic, "push") && ntokens >= 2)
    {
        r1 = reg16_lookup(tokens[1]);
        if (r1 >= 0)
        {
            emit_byte(0x50 + r1);
            return;
        }
    }
    if (str_eq(mnemonic, "pop") && ntokens >= 2)
    {
        r1 = reg16_lookup(tokens[1]);
        if (r1 >= 0)
        {
            emit_byte(0x58 + r1);
            return;
        }
    }
    if (str_eq(mnemonic, "inc") && ntokens >= 2)
    {
        r1 = reg16_lookup(tokens[1]);
        if (r1 >= 0)
        {
            emit_byte(0x40 + r1);
            return;
        }
    }
    if (str_eq(mnemonic, "dec") && ntokens >= 2)
    {
        r1 = reg16_lookup(tokens[1]);
        if (r1 >= 0)
        {
            emit_byte(0x48 + r1);
            return;
        }
    }

    /* int imm8 */
    if (str_eq(mnemonic, "int") && ntokens >= 2)
    {
        if (parse_num(tokens[1], &val))
        {
            emit_byte(0xCD);
            emit_byte((unsigned char)val);
            return;
        }
    }

    /* mov reg16, imm16 */
    if (str_eq(mnemonic, "mov") && ntokens >= 4)
    {
        r1 = reg16_lookup(tokens[1]);
        if (r1 >= 0 && tokens[2][0] == ',' && parse_num(tokens[3], &val))
        {
            emit_byte(0xB8 + r1);
            emit_word(val);
            return;
        }

        /* mov reg8, imm8 */
        r1 = reg8_lookup(tokens[1]);
        if (r1 >= 0 && tokens[2][0] == ',' && parse_num(tokens[3], &val))
        {
            emit_byte(0xB0 + r1);
            emit_byte((unsigned char)val);
            return;
        }

        /* mov reg16, reg16: 89 /r */
        r1 = reg16_lookup(tokens[1]);
        r2 = reg16_lookup(tokens[3]);
        if (r1 >= 0 && tokens[2][0] == ',' && r2 >= 0)
        {
            emit_byte(0x89);
            emit_byte(0xC0 + (r2 << 3) + r1);
            return;
        }

        /* mov reg8, reg8: 88 /r */
        r1 = reg8_lookup(tokens[1]);
        r2 = reg8_lookup(tokens[3]);
        if (r1 >= 0 && tokens[2][0] == ',' && r2 >= 0)
        {
            emit_byte(0x88);
            emit_byte(0xC0 + (r2 << 3) + r1);
            return;
        }
    }

    /* add/sub/cmp/and/or/xor reg, reg or reg, imm */
    if (ntokens >= 4 &&
        (str_eq(mnemonic, "add") || str_eq(mnemonic, "sub") ||
         str_eq(mnemonic, "cmp") || str_eq(mnemonic, "and") ||
         str_eq(mnemonic, "or") || str_eq(mnemonic, "xor")) &&
        reg16_lookup(tokens[1]) >= 0 &&
        tokens[2][0] == ',')
    {
        int opcode_rr;
        int opcode_ri;
        int sub_field;

        r1 = reg16_lookup(tokens[1]);

        if (str_eq(mnemonic, "add"))
        {
            opcode_rr = 0x01;
            sub_field = 0;
        }
        else if (str_eq(mnemonic, "sub"))
        {
            opcode_rr = 0x29;
            sub_field = 5;
        }
        else if (str_eq(mnemonic, "cmp"))
        {
            opcode_rr = 0x39;
            sub_field = 7;
        }
        else if (str_eq(mnemonic, "and"))
        {
            opcode_rr = 0x21;
            sub_field = 4;
        }
        else if (str_eq(mnemonic, "or"))
        {
            opcode_rr = 0x09;
            sub_field = 1;
        }
        else /* xor */
        {
            opcode_rr = 0x31;
            sub_field = 6;
        }

        /* reg, reg */
        r2 = reg16_lookup(tokens[3]);
        if (r2 >= 0)
        {
            emit_byte(opcode_rr);
            emit_byte(0xC0 + (r2 << 3) + r1);
            return;
        }

        /* reg, imm */
        if (parse_num(tokens[3], &val))
        {
            /* Use 81 /r form */
            emit_byte(0x81);
            emit_byte(0xC0 + (sub_field << 3) + r1);
            emit_word(val);
            return;
        }
    }

    /* in al, dx / in ax, dx / out dx, al / out dx, ax */
    if (str_eq(mnemonic, "in") && ntokens >= 4)
    {
        if (str_eq(tokens[1], "al") && str_eq(tokens[3], "dx"))
        {
            emit_byte(0xEC);
            return;
        }
        if (str_eq(tokens[1], "ax") && str_eq(tokens[3], "dx"))
        {
            emit_byte(0xED);
            return;
        }
        /* in al, imm8 */
        if (str_eq(tokens[1], "al") && parse_num(tokens[3], &val))
        {
            emit_byte(0xE4);
            emit_byte((unsigned char)val);
            return;
        }
    }
    if (str_eq(mnemonic, "out") && ntokens >= 4)
    {
        if (str_eq(tokens[1], "dx") && str_eq(tokens[3], "al"))
        {
            emit_byte(0xEE);
            return;
        }
        if (str_eq(tokens[1], "dx") && str_eq(tokens[3], "ax"))
        {
            emit_byte(0xEF);
            return;
        }
        /* out imm8, al */
        if (parse_num(tokens[1], &val) && str_eq(tokens[3], "al"))
        {
            emit_byte(0xE6);
            emit_byte((unsigned char)val);
            return;
        }
    }

    /* db - define byte */
    if (str_eq(mnemonic, "db"))
    {
        int i;
        for (i = 1; i < ntokens; i++)
        {
            if (tokens[i][0] != ',')
            {
                if (parse_num(tokens[i], &val))
                    emit_byte((unsigned char)val);
            }
        }
        return;
    }

    /* Jumps and calls */
    if (str_eq(mnemonic, "jmp"))
    {
        char label[32];
        int label_addr;
        int instr_start = output_len;

        /* Handle "jmp short label" */
        if (ntokens >= 3 && str_eq(tokens[1], "short"))
            str_copy(label, tokens[2]);
        else
            str_copy(label, tokens[1]);

        /* Remove trailing colon if present */
        {
            int llen = str_len(label);
            if (llen > 0 && label[llen - 1] == ':')
                label[llen - 1] = 0;
        }

        /* Direct number (absolute address) */
        if (parse_num(label, &val))
        {
            emit_byte(0xE9);
            emit_word(val);
            return;
        }

        /* Label reference - emit short jump, resolve later */
        emit_byte(0xEB);
        record_jump(label, output_len, 1, instr_start);
        emit_byte(0x00); /* placeholder */
        return;
    }

    if (str_eq(mnemonic, "call"))
    {
        char label[32];
        int instr_start = output_len;

        str_copy(label, tokens[1]);
        {
            int llen = str_len(label);
            if (llen > 0 && label[llen - 1] == ':')
                label[llen - 1] = 0;
        }

        if (parse_num(label, &val))
        {
            emit_byte(0xE8);
            emit_word(val);
            return;
        }

        /* Near call: E8 rel16 */
        emit_byte(0xE8);
        record_jump(label, output_len, 0, instr_start);
        emit_word(0x0000); /* placeholder */
        return;
    }

    /* Conditional jumps */
    {
        int cc = -1;
        if (str_eq(mnemonic, "je") || str_eq(mnemonic, "jz"))
            cc = 0x74;
        else if (str_eq(mnemonic, "jne") || str_eq(mnemonic, "jnz"))
            cc = 0x75;
        else if (str_eq(mnemonic, "jc"))
            cc = 0x72;
        else if (str_eq(mnemonic, "jnc"))
            cc = 0x73;
        else if (str_eq(mnemonic, "ja"))
            cc = 0x77;
        else if (str_eq(mnemonic, "jae"))
            cc = 0x73;
        else if (str_eq(mnemonic, "jb"))
            cc = 0x72;
        else if (str_eq(mnemonic, "jbe"))
            cc = 0x76;
        else if (str_eq(mnemonic, "jg"))
            cc = 0x7F;
        else if (str_eq(mnemonic, "jge"))
            cc = 0x7D;
        else if (str_eq(mnemonic, "jl"))
            cc = 0x7C;
        else if (str_eq(mnemonic, "jle"))
            cc = 0x7E;
        else if (str_eq(mnemonic, "js"))
            cc = 0x78;
        else if (str_eq(mnemonic, "jns"))
            cc = 0x79;
        else if (str_eq(mnemonic, "jo"))
            cc = 0x70;
        else if (str_eq(mnemonic, "jno"))
            cc = 0x71;

        if (cc > 0 && ntokens >= 2)
        {
            char label[32];
            int instr_start = output_len;

            str_copy(label, tokens[1]);
            {
                int llen = str_len(label);
                if (llen > 0 && label[llen - 1] == ':')
                    label[llen - 1] = 0;
            }

            emit_byte(cc);
            record_jump(label, output_len, 1, instr_start);
            emit_byte(0x00); /* placeholder */
            return;
        }
    }

    /* Unknown instruction - emit nop and warn */
    print("Warning: unknown instruction '");
    print(mnemonic);
    print("'\n");
    has_error = 1;
    emit_byte(0x90);
}

/* Parse content into lines and assemble */
static void assemble(const char *content)
{
    char line[MAX_LINE_LEN];
    char tokens[MAX_TOKENS][TOKEN_LEN];
    int line_pos = 0;
    int line_num = 0;
    int i;
    int content_idx;
    int pass;

    /* Two passes */
    for (pass = 1; pass <= 2; pass++)
    {
        content_idx = 0;
        output_len = 0;
        line_num = 0;

        if (pass == 1)
        {
            label_count = 0;
        }
        else
        {
            jump_count = 0;
        }

        while (content[content_idx])
        {
            /* Read one line */
            line_pos = 0;
            while (content[content_idx] && content[content_idx] != '\n' && line_pos < MAX_LINE_LEN - 1)
            {
                line[line_pos] = content[content_idx];
                line_pos++;
                content_idx++;
            }
            line[line_pos] = 0;
            if (content[content_idx] == '\n')
                content_idx++;
            line_num++;

            /* Tokenize */
            {
                int ntokens = tokenize(line, tokens, MAX_TOKENS);
                int tok_idx = 0;

                if (ntokens == 0)
                    continue;

                /* Check for label: "name :" or "name:" */
                if (ntokens >= 2 && tokens[1][0] == ':')
                {
                    if (pass == 1)
                    {
                        add_label(tokens[0], output_len);
                    }
                    tok_idx = 2; /* Skip label and colon */
                }
                /* Label without space: "label:" might be one token if no space */
                else if (ntokens >= 1)
                {
                    int tlen = str_len(tokens[0]);
                    if (tlen > 1 && tokens[0][tlen - 1] == ':')
                    {
                        tokens[0][tlen - 1] = 0;
                        if (pass == 1)
                        {
                            add_label(tokens[0], output_len);
                        }
                        tok_idx = 1;
                    }
                }

                if (tok_idx >= ntokens)
                    continue;

                /* Shift tokens to start from tok_idx */
                if (tok_idx > 0)
                {
                    int j;
                    for (j = 0; j < ntokens - tok_idx; j++)
                    {
                        str_copy(tokens[j], tokens[j + tok_idx]);
                    }
                    ntokens -= tok_idx;
                }

                if (ntokens == 0)
                    continue;

                if (pass == 1)
                {
                    /* Just calculate size */
                    output_len += instr_size(tokens, ntokens);
                }
                else
                {
                    /* Encode instruction */
                    encode_instr(tokens, ntokens);
                }
            }
        }
    }

    /* Resolve jumps */
    for (i = 0; i < jump_count; i++)
    {
        int target = find_label(jump_labels[i]);
        if (target < 0)
        {
            print("Error: undefined label '");
            print(jump_labels[i]);
            print("'\n");
            has_error = 1;
            continue;
        }

        if (jump_is_short[i])
        {
            int rel = target - (jump_instr_start[i] + 2);
            output[jump_offsets[i]] = (unsigned char)(rel & 0xFF);
        }
        else
        {
            int rel = target - (jump_instr_start[i] + 3);
            output[jump_offsets[i]] = (unsigned char)(rel & 0xFF);
            output[jump_offsets[i] + 1] = (unsigned char)((rel >> 8) & 0xFF);
        }
    }
}

/* Main entry point for nasm compiler */
int nasmc_main(int argc, char *argv[])
{
    char filename[64];
    int idx;
    int i, j, pos;
    char outname[64];
    int outpos;

    if (argc < 2)
    {
        print("Usage: nasm <source.asm>\n");
        print("Assembles x86 assembly source to binary.\n");
        return 1;
    }

    /* Concatenate args as filename */
    pos = 0;
    for (i = 1; i < argc && pos < 63; i++)
    {
        if (i > 1 && pos < 62)
            filename[pos++] = ' ';
        for (j = 0; argv[i][j] && pos < 63; j++)
            filename[pos++] = argv[i][j];
    }
    filename[pos] = 0;

    /* Find source file */
    idx = fs_find(filename);
    if (idx < 0)
    {
        print("Error: Source file '");
        print(filename);
        print("' not found\n");
        return 1;
    }

    if (fs[idx].is_directory)
    {
        print("Error: '");
        print(filename);
        print("' is a directory\n");
        return 1;
    }

    if (fs[idx].content_length == 0)
    {
        print("Error: File is empty\n");
        return 1;
    }

    /* Reset state */
    output_len = 0;
    label_count = 0;
    jump_count = 0;
    has_error = 0;

    /* Assemble */
    print("Assembling ");
    print(filename);
    print("...\n");

    assemble(fs[idx].content);

    if (has_error)
    {
        print("Assembly completed with errors.\n");
    }

    if (output_len == 0)
    {
        print("Error: No code generated\n");
        return 1;
    }

    /* Generate output filename: replace .asm with .bin */
    {
        int flen = str_len(filename);
        int dot_pos = -1;
        for (i = flen - 1; i >= 0; i--)
        {
            if (filename[i] == '.')
            {
                dot_pos = i;
                break;
            }
        }

        if (dot_pos >= 0)
        {
            for (i = 0; i < dot_pos; i++)
                outname[i] = filename[i];
            outname[i] = 0;
        }
        else
        {
            str_copy(outname, filename);
        }

        outpos = str_len(outname);
        outname[outpos++] = '.';
        outname[outpos++] = 'b';
        outname[outpos++] = 'i';
        outname[outpos++] = 'n';
        outname[outpos] = 0;
    }

    /* Write output file */
    if (fs_write(outname, (const char *)output, output_len) == 0)
    {
        char num_str[8];
        print("Success: ");
        print(outname);
        print(" (");
        int_to_str(output_len, num_str);
        print(num_str);
        print(" bytes)\n");
    }
    else
    {
        print("Error: Could not write output file\n");
        return 1;
    }

    /* Print hex dump of first few bytes */
    {
        int dump_len = output_len;
        if (dump_len > 32)
            dump_len = 32;

        print("Hex: ");
        for (i = 0; i < dump_len; i++)
        {
            char hex[3];
            unsigned char b = output[i];
            int hi = (b >> 4) & 0x0F;
            int lo = b & 0x0F;

            hex[0] = hi < 10 ? '0' + hi : 'A' + hi - 10;
            hex[1] = lo < 10 ? '0' + lo : 'A' + lo - 10;
            hex[2] = 0;
            print(hex);
            print(" ");
        }
        if (output_len > 32)
            print("...");
        print("\n");
    }

    return 0;
}
