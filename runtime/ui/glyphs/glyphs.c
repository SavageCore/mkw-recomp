// Button icons for the device in use (glyphs.h).
//
// MKW draws its button prompts ("Pulsa L R", menus, the HUD) as characters of its own picture font,
// romfs/Font/PicFont_00.Nin_Ounce_NVN2.bffnt: 40 glyphs at U+E0A0..E0CF in 104x104 cells (105 apart, rows stored
// bottom-up) of one 1024x1024 RGBA8 sRGB sheet: a white button, a grey symbol, a translucent black outline. The prompts'
// text tags pick the characters, so the sheet is what changes, not the texts: as the sheet is uploaded
// (vk_tex_upload.c) its cells are redrawn for the device in use, and it is uploaded again when that changes
// (glyphs_generation). The idea is SavageWindRecomp's runtime/nvn/glyphs.c (https://github.com/Revivedx/SavageWindRecomp,
// branch exp/recorder, MIT), where BOTW's sheet is a BC4 font; the shapes are its (ui/glyphs/glyph_draw.c).
//
// The icons follow the bindings (input_map.h): the cell of Switch A shows the controller button or the key that presses
// A now. Nothing of Nintendo's ships: Xbox and PlayStation buttons, keycaps and mice are drawn, labels set in Roboto.
//
// MKW_GLYPH_DUMP=<dir>: every RGBA8 1024x1024 upload is saved as <dir>/glyph_<addr>.rgba (raw rows, as uploaded) with
// its hash in the log, and the redrawn sheet beside it as glyph_<addr>_<kind>.rgba.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "glyphs.h"
#include "input.h"
#include "input_map.h"
#include "platform/platform.h"
#include "ui/glyphs/glyph_draw.h"

void rt_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
const unsigned char *menu_look_font_data(void);   // ui/menu/menu_look.cpp: the embedded Roboto

#define FMT_RGBA8 0x25
#define FMT_RGBA8_SRGB 0x38   // the sheet's (lz1, 06/11/2026)
#define SHEET 1024
#define CELL 104
#define PITCH 105
// FNV-1a of the sheet as uploaded. 1.8.0's has two more glyphs (U+E0CB and U+E0CC in cells 39 and 40, the coin moves
// to 41); cells 0-38 are where 1.0.0 has them, so the same table redraws both (hash from seq180d, 14/11/2026).
#define SHEET_HASH_100 0x832514785d492576ull   // player2 06/11/2026
#define SHEET_HASH_180 0xa792f977841ba116ull

// ---- what each cell shows
enum { G_KEEP, G_BUTTON, G_FACE, G_STICK, G_STICK_PRESS, G_DPAD };
typedef struct { uint8_t kind, arg; } Meaning;
static const Meaning k_cells[40] = {
    {G_BUTTON, B_A}, {G_BUTTON, B_B}, {G_BUTTON, B_X}, {G_BUTTON, B_Y}, {G_BUTTON, B_L}, {G_BUTTON, B_R},
    {G_BUTTON, B_ZL}, {G_BUTTON, B_ZR}, {G_KEEP, 0}, {G_KEEP, 0},              // SL, SR: Joy-Con only
    {G_FACE, 0xff}, {G_FACE, B_A}, {G_FACE, B_B}, {G_FACE, B_X}, {G_FACE, B_Y}, // the face diamond, one button lit
    {G_KEEP, 0}, {G_KEEP, 0}, {G_KEEP, 0}, {G_KEEP, 0},                        // a sideways Joy-Con's buttons
    {G_BUTTON, B_PLUS}, {G_BUTTON, B_MINUS}, {G_KEEP, 0}, {G_KEEP, 0},         // Home, capture
    {G_KEEP, 0}, {G_KEEP, 0}, {G_KEEP, 0},                                     // the controllers themselves
    {G_STICK, 0}, {G_STICK, 0}, {G_STICK, 1},                                  // a stick, the left, the right
    {G_STICK_PRESS, 0}, {G_STICK_PRESS, 0}, {G_STICK_PRESS, 1},
    {G_DPAD, GLYPH_UP | GLYPH_DOWN | GLYPH_LEFT | GLYPH_RIGHT}, {G_DPAD, GLYPH_UP}, {G_DPAD, GLYPH_DOWN},
    {G_DPAD, GLYPH_LEFT}, {G_DPAD, GLYPH_RIGHT}, {G_DPAD, GLYPH_UP | GLYPH_DOWN}, {G_DPAD, GLYPH_LEFT | GLYPH_RIGHT},
    {G_KEEP, 0},                                                               // the coin
};

