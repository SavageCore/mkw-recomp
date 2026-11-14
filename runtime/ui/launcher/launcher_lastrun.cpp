// The last run's end (launcher_lastrun.h): tools/prepare/lastrun.py in the package's Python, its one line read by a
// thread of ours, as launcher_update.cpp reads update.py. The log it reads is the previous game run's: the next Play
// moves it to mkw.1.log (core/boot/main.c log_open), and the start screen alone writes no log file.
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui/launcher/launcher_lastrun.h"
#include "ui/launcher/launcher_setup.h"

extern "C" {
void rt_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
extern const char rt_port_version[];
}

static SDL_Mutex *g_lock;
static LastRun g_state;
static SDL_Process *g_proc;

static void on_line(const char *line) {
    LastRun r = {};
    char tag[32] = "";
    if (sscanf(line, "known %d", &r.issue) == 1) r.kind = LASTRUN_KNOWN;
    else if (sscanf(line, "fixed %d %31s", &r.issue, tag) == 2) r.kind = LASTRUN_FIXED;
    else if (!strcmp(line, "crash")) r.kind = LASTRUN_CRASH;
    snprintf(r.tag, sizeof r.tag, "%s", tag);
    rt_log("launcher: last run: %s", *line ? line : "(nothing to say)");
    SDL_LockMutex(g_lock);
    g_state = r;
    SDL_UnlockMutex(g_lock);
}

static int SDLCALL reader(void *) {
    size_t n = 0;
    char *out = (char *)SDL_ReadProcess(g_proc, &n, nullptr);   // one short line: waits for the script to end
    if (out) {
        char *nl = strpbrk(out, "\r\n");
        if (nl) *nl = 0;
        on_line(out);
        SDL_free(out);
    }
    SDL_DestroyProcess(g_proc);
    g_proc = nullptr;
    return 0;
}

void lastrun_start(const char *exe_dir, const char *game_dir) {
    if (!g_lock) g_lock = SDL_CreateMutex();
    char python[1200], script[1200];
    if (!game_dir || !*game_dir || !setup_tool(exe_dir, "prepare/lastrun.py", python, script, sizeof python)) return;
    const char *args[] = {python, "-I", script, "--game", game_dir, "--version", rt_port_version, nullptr};
    g_proc = SDL_CreateProcess(args, true);
    if (!g_proc) {
        rt_log("launcher: cannot read the last run: %s", SDL_GetError());
        return;
    }
    SDL_DetachThread(SDL_CreateThread(reader, "lastrun", nullptr));
}

LastRun lastrun_state(void) {
    if (!g_lock) return g_state;
    SDL_LockMutex(g_lock);
    LastRun r = g_state;
    SDL_UnlockMutex(g_lock);
    return r;
}

void lastrun_dismiss(void) {
    if (!g_lock) return;
    SDL_LockMutex(g_lock);
    g_state = LastRun{};
    SDL_UnlockMutex(g_lock);
}
