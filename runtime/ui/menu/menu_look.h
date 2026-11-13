// The look the in-game menu and the start screen share (ui/menu/menu_look.cpp): colours, sizes and fonts. ImGui's thread only.
#pragma once
#include <stdint.h>

#include "imgui.h"

float menu_look_scale(uint32_t height);    // sizes are tuned for 1080 lines; under 720 lines they stay at the 720 size
void menu_look_style(ImGuiStyle &s);       // at scale 1: callers ScaleAllSizes
void menu_look_fonts(float scale);         // the atlas's fonts at that scale; the caller builds the texture
int menu_look_fonts_stale(void);           // the menu language changed since: menu_look_fonts again
ImFont *menu_look_font_at(float px);       // after menu_look_fonts: Roboto at that size, in the same atlas
                                           // (titles), with the system font of a CJK or Thai menu language
