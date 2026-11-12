// The Mods page: scan, import, enable, delete. Enabling only writes config (mkw.ini's MODS order and the mod's
// switches); the game reads both at the next start, so nothing here touches the running game. The import runs
// tools/mod/import.py in a child process on a thread, like launcher_setup.cpp runs prepare.py.
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imgui.h"
#include "ui/launcher/launcher_setup.h"
#include "ui/launcher/mods.h"
#include "ui/menu/menu_look.h"

extern "C" {
#include "cfg.h"
#include "platform/platform.h"
#include "ui_text.h"
void rt_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
}

static struct {
    Mod mods[MODS_MAX];
    int n;
    char exe_dir[PLAT_PATH_MAX];
    // import
    SDL_Thread *thread;
    SDL_Process *proc;
    SDL_Mutex *lock;
    int running, finished;
    char done[64], code[32], error[512];
    char pick[PLAT_PATH_MAX];   // the zip or folder the dialog chose
    int pick_is_zip;
    char name[MOD_NAME_MAX];    // the rename field; empty: import.py takes the mod's own name
} G = {};

ModsState mods_state(void) {
    ModsState s = {0, G.finished, "", "", ""};
    if (G.lock) SDL_LockMutex(G.lock);
    s.running = G.running;
    snprintf(s.done, sizeof s.done, "%s", G.done);
    snprintf(s.code, sizeof s.code, "%s", G.code);
    snprintf(s.error, sizeof s.error, "%s", G.error);
    if (G.lock) SDL_UnlockMutex(G.lock);
    return s;
}

static void mods_dir(const char *exe_dir, char *out, size_t cap) {
    snprintf(out, cap, "%s/mods", exe_dir);
}

// The value of "key" in {"a": 1, "key": value, ...} (mod.json's flat top level): a number, true/false or "string".
static int json_value(const char *json, const char *key, char *out, size_t cap) {
    char want[64];
    snprintf(want, sizeof want, "\"%s\"", key);
    const char *p = strstr(json, want);
    if (!p) return 0;
    p = strchr(p + strlen(want), ':');
    if (!p) return 0;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '"') p++;
    size_t n = 0;
    while (p[n] && p[n] != '"' && p[n] != ',' && p[n] != '}' && p[n] != '\n' && n + 1 < cap) n++;
    while (n && (p[n - 1] == ' ' || p[n - 1] == '\t')) n--;
    memcpy(out, p, n);
    out[n] = 0;
    return 1;
}

// mod.json's "switches": {"MKW_A": "1", ...} into the mod (our own files: flat, MKW_ keys, short values).
static void json_switches(const char *json, Mod *m) {
    const char *p = strstr(json, "\"switches\"");
    if (!p || !(p = strchr(p, '{'))) return;
    for (p++; *p && *p != '}' && m->nswitches < MOD_SWITCHES_MAX; p++) {
        if (*p != '"') continue;
        const char *k = ++p, *ke = strchr(k, '"');
        if (!ke || ke - k < 5 || ke - k >= (int)sizeof m->switches[0].name) break;
        const char *v = strchr(ke, ':');
        if (!v) break;
        v++;
        while (*v == ' ' || *v == '\t' || *v == '"') v++;
        const char *ve = v;
        while (*ve && *ve != '"' && *ve != ',' && *ve != '}') ve++;
        size_t vn = ve - v;
        while (vn && (v[vn - 1] == ' ' || v[vn - 1] == '\t')) vn--;
        if (vn == 0 || vn >= sizeof m->switches[0].value) break;
        ModSwitch *s = &m->switches[m->nswitches++];
        memcpy(s->name, k, (size_t)(ke - k));
        s->name[ke - k] = 0;
        memcpy(s->value, v, vn);
        s->value[vn] = 0;
        p = ve;
    }
}

