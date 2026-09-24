#include "ui.h"
#include "mesi.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#endif

static int g_color   = 1;
static int g_unicode = 1;

const char *C_RESET = "";
const char *C_DIM   = "";
const char *C_BOLD  = "";
const char *C_INV   = "";
const char *C_RED   = "";
const char *C_GRN   = "";
const char *C_YEL   = "";
const char *C_BLU   = "";
const char *C_MAG   = "";
const char *C_CYN   = "";
const char *C_WHT   = "";
const char *C_BRED  = "";
const char *C_BGRN  = "";
const char *C_BYEL  = "";
const char *C_BBLU  = "";
const char *C_BMAG  = "";
const char *C_BCYN  = "";

/* box drawing pieces -- swapped for plain ASCII when --ascii is given */
static const char *BX_H  = "-";
static const char *BX_V  = "|";
static const char *BX_TL = "+";
static const char *BX_TR = "+";
static const char *BX_BL = "+";
static const char *BX_BR = "+";
static const char *BX_LT = "+";
static const char *BX_RT = "+";
static const char *BX_TT = "+";
static const char *BX_BT = "+";
static const char *DH    = "=";
static const char *DV    = "|";
static const char *DTL   = "+";
static const char *DTR   = "+";
static const char *DBL   = "+";
static const char *DBR   = "+";

void ui_init(int use_color, int use_unicode)
{
    g_color   = use_color;
    g_unicode = use_unicode;

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode))
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif

    if (g_color) {
        C_RESET = "\033[0m";  C_DIM  = "\033[2m";  C_BOLD = "\033[1m";
        C_INV   = "\033[7m";
        C_RED   = "\033[31m"; C_GRN  = "\033[32m"; C_YEL  = "\033[33m";
        C_BLU   = "\033[34m"; C_MAG  = "\033[35m"; C_CYN  = "\033[36m";
        C_WHT   = "\033[37m";
        C_BRED  = "\033[91m"; C_BGRN = "\033[92m"; C_BYEL = "\033[93m";
        C_BBLU  = "\033[94m"; C_BMAG = "\033[95m"; C_BCYN = "\033[96m";
    }

    if (g_unicode) {
        BX_H  = "─"; BX_V  = "│";
        BX_TL = "┌"; BX_TR = "┐";
        BX_BL = "└"; BX_BR = "┘";
        BX_LT = "├"; BX_RT = "┤";
        BX_TT = "┬"; BX_BT = "┴";
        DH    = "═"; DV    = "║";
        DTL   = "╔"; DTR   = "╗";
        DBL   = "╚"; DBR   = "╝";
    }
}

int ui_has_color(void) { return g_color; }

/* Clearing the screen and moving the cursor are ANSI escapes too, so
 * --no-color switches them off as well.  The dashboard then just scrolls,
 * which is what you want when piping the output into a file. */
void ui_clear_screen(void)  { if (g_color) fputs("\033[2J\033[H", stdout); else putchar('\n'); }
void ui_home(void)          { if (g_color) fputs("\033[H", stdout); }
void ui_clear_to_end(void)  { if (g_color) fputs("\033[0J", stdout); }
void ui_hide_cursor(void)   { if (g_color) fputs("\033[?25l", stdout); }
void ui_show_cursor(void)   { if (g_color) fputs("\033[?25h", stdout); }

/* Count printable columns.  ANSI escape sequences contribute nothing, and a
 * UTF-8 continuation byte (10xxxxxx) is part of a character we already
 * counted, so we only count lead bytes. */
int ui_vislen(const std::string &s)
{
    int n = 0;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '\033') {
            while (i < s.size() && s[i] != 'm')
                i++;
            if (i < s.size())
                i++;
            continue;
        }
        unsigned char ch = (unsigned char)s[i];
        if ((ch & 0xC0) != 0x80)
            n++;
        i++;
    }
    return n;
}

std::string ui_pad(const std::string &s, int width)
{
    int len = ui_vislen(s);
    std::string out = s;
    for (int i = len; i < width; i++)
        out += ' ';
    return out;
}

/* cut a string down to at most `width` visible columns, keeping any colour
 * escapes it passes through so we never leave the terminal mid-sequence */
std::string ui_trim(const std::string &s, int width)
{
    if (ui_vislen(s) <= width)
        return s;
    std::string out;
    int n = 0;
    size_t i = 0;
    while (i < s.size() && n < width) {
        if (s[i] == '\033') {
            while (i < s.size() && s[i] != 'm')
                out += s[i++];
            if (i < s.size())
                out += s[i++];
            continue;
        }
        unsigned char ch = (unsigned char)s[i];
        if ((ch & 0xC0) != 0x80)
            n++;
        out += s[i++];
        while (i < s.size() && ((unsigned char)s[i] & 0xC0) == 0x80)
            out += s[i++];
    }
    return out;
}

std::string ui_padleft(const std::string &s, int width)
{
    int len = ui_vislen(s);
    std::string out;
    for (int i = len; i < width; i++)
        out += ' ';
    out += s;
    return out;
}

std::string ui_centre(const std::string &s, int width)
{
    int len  = ui_vislen(s);
    int lpad = (width - len) / 2;
    if (lpad < 0) lpad = 0;
    return ui_pad(std::string(lpad, ' ') + s, width);
}

std::string ui_repeat(const char *unit, int n)
{
    std::string out;
    for (int i = 0; i < n; i++)
        out += unit;
    return out;
}