// ---- which icons
int glyphs_kind(void) {
    const char *s = cfg_value("MKW_BUTTON_ICONS");
    if (!strcmp(s, "nintendo")) return GLYPHS_NINTENDO;
    if (!strcmp(s, "xbox")) return GLYPHS_XBOX;
    if (!strcmp(s, "playstation")) return GLYPHS_PLAYSTATION;
    if (!strcmp(s, "keyboard")) return GLYPHS_KEYBOARD;
    if (input_last_device() == INPUT_DEVICE_KEYBOARD) return GLYPHS_KEYBOARD;
    int south = input_pad_label(PAD_SOUTH), east = input_pad_label(PAD_EAST);
    if (south == PAD_LABEL_CROSS) return GLYPHS_PLAYSTATION;
    if (east == PAD_LABEL_A) return GLYPHS_NINTENDO;
    return GLYPHS_XBOX;
}

static InputMap g_map;   // the bindings the sheet was drawn with (glyphs_apply)

// The gamepad button bound to action, PAD_NONE when none; and the first key
static int pad_of(const InputMap *m, int action) {
    int layout = input_pad_layout();
    if (layout < 0) layout = !strcmp(cfg_value("MKW_PAD_LAYOUT"), "nintendo") ? PAD_LAYOUT_NINTENDO : PAD_LAYOUT_XBOX;
    for (int i = 0; i < m->npads[layout]; i++)
        if (m->pads[layout][i].action == action) return m->pads[layout][i].pad;
    return PAD_NONE;
}

static int key_of(const InputMap *m, int action) {
    for (int i = 0; i < m->nkeys; i++)
        if (m->keys[i].action == action) return m->keys[i].key;
    return KEY_NONE;
}

uint32_t glyphs_generation(void) {
    int kind = glyphs_kind();
    InputMap m;
    input_map_snapshot(&m);
    uint32_t h = 2166136261u ^ (uint32_t)kind;
    if (kind == GLYPHS_KEYBOARD)
        for (int a = 0; a < ACTION_COUNT; a++) h = (h ^ (uint32_t)key_of(&m, a)) * 16777619u;
    else
        for (int a = 0; a < ACTION_COUNT; a++) h = (h ^ (uint32_t)pad_of(&m, a)) * 16777619u;
    return h;
}

