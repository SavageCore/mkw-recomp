// Shared by the system overrides: runtime/ov_system.c, ov_input.c, ov_media.c and ov_gfx.c, and their probe.
// oe_push_message, oe_set_focus and ov_nv_state are in gfx.h (the window posts focus messages, the NVN layer
// allocates through the recorded callbacks).
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"

// ---- ov_system.c
int ov_handheld(void);                     // MKW_OPERATION_MODE=handheld (default docked); fatal on another value
const char *ov_language(void);             // the game's language, a Switch language code ("en-US", "zh-Hans", ...)
// A language the game does not ship whose texts are rebuilt over ov_language() ("ca", "gl", "eu"), else NULL:
// MKW_LANG names it, or unset, the locale's language is one
const char *ov_text_language(void);
const char *ov_shared_font_dir(char *buf, size_t n);   // MKW_SHARED_FONTS, else <romfs>/../../firmware/shared-fonts
uint64_t ov_bfttf_size(const uint8_t *p, uint64_t len); // decoded font size of a BFTTF image, 0 if it is not one

// ---- ov_socket.c
int ov_lan(void);                                    // MKW_LAN set: sockets and nifm's local mode are live
int ov_lan_only(uint32_t host_order);   // 1 for an IPv4 address that never leaves a local network (ov_socket.c)
int ov_lan_address(uint8_t ip[4], uint8_t mask[4]);   // MKW_LAN_IP, else the first interface up with a local address; -1 with none