/* the double-line banner at the very top of the dashboard */
void ui_banner(const std::string &left, const std::string &right)
{
    printf("%s%s%s%s%s\n", C_BCYN, DTL, ui_repeat(DH, UI_WIDTH - 2).c_str(), DTR, C_RESET);

    std::string body = " " + left;
    int space = UI_WIDTH - 4 - ui_vislen(body) - ui_vislen(right);
    if (space < 1) {
        /* no room: keep the right hand side, trim the title */
        body  = ui_trim(body, UI_WIDTH - 5 - ui_vislen(right));
        space = 1;
    }
    body += std::string(space, ' ') + right + " ";

    printf("%s%s%s%s%s%s\n", C_BCYN, DV, C_RESET,
           ui_pad(body, UI_WIDTH - 2).c_str(), C_BCYN, (std::string(DV) + C_RESET).c_str());

    printf("%s%s%s%s%s\n", C_BCYN, DBL, ui_repeat(DH, UI_WIDTH - 2).c_str(), DBR, C_RESET);
}

void ui_box_top(const std::string &title)
{
    std::string t = " " + title + " ";
    int fill = UI_WIDTH - 2 - 1 - ui_vislen(t);
    if (fill < 0) fill = 0;
    printf("%s%s%s%s%s%s%s%s\n",
           C_CYN, BX_TL, BX_H, C_BOLD, t.c_str(), C_RESET,
           (std::string(C_CYN) + ui_repeat(BX_H, fill)).c_str(),
           (std::string(BX_TR) + C_RESET).c_str());
}

void ui_box_row(const std::string &content)
{
    printf("%s%s%s %s %s%s%s\n",
           C_CYN, BX_V, C_RESET,
           ui_pad(content, UI_INNER).c_str(),
           C_CYN, BX_V, C_RESET);
}

void ui_box_sep(void)
{
    printf("%s%s%s%s%s\n", C_CYN, BX_LT, ui_repeat(BX_H, UI_WIDTH - 2).c_str(), BX_RT, C_RESET);
}

void ui_box_bottom(void)
{
    printf("%s%s%s%s%s\n", C_CYN, BX_BL, ui_repeat(BX_H, UI_WIDTH - 2).c_str(), BX_BR, C_RESET);
}

/* -- multi column rows (used for the per-CPU strip) -------------------- */

/* The columns have to add up to exactly UI_WIDTH, so the leftover columns
 * from the division are handed out one each to the first few cells. */
static int col_width(int ncols, int which)
{
    int usable = UI_WIDTH - 2 - (ncols - 1);   /* minus borders and dividers */
    int base   = usable / ncols;
    int extra  = usable % ncols;
    return base + (which < extra ? 1 : 0);
}

void ui_box_cols_top(const std::string titles[], int ncols)
{
    std::string line = std::string(C_CYN) + BX_TL;
    for (int i = 0; i < ncols; i++) {
        int w = col_width(ncols, i);
        std::string t = std::string(BX_H) + " " + titles[i] + " ";
        int fill = w - ui_vislen(t);
        if (fill < 0) fill = 0;
        line += t + ui_repeat(BX_H, fill);
        line += (i == ncols - 1) ? BX_TR : BX_TT;
    }
    printf("%s%s\n", line.c_str(), C_RESET);
}

void ui_box_cols(const std::string cells[], int ncols)
{
    std::string line = std::string(C_CYN) + BX_V + C_RESET;
    for (int i = 0; i < ncols; i++) {
        line += ui_pad(" " + cells[i], col_width(ncols, i));
        line += std::string(C_CYN) + BX_V + C_RESET;
    }
    printf("%s\n", line.c_str());
}

void ui_box_cols_bottom(int ncols)
{
    std::string line = std::string(C_CYN) + BX_BL;
    for (int i = 0; i < ncols; i++) {
        line += ui_repeat(BX_H, col_width(ncols, i));
        line += (i == ncols - 1) ? BX_BR : BX_BT;
    }
    printf("%s%s\n", line.c_str(), C_RESET);
}

/* -- MESI states ------------------------------------------------------- */

static const char *state_color(int st)
{
    switch (st) {
        case ST_M: return C_BRED;
        case ST_E: return C_BYEL;
        case ST_S: return C_BGRN;
        default:   return C_DIM;
    }
}

std::string ui_state(int st)
{
    return std::string(state_color(st)) + state_name(st) + C_RESET;
}

std::string ui_state_wide(int st, int width)
{
    std::string body = state_name(st);
    int lpad = (width - 1) / 2;
    int rpad = width - 1 - lpad;
    return std::string(lpad, ' ') + state_color(st) + body + C_RESET + std::string(rpad, ' ');
}

/* -- misc -------------------------------------------------------------- */

std::string ui_hex(unsigned v, int digits)
{
    char buf[32];
    sprintf(buf, "%0*X", digits, v);
    return std::string("0x") + buf;
}

std::string ui_fmt(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return std::string(buf);
}

int ui_pause(const std::string &prompt)
{
    printf("%s", prompt.c_str());
    fflush(stdout);
    int c = getchar();
    if (c >= 'A' && c <= 'Z')
        c = c - 'A' + 'a';
    /* swallow the rest of the line so ENTER does not queue up */
    if (c != '\n' && c != EOF) {
        int d;
        while ((d = getchar()) != '\n' && d != EOF)
            ;
    }
    return c;
}
