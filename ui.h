/* ==========================================================================
 * ui.h  --  all the terminal drawing lives here.
 *
 * The dashboard is drawn with box-drawing characters and ANSI colour.  On
 * Windows we have to switch the console into UTF-8 mode and turn on virtual
 * terminal processing first, otherwise the escape codes come out as garbage.
 * Run with --ascii if your terminal cannot do the line-drawing characters,
 * or --no-color to strip the escapes entirely.
 * ========================================================================== */
#ifndef UI_H
#define UI_H

#include <string>

#define UI_WIDTH 100                  /* total width of every box drawn    */
#define UI_INNER (UI_WIDTH - 4)       /* usable text width inside a box    */

void ui_init(int use_color, int use_unicode);
int  ui_has_color(void);

void ui_clear_screen(void);
void ui_home(void);                   /* cursor to 1,1 without clearing    */
void ui_clear_to_end(void);           /* wipe from cursor down             */
void ui_hide_cursor(void);
void ui_show_cursor(void);

/* colour escapes.  These are empty strings when colour is turned off, so
 * you can always concatenate them safely. */
extern const char *C_RESET, *C_DIM, *C_BOLD, *C_INV;
extern const char *C_RED, *C_GRN, *C_YEL, *C_BLU, *C_MAG, *C_CYN, *C_WHT;
extern const char *C_BRED, *C_BGRN, *C_BYEL, *C_BBLU, *C_BMAG, *C_BCYN;

/* how many columns a string occupies on screen: ANSI escapes count as 0 and
 * a multi-byte UTF-8 character counts as 1 */
int         ui_vislen(const std::string &s);
std::string ui_pad(const std::string &s, int width);
std::string ui_padleft(const std::string &s, int width);
std::string ui_centre (const std::string &s, int width);
std::string ui_trim   (const std::string &s, int width);
std::string ui_repeat(const char *unit, int n);

/* box drawing */
void ui_banner(const std::string &left, const std::string &right);
void ui_box_top(const std::string &title);
void ui_box_row(const std::string &content);
void ui_box_sep(void);
void ui_box_bottom(void);
void ui_box_cols(const std::string cells[], int ncols);   /* split row      */
void ui_box_cols_top(const std::string titles[], int ncols);
void ui_box_cols_bottom(int ncols);

/* MESI state rendered as a coloured letter */
std::string ui_state(int st);
std::string ui_state_wide(int st, int width);

/* small helpers */
std::string ui_hex(unsigned v, int digits);
std::string ui_fmt(const char *fmt, ...);

/* wait for a key; returns the character typed (lower-cased) */
int ui_pause(const std::string &prompt);

#endif /* UI_H */