static int list_cb(const char *name, int kind, uint64_t, void *ctx) {
    char (*exe)[PLAT_PATH_MAX] = (char (*)[PLAT_PATH_MAX])ctx;
    if (kind != PLAT_FS_DIR || G.n >= MODS_MAX) return 1;
    if (strlen(name) >= MOD_NAME_MAX) return 1;   // import.py caps names at 64; not ours
    Mod *m = &G.mods[G.n];
    memset(m, 0, sizeof *m);
    snprintf(m->name, sizeof m->name, "%s", name);
    snprintf(m->title, sizeof m->title, "%s", name);
    char meta[PLAT_PATH_MAX * 2 + 32], json[4096];
    snprintf(meta, sizeof meta, "%s/mods/%s/mod.json", *exe, name);
    PlatFile *f = plat_fs_open(meta, 0, NULL);
    if (f) {
        int64_t n = plat_file_pread(f, json, sizeof json - 1, 0);
        plat_file_close(f);
        if (n < 0) n = 0;
        json[n] = 0;
        char v[96];
        if (json_value(json, "title", v, sizeof v) && *v) snprintf(m->title, sizeof m->title, "%s", v);
        if (json_value(json, "has_romfs", v, sizeof v)) m->has_romfs = !strcmp(v, "true") || !strcmp(v, "1");
        if (json_value(json, "romfs_files", v, sizeof v)) m->romfs_files = atoi(v);
        if (json_value(json, "has_exefs", v, sizeof v)) m->has_exefs = !strcmp(v, "true") || !strcmp(v, "1");
        if (json_value(json, "exefs_match", v, sizeof v)) m->exefs_match = !strcmp(v, "true") || !strcmp(v, "1");
        const char *b = strstr(json, "\"exefs_builds\"");
        if (b && (b = strchr(b, '[')) && (b = strchr(b, '"'))) {
            size_t n = 0;
            while (b[1 + n] && b[1 + n] != '"' && n < 16) n++;
            memcpy(m->exefs_build, b + 1, n);
            m->exefs_build[n] = 0;
        }
        json_switches(json, m);
    }
    G.n++;
    return 1;
}

static int mod_enabled(const char *name) {
    const char *mods = cfg_get("MKW_MODS");
    if (!mods || !*mods) return 0;
    char copy[4096];
    snprintf(copy, sizeof copy, "%s", mods);
    for (char *t = strtok(copy, ","); t; t = strtok(NULL, ",")) {
        while (*t == ' ') t++;
        if (!strcmp(t, name)) return 1;
    }
    return 0;
}

void mods_refresh(const char *exe_dir) {
    if (exe_dir != G.exe_dir) snprintf(G.exe_dir, sizeof G.exe_dir, "%s", exe_dir);   // not a self-copy (UB)
    G.n = 0;
    char dir[PLAT_PATH_MAX];
    mods_dir(G.exe_dir, dir, sizeof dir);
    if (plat_dir_exists(dir)) plat_fs_list(dir, list_cb, &G.exe_dir);
    // plat_fs_list order is the filesystem's; keep the page stable
    for (int i = 0; i < G.n; i++)
        for (int j = i + 1; j < G.n; j++)
            if (strcmp(G.mods[i].name, G.mods[j].name) > 0) {
                Mod t = G.mods[i];
                G.mods[i] = G.mods[j];
                G.mods[j] = t;
            }
    for (int i = 0; i < G.n; i++) G.mods[i].enabled = mod_enabled(G.mods[i].name);
}

// Every scanned mod's switches off, then the enabled mods' on, in MODS order (later mods win shared keys).
void mods_apply(void) {
    const char *names[MODS_MAX * MOD_SWITCHES_MAX];
    int nn = 0;
    for (int i = 0; i < G.n; i++)
        for (int s = 0; s < G.mods[i].nswitches; s++) names[nn++] = G.mods[i].switches[s].name;
    if (nn) cfg_reset(names, nn);
    for (int i = 0; i < G.n; i++) {
        if (!G.mods[i].enabled) continue;
        for (int s = 0; s < G.mods[i].nswitches; s++) {
            char var[96];
            snprintf(var, sizeof var, "MKW_%s", G.mods[i].switches[s].name);
            cfg_set(var, G.mods[i].switches[s].value);
        }
    }
    cfg_save();
}

int mods_count(void) { return G.n; }
const Mod *mods_at(int i) { return i >= 0 && i < G.n ? &G.mods[i] : NULL; }

static void remove_tree(const char *path);
static void set_enabled(const char *name, int on) {
    char keep[MODS_MAX][MOD_NAME_MAX], cur[4096];
    int nk = 0;
    const char *mods = cfg_get("MKW_MODS");
    snprintf(cur, sizeof cur, "%s", mods ? mods : "");
    for (char *t = strtok(cur, ","); t && nk < MODS_MAX; t = strtok(NULL, ",")) {
        while (*t == ' ') t++;
        if (*t && strcmp(t, name)) snprintf(keep[nk++], sizeof keep[0], "%s", t);
    }
    // enabling appends: the MODS order is the overlay precedence, the newest mod wins shared files
    if (on && nk < MODS_MAX) snprintf(keep[nk++], sizeof keep[0], "%s", name);
    char out[4096] = "";
    for (int i = 0; i < nk; i++) {
        if (i) strcat(out, ",");
        strcat(out, keep[i]);
    }
    cfg_set("MKW_MODS", out);
    mods_refresh(G.exe_dir);
    mods_apply();   // saves too
}