// ---- icons
// A key's short label; NULL for an arrow key (*dir set) or a mouse button (*mouse set)
static const char *key_label(int key, int *dir, int *mouse) {
    static char buf[16];
    *dir = *mouse = 0;
    if (key >= KEY_A && key <= KEY_Z) return buf[0] = (char)('A' + key - KEY_A), buf[1] = 0, buf;
    if (key >= KEY_0 && key <= KEY_9) return buf[0] = (char)('0' + key - KEY_0), buf[1] = 0, buf;
    if (key >= KEY_MOUSE_L && key <= KEY_MOUSE_X2) {
        static const int b[] = {1, 3, 2, 4, 5};
        *mouse = b[key - KEY_MOUSE_L];
        return NULL;
    }
    switch (key) {
    case KEY_UP: *dir = GLYPH_UP; return NULL;
    case KEY_DOWN: *dir = GLYPH_DOWN; return NULL;
    case KEY_LEFT: *dir = GLYPH_LEFT; return NULL;
    case KEY_RIGHT: *dir = GLYPH_RIGHT; return NULL;
    case KEY_SPACE: return "Space";
    case KEY_TAB: return "Tab";
    case KEY_LSHIFT: case KEY_RSHIFT: return "Shift";
    case KEY_LCTRL: case KEY_RCTRL: return "Ctrl";
    case KEY_LALT: return "Alt";
    case KEY_ENTER: return "Enter";
    case KEY_BACKSPACE: return "Bksp";
    case KEY_CAPSLOCK: return "Caps";
    case KEY_INSERT: return "Ins";
    case KEY_DELETE: return "Del";
    case KEY_HOME: return "Home";
    case KEY_END: return "End";
    case KEY_PAGEUP: return "PgUp";
    case KEY_PAGEDOWN: return "PgDn";
    default: break;
    }
    if (key >= KEY_F2 && key <= KEY_F12) {
        static const int f[] = {2, 3, 4, 5, 6, 7, 9, 12};
        snprintf(buf, sizeof buf, "F%d", f[key - KEY_F2]);
        return buf;
    }
    if (key >= KEY_NUM0 && key <= KEY_NUM9) {
        snprintf(buf, sizeof buf, "Num%d", key - KEY_NUM0);
        return buf;
    }
    uint32_t ch = input_key_char(key);   // punctuation, in the keyboard layout in use
    if (ch > 0x20 && ch < 0x7f) return buf[0] = (char)ch, buf[1] = 0, buf;
    return key == KEY_NONE ? "-" : "?";
}

static void key_icon(GlyphCanvas *c, GlyphBox b, int key) {
    int dir, mouse;
    const char *l = key_label(key, &dir, &mouse);
    if (mouse) glyph_mouse(c, b, mouse);
    else glyph_keycap(c, b, l, dir);
}

// A controller button as an Xbox or PlayStation icon (pad: input_map.h PAD_*)
static void pad_icon(GlyphCanvas *c, GlyphBox b, int pad, int ps) {
    static const char *const xbox[] = {"A", "B", "X", "Y"};
    static const int psface[] = {GLYPH_PS_CROSS, GLYPH_PS_CIRCLE, GLYPH_PS_SQUARE, GLYPH_PS_TRIANGLE};
    switch (pad) {
    case PAD_SOUTH: case PAD_EAST: case PAD_WEST: case PAD_NORTH:
        if (ps) glyph_ps_face(c, b, psface[pad - PAD_SOUTH]);
        else glyph_round(c, b, xbox[pad - PAD_SOUTH]);
        return;
    case PAD_L: glyph_tab(c, b, ps ? "L1" : "LB"); return;
    case PAD_R: glyph_tab(c, b, ps ? "R1" : "RB"); return;
    case PAD_ZL: glyph_tab(c, b, ps ? "L2" : "LT"); return;
    case PAD_ZR: glyph_tab(c, b, ps ? "R2" : "RT"); return;
    case PAD_START: glyph_menu_button(c, b, 1, ps); return;
    case PAD_BACK: glyph_menu_button(c, b, 0, ps); return;
    case PAD_LSTICK: glyph_round(c, b, ps ? "L3" : "LS"); return;
    case PAD_RSTICK: glyph_round(c, b, ps ? "R3" : "RS"); return;
    case PAD_DPAD_UP: glyph_dpad(c, b, GLYPH_UP); return;
    case PAD_DPAD_DOWN: glyph_dpad(c, b, GLYPH_DOWN); return;
    case PAD_DPAD_LEFT: glyph_dpad(c, b, GLYPH_LEFT); return;
    case PAD_DPAD_RIGHT: glyph_dpad(c, b, GLYPH_RIGHT); return;
    default: glyph_tab(c, b, "-"); return;
    }
}

