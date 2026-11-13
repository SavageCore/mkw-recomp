// The Mods page's GameBanana tab (mods_browse.cpp): list, search, a mod's page and its install.
#pragma once

struct SDL_Renderer;

// Draws the tab on the UI thread; 1 when an install's download finished and its import started (the page then shows
// the installed list, where the import's progress is).
int mods_browse_draw(const char *exe_dir, SDL_Renderer *ren);
void mods_browse_open(int mod_id, int file_id);   // a 1-Click link: that mod's page, and the install of that file asked   // ren: the preview pictures' textures
