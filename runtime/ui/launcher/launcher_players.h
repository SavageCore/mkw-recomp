// The start screen's players (ui/launcher/launcher_players.cpp): the Profiles page and the profile row of the main
// page. The folders and names are core/base/players.h; the one the next start plays is mkw.ini's MKW_PLAYER.
#pragma once
#include <stddef.h>

void launcher_players_set_base(const char *base);    // the profile folder the players live in (main.c)
const char *launcher_player_name(void);              // the name of the player the next start plays
void launcher_player_dir(char *out, size_t cap);     // and its folder; "" when no profile folder is known
// The page's body, inside the page's window (launcher.cpp panel_begin), leaving bottom pixels for its Back row.
void launcher_players_page(float scale, float bottom);
