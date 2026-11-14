// How the last game run ended (ui/launcher/launcher_lastrun.cpp): tools/prepare/lastrun.py reads its log against the
// known errors the issue bot answers with, so the start screen can say "a known problem, followed in #N" or "fixed in
// vX" instead of the player opening another issue (14/11/2026: 40-68 issues a day, 15 % known already).
#pragma once

enum LastRunKind { LASTRUN_NONE, LASTRUN_KNOWN, LASTRUN_FIXED, LASTRUN_CRASH };

typedef struct {
    int kind;        // LastRunKind; NONE while it is read, and for a clean or silent end
    int issue;       // KNOWN, FIXED: the issue that follows it
    char tag[32];    // FIXED: the release that fixes it, as "v0.1.11"
} LastRun;

void lastrun_start(const char *exe_dir, const char *game_dir);   // at start; nothing without the package's tools
LastRun lastrun_state(void);                                      // a copy, any time
void lastrun_dismiss(void);                                       // the player closed the notice