// The face diamond with the button at pad lit and the others as rings (0xff: all four lit), as the game's
static void face_icon(GlyphCanvas *c, GlyphBox b, int pad) {
    float r = b.s * 0.15f, d = b.s * 0.31f;
    static const float pos[4][2] = {{0, 1}, {1, 0}, {-1, 0}, {0, -1}};   // south, east, west, north
    for (int i = 0; i < 4; i++) {
        int lit = pad == 0xff || pad == PAD_SOUTH + i;
        glyph_circle(c, b.cx + pos[i][0] * d, b.cy + pos[i][1] * d, r, lit ? 0 : r * 0.35f);
    }
}

static const int k_dpad_action[4] = {B_UP, B_DOWN, B_LEFT, B_RIGHT};   // GLYPH_UP, DOWN, LEFT, RIGHT order

// The cell's icon for kind; 0: keep the game's
static int draw_cell(GlyphCanvas *c, GlyphBox b, Meaning m, int kind, const InputMap *map) {
    int ps = kind == GLYPHS_PLAYSTATION;
    if (kind == GLYPHS_KEYBOARD) {
        switch (m.kind) {
        case G_BUTTON: key_icon(c, b, key_of(map, m.arg)); return 1;
        case G_FACE: if (m.arg == 0xff) return 0; key_icon(c, b, key_of(map, m.arg)); return 1;
        case G_STICK_PRESS: key_icon(c, b, key_of(map, m.arg ? B_STICK_R : B_STICK_L)); return 1;
        case G_STICK:
            if (m.arg) { glyph_mouse(c, b, 0); return 1; }   // the right stick is the camera: the mouse
            {   // the left stick: its four keys on one keycap when they are letters ("WASD"), else arrows
                int u = key_of(map, B_SL_UP), l = key_of(map, B_SL_LEFT), dn = key_of(map, B_SL_DOWN), r = key_of(map, B_SL_RIGHT);
                char wasd[5] = "";
                if (u >= KEY_A && u <= KEY_Z && l >= KEY_A && l <= KEY_Z && dn >= KEY_A && dn <= KEY_Z && r >= KEY_A && r <= KEY_Z)
                    snprintf(wasd, sizeof wasd, "%c%c%c%c", 'A' + u - KEY_A, 'A' + l - KEY_A, 'A' + dn - KEY_A, 'A' + r - KEY_A);
                glyph_keycap(c, b, wasd[0] ? wasd : NULL, GLYPH_UP | GLYPH_DOWN | GLYPH_LEFT | GLYPH_RIGHT);
                return 1;
            }
        case G_DPAD:
            if (!(m.arg & (m.arg - 1))) {
                int i = m.arg == GLYPH_UP ? 0 : m.arg == GLYPH_DOWN ? 1 : m.arg == GLYPH_LEFT ? 2 : 3;
                key_icon(c, b, key_of(map, k_dpad_action[i]));
            } else {
                glyph_keycap(c, b, NULL, m.arg);
            }
            return 1;
        default: return 0;
        }
    }
    if (kind == GLYPHS_NINTENDO) return 0;   // the game's own (a Switch layout)
    switch (m.kind) {
    case G_BUTTON: pad_icon(c, b, pad_of(map, m.arg), ps); return 1;
    case G_FACE: face_icon(c, b, m.arg == 0xff ? 0xff : pad_of(map, m.arg)); return 1;
    case G_STICK_PRESS: glyph_round(c, b, m.arg ? (ps ? "R3" : "RS") : (ps ? "L3" : "LS")); return 1;
    case G_DPAD: glyph_dpad(c, b, m.arg); return 1;
    default: return 0;   // the sticks: the game's (L / R on a stick)
    }
}

