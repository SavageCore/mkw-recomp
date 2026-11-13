// The players of one install: folders, names and the one played (players.h).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cfg.h"
#include "host.h"
#include "platform/platform.h"
#include "players.h"

void player_dir(const char *base, const char *id, char *out, size_t cap) {
    if (id && *id) snprintf(out, cap, "%s/players/%s", base, id);
    else snprintf(out, cap, "%s", base);
}

void player_name_clean(const char *name, char *out) {
    const unsigned char *s = (const unsigned char *)(name ? name : "");
    while (*s == ' ') s++;
    size_t n = 0;
    int chars = 0;
    while (*s && chars < PLAYER_NAME_CHARS) {
        int len = *s < 0x80 ? 1 : (*s & 0xE0) == 0xC0 ? 2 : (*s & 0xF0) == 0xE0 ? 3 : (*s & 0xF8) == 0xF0 ? 4 : 0;
        int whole = len > 0;
        for (int i = 1; i < len && whole; i++) whole = (s[i] & 0xC0) == 0x80;
        if (!whole) {   // not UTF-8: skipped byte by byte
            s++;
            continue;
        }
        if (n + len > PLAYER_NAME_BYTES - 1) break;
        if (len > 1 || *s >= 0x20) {   // control characters dropped
            memcpy(out + n, s, len);
            n += len;
            chars++;
        }
        s += len;
    }
    while (n && out[n - 1] == ' ') n--;
    out[n] = 0;
}

void player_name(const char *dir, char *out) {
    char path[PLAT_PATH_MAX * 2];
    snprintf(path, sizeof path, "%s/nickname.txt", dir);
    uint64_t size = 0;
    char *text = (char *)plat_file_read(path, &size);
    out[0] = 0;
    if (text) {
        text[strcspn(text, "\r\n")] = 0;
        player_name_clean(text, out);
        free(text);
    }
    if (!out[0]) snprintf(out, PLAYER_NAME_BYTES, "%s", PLAYER_DEFAULT_NAME);
}

static int write_name(const char *dir, const char *name) {
    char clean[PLAYER_NAME_BYTES], path[PLAT_PATH_MAX * 2];
    player_name_clean(name, clean);
    if (!clean[0]) return 0;
    snprintf(path, sizeof path, "%s/nickname.txt", dir);
    return plat_file_write_atomic(path, clean, strlen(clean));
}

// A player's folder name: a number, nothing else (deleted/ and stray files are not players).
static int player_number(const char *name) {
    if (!*name || strlen(name) > 6) return 0;
    for (const char *p = name; *p; p++)
        if (*p < '0' || *p > '9') return 0;
    return atoi(name);
}

typedef struct { int ids[PLAYERS_MAX]; int n; } Numbers;

static int number_cb(const char *name, int kind, uint64_t size, void *ctx) {
    (void)size;
    Numbers *c = (Numbers *)ctx;
    int id = kind == PLAT_FS_DIR ? player_number(name) : 0;
    if (id > 0 && c->n < PLAYERS_MAX - 1) c->ids[c->n++] = id;
    return 1;
}

static int by_number(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

static Numbers numbers(const char *base) {
    Numbers c = {0};
    char dir[PLAT_PATH_MAX * 2];
    snprintf(dir, sizeof dir, "%s/players", base);
    if (plat_dir_exists(dir)) plat_fs_list(dir, number_cb, &c);
    qsort(c.ids, c.n, sizeof *c.ids, by_number);
    return c;
}

int players_list(const char *base, Player *out, int cap) {
    if (cap < 1) return 0;
    char dir[PLAT_PATH_MAX * 2];
    out[0].id[0] = 0;
    player_name(base, out[0].name);
    Numbers c = numbers(base);
    int n = 1;
    for (int i = 0; i < c.n && n < cap; i++, n++) {
        snprintf(out[n].id, sizeof out[n].id, "%d", c.ids[i]);
        player_dir(base, out[n].id, dir, sizeof dir);
        player_name(dir, out[n].name);
    }
    return n;
}

int player_create(const char *base, const char *name, char *id_out, size_t cap) {
    char clean[PLAYER_NAME_BYTES], dir[PLAT_PATH_MAX * 2];
    player_name_clean(name, clean);
    Numbers c = numbers(base);
    if (!clean[0] || c.n >= PLAYERS_MAX - 1) return 0;
    int id = c.n ? c.ids[c.n - 1] + 1 : 2;   // 2: the first player is the first
    snprintf(id_out, cap, "%d", id);
    player_dir(base, id_out, dir, sizeof dir);
    plat_file_mkdir_p(dir);
    if (!plat_dir_exists(dir) || !write_name(dir, clean)) {
        rt_log("players: cannot make %s", dir);
        return 0;
    }
    rt_log("players: new player %s in %s", id_out, dir);
    return 1;
}

int player_rename(const char *base, const char *id, const char *name) {
    char dir[PLAT_PATH_MAX * 2];
    player_dir(base, id, dir, sizeof dir);
    plat_file_mkdir_p(dir);
    int ok = write_name(dir, name);
    if (!ok) rt_log("players: cannot name player '%s' in %s", id, dir);
    return ok;
}

int player_remove(const char *base, const char *id) {
    if (!id || !player_number(id)) return 0;
    char from[PLAT_PATH_MAX * 2], to[PLAT_PATH_MAX * 2 + 64];
    player_dir(base, id, from, sizeof from);
    snprintf(to, sizeof to, "%s/players/deleted", base);
    plat_file_mkdir_p(to);
    snprintf(to, sizeof to, "%s/players/deleted/%s-%lld", base, id, (long long)time(NULL));
    int r = plat_fs_rename(from, to, 1);
    if (r != PLAT_FS_OK) {
        rt_log("players: cannot move %s to %s (%d, OS error %d)", from, to, r, plat_fs_os_error());
        return 0;
    }
    rt_log("players: player %s moved to %s", id, to);
    return 1;
}

void player_active_dir(const char *base, char *out, size_t cap) {
    const char *id = cfg_get("MKW_PLAYER");
    if (id && player_number(id)) {
        player_dir(base, id, out, cap);
        if (plat_dir_exists(out)) return;
        rt_log("players: MKW_PLAYER=%s has no folder (%s): the first player plays", id, out);
    }
    snprintf(out, cap, "%s", base);
}