void mods_toggle(const char *name, int on) { set_enabled(name, on); }

int mods_is_enabled(const char *name) {
    for (int i = 0; i < G.n; i++)
        if (!strcmp(G.mods[i].name, name)) return G.mods[i].enabled;
    return mod_enabled(name);
}

void mods_delete(const char *name) {
    char path[PLAT_PATH_MAX * 2];
    snprintf(path, sizeof path, "%s/mods/%s", G.exe_dir, name);
    remove_tree(path);
    set_enabled(name, 0);   // drops it from MODS when present; re-applies switches and rescans either way
}

typedef struct {
    char path[PLAT_PATH_MAX];
} RmCtx;

static int rm_cb(const char *name, int kind, uint64_t, void *ctx) {
    RmCtx *c = (RmCtx *)ctx;
    char full[PLAT_PATH_MAX * 2];
    snprintf(full, sizeof full, "%s/%s", c->path, name);
    if (kind == PLAT_FS_DIR) {
        RmCtx sub;
        snprintf(sub.path, sizeof sub.path, "%s", full);
        plat_fs_list(full, rm_cb, &sub);
        plat_fs_rmdir(full);
    } else {
        plat_fs_unlink(full);
    }
    return 1;
}

// Our own imports only, under <exe>/mods: depth-first, then the mod folder itself.
static void remove_tree(const char *path) {
    RmCtx c;
    snprintf(c.path, sizeof c.path, "%s", path);
    plat_fs_list(path, rm_cb, &c);
    plat_fs_rmdir(path);
}

// The wrapper scan for the rename prefill: the single non-hidden subfolder's name, and how many there are.
static char wrap_sub[MOD_NAME_MAX];
static int wrap_n;

static int wrap_cb(const char *name, int kind, uint64_t, void *) {
    if (kind == PLAT_FS_DIR && name[0] != '.') {
        if (wrap_n == 0) snprintf(wrap_sub, sizeof wrap_sub, "%s", name);
        wrap_n++;
    }
    return 1;
}

void mods_pick(const char *path, int is_zip) {
    snprintf(G.pick, sizeof G.pick, "%s", path);
    G.pick_is_zip = is_zip;
    // prefill the rename field: the zip stem, or the folder's name (its single subfolder when the picked folder
    // only wraps the mod, as a zip extraction does); empty means import.py takes the mod's own name
    const char *base = strrchr(path, '/');
#ifdef _WIN32
    const char *bs = strrchr(path, '\\');
    if (bs && (!base || bs > base)) base = bs;
#endif
    base = base ? base + 1 : path;
    snprintf(G.name, sizeof G.name, "%s", base);
    if (is_zip) {
        size_t n = strlen(G.name);   // "timer.zip" -> "timer"
        if (n > 4 && G.name[n - 4] == '.' && (G.name[n - 3] == 'z' || G.name[n - 3] == 'Z') &&
            (G.name[n - 2] == 'i' || G.name[n - 2] == 'I') &&
            (G.name[n - 1] == 'p' || G.name[n - 1] == 'P'))
            G.name[n - 4] = 0;
    } else {
        char romfs[PLAT_PATH_MAX * 2], exefs[PLAT_PATH_MAX * 2];
        snprintf(romfs, sizeof romfs, "%s/romfs", path);
        snprintf(exefs, sizeof exefs, "%s/exefs", path);
        if (!plat_dir_exists(romfs) && !plat_dir_exists(exefs) && plat_dir_exists(path)) {
            wrap_n = 0;
            wrap_sub[0] = 0;
            plat_fs_list(path, wrap_cb, NULL);
            if (wrap_n == 1)
                snprintf(G.name, sizeof G.name, "%s", wrap_sub);
            else
                G.name[0] = 0;   // import.py decides by its own single-inner rule
        }
    }
    G.finished = 0;
    G.done[0] = G.code[0] = G.error[0] = 0;
}

int mods_has_pick(void) { return G.pick[0] != 0; }
int mods_pick_is_zip(void) { return G.pick_is_zip; }
const char *mods_pick_path(void) { return G.pick; }
char *mods_name_buf(void) { return G.name; }