// ---- the sheet
static uint64_t fnv1a(const uint8_t *p, uint64_t n, uint64_t h) {
    for (uint64_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}

static void dump(const char *name, uint64_t addr, const uint8_t *lin, uint32_t row) {
    const char *dir = cfg_get("MKW_GLYPH_DUMP");
    if (!dir || !*dir) return;
    char path[1024];
    snprintf(path, sizeof path, "%s/glyph_%llx%s.rgba", dir, (unsigned long long)addr, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    for (uint32_t y = 0; y < SHEET; y++) fwrite(lin + (size_t)y * row, 1, SHEET * 4, f);
    fclose(f);
    rt_log("glyphs: dumped %s", path);
}

static uint8_t *g_orig;   // the sheet as the game uploaded it: each redraw starts from it

int glyphs_apply(uint64_t addr, int format, uint32_t w, uint32_t h, uint8_t *lin, uint32_t row) {
    if ((format != FMT_RGBA8 && format != FMT_RGBA8_SRGB) || w != SHEET || h != SHEET || row < SHEET * 4) return 0;
    uint64_t hash = 0xcbf29ce484222325ull;
    for (uint32_t y = 0; y < SHEET; y++) hash = fnv1a(lin + (size_t)y * row, SHEET * 4, hash);
    if (hash != SHEET_HASH_100 && hash != SHEET_HASH_180) {   // (a re-upload reads the game's storage again: the original, not our icons)
        static int n;
        if (n++ < 4) rt_log("glyphs: 1024x1024 RGBA8 texture %#llx is not the button sheet (hash %016llx)",
                            (unsigned long long)addr, (unsigned long long)hash);
        dump("", addr, lin, row);
        return 0;
    }
    if (!g_orig) {
        g_orig = malloc((size_t)SHEET * SHEET * 4);
        if (!g_orig) return 1;
        for (uint32_t y = 0; y < SHEET; y++) memcpy(g_orig + (size_t)y * SHEET * 4, lin + (size_t)y * row, SHEET * 4);
        glyph_font_set(menu_look_font_data());
    }
    for (uint32_t y = 0; y < SHEET; y++) memcpy(lin + (size_t)y * row, g_orig + (size_t)y * SHEET * 4, SHEET * 4);
    int kind = glyphs_kind();
    input_map_snapshot(&g_map);
    static GlyphCanvas cv;
    static const GlyphLook look = {{245, 245, 245}, {120, 120, 120}, {0, 0, 0}, 0.3f, 4};
    uint8_t cell[CELL * CELL * 4];
    int drawn = 0;
    for (int i = 0; i < 40; i++) {
        int x0 = (i % 9) * PITCH, y0 = (i / 9) * PITCH;   // top-down cell origin; rows are stored bottom-up
        // the game's glyph box (its opaque pixels), so ours keeps its size and place
        int bx0 = CELL, by0 = CELL, bx1 = -1, by1 = -1;
        for (int y = 0; y < CELL; y++)
            for (int x = 0; x < CELL; x++)
                if (g_orig[((size_t)(SHEET - 1 - (y0 + y)) * SHEET + x0 + x) * 4 + 3] > 200) {
                    if (x < bx0) bx0 = x;
                    if (x > bx1) bx1 = x;
                    if (y < by0) by0 = y;
                    if (y > by1) by1 = y;
                }
        if (bx1 < 0) continue;
        GlyphBox b = {(bx0 + bx1 + 1) / 2.0f, (by0 + by1 + 1) / 2.0f, (float)(by1 - by0 + 1), (float)(bx1 - bx0 + 1)};
        memset(&cv, 0, sizeof cv);
        cv.size = CELL;
        if (!draw_cell(&cv, b, k_cells[i], kind, &g_map)) continue;
        glyph_compose(&cv, cell, CELL * 4, &look);
        for (int y = 0; y < CELL; y++)
            memcpy(lin + (size_t)(SHEET - 1 - (y0 + y)) * row + (size_t)x0 * 4, cell + (size_t)y * CELL * 4, CELL * 4);
        drawn++;
    }
    static const char *const names[] = {"_nintendo", "_xbox", "_playstation", "_keyboard"};
    rt_log("glyphs: button sheet %#llx: %s icons, %d cells redrawn", (unsigned long long)addr, names[kind] + 1, drawn);
    dump(names[kind], addr, lin, row);
    return 1;
}