void mods_cancel(void) {
    if (!G.lock) return;
    SDL_LockMutex(G.lock);
    if (G.proc) SDL_KillProcess(G.proc, true);
    SDL_UnlockMutex(G.lock);
}

void mods_clear_pick(void) {
    G.pick[0] = G.name[0] = 0;
    G.finished = 0;
    G.done[0] = G.code[0] = G.error[0] = 0;
}

static void set_finished(int how, const char *code, const char *why) {
    SDL_LockMutex(G.lock);
    G.running = 0;
    G.finished = how;
    if (code) snprintf(G.code, sizeof G.code, "%s", code);
    if (why) snprintf(G.error, sizeof G.error, "%s", why);
    SDL_UnlockMutex(G.lock);
}

// One event line of import.py ("done <name> ..." or "error <code> <detail>", like prepare.py's).
static void on_line(char *line) {
    SDL_LockMutex(G.lock);
    char *arg = strchr(line, ' ');
    if (arg) *arg++ = 0;
    if (!strcmp(line, "done") && arg) {
        char *sp = strchr(arg, ' ');
        if (sp) *sp = 0;
        snprintf(G.done, sizeof G.done, "%s", arg);
    } else if (!strcmp(line, "error") && arg) {
        char *why = strchr(arg, ' ');
        if (why) *why++ = 0;
        snprintf(G.code, sizeof G.code, "%s", arg);
        snprintf(G.error, sizeof G.error, "%s", why ? why : "");
    }
    SDL_UnlockMutex(G.lock);
}

static int SDLCALL reader(void *) {
    SDL_IOStream *out = SDL_GetProcessOutput(G.proc);
    char line[2048];
    size_t n = 0;
    char c;
    for (;;) {
        if (SDL_ReadIO(out, &c, 1) != 1) {   // the pipe does not block: no data yet is not the end
            if (SDL_GetIOStatus(out) != SDL_IO_STATUS_NOT_READY) break;
            SDL_Delay(20);
            continue;
        }
        if (c == '\r') continue;
        if (c != '\n') {
            if (n + 1 < sizeof line) line[n++] = c;
            continue;
        }
        line[n] = 0;
        n = 0;
        on_line(line);
    }
    int code = -1;
    SDL_WaitProcess(G.proc, true, &code);
    SDL_DestroyProcess(G.proc);
    G.proc = nullptr;
    ModsState s = mods_state();
    int ok = code == 0 && s.done[0];
    if (ok) {
        char exe[PLAT_PATH_MAX];
        snprintf(exe, sizeof exe, "%s", G.exe_dir);
        mods_refresh(exe);   // the new mod is on disk now
    }
    if (!ok && !s.code[0]) set_finished(-1, "stopped", "");
    else set_finished(ok ? 1 : -1, nullptr, nullptr);
    rt_log("mods: import ended with code %d", code);
    return 0;
}

void mods_import_start(const char *exe_dir) {
    if (!G.lock) G.lock = SDL_CreateMutex();
    if (G.running || !G.pick[0]) return;
    char python[1200], script[1200];
    SDL_LockMutex(G.lock);
    G.running = 1;
    G.finished = 0;
    G.done[0] = G.code[0] = G.error[0] = 0;
    SDL_UnlockMutex(G.lock);
    if (!setup_tool(exe_dir, "mod/import.py", python, script, sizeof python)) {
        set_finished(-1, "no_setup", "");
        return;
    }
    // the loose RomFS the manifest's original hashes come from: link mode's path, else the copied one
    char romfs[PLAT_PATH_MAX * 2];
    const char *linked = cfg_get("MKW_ROMFS");
    if (linked && *linked)
        snprintf(romfs, sizeof romfs, "%s", linked);
    else
        snprintf(romfs, sizeof romfs, "%s/romfs", exe_dir);
    const char *kind = G.pick_is_zip ? "--zip" : "--folder";
    const char *args[] = {python, "-I", script, "--game", exe_dir, "--romfs", romfs,
                          kind,      G.pick,   "--name", G.name, nullptr};
    if (!G.name[0]) args[9] = nullptr;
    rt_log("mods: %s %s --game %s %s %s%s%s", python, script, exe_dir, kind, G.pick,
           G.name[0] ? " --name " : "", G.name[0] ? G.name : "");
    G.proc = SDL_CreateProcess(args, true);
    if (!G.proc) {
        set_finished(-1, "", SDL_GetError());
        return;
    }
    SDL_DetachThread(SDL_CreateThread(reader, "mods-import", nullptr));
}
