// Unlock everything: MKW_UNLOCK_ALL=1 owns every driver and costume, vehicle and sticker and
// reveals the Special Cup in the save the game opens (Play/account.sav), as a save editor does. Ported from
// Ca1nM/mario-kart-world-save-editor (MIT, mkw_core.py unlock_all / set_owned / reveal_special_cup at b12cbe6): each
// category is a length-prefixed list of owned ids, a parallel per-slot state array and, for drivers, one bit per owned
// slot; new items are appended with the state the game gives a new one. The arrays are found by key hash in the save's
// own table, so any game version; the offsets of game data Product.100 (1.0.0) are the fallback.
// fs_host.c edits the save's working copy when the game opens it: the game keeps the change only if it saves.
#include <stdio.h>
#include <string.h>

#include "cfg.h"
#include "hacks.h"
#include "host.h"
#include "unlock.h"

#define SAVE_MAGIC 0x01020304u
#define KEY_TABLE 0x20u
#define SAVE_SIZE_100 850928u
#define SPECIAL_CUP_SLOT 15u

typedef struct { uint32_t key_hash, offset_100; } Array;

typedef struct {
    const char *name;
    const uint32_t *ids;
    const int *n;
    Array list, state, mask;   // mask.key_hash 0: none
    uint32_t cap, new_state;
} Category;

static const Category cats[] = {
    {"drivers", unlock_driver_ids, &unlock_driver_ids_n,
     {0x44850554, 0x29800}, {0xED0C72BF, 0x683C}, {0xCE4D2DD9, 0x6A8}, 1000, 3},
    {"vehicles", unlock_vehicle_ids, &unlock_vehicle_ids_n,
     {0x1E849D5C, 0x63F28}, {0xF39B8041, 0x5704}, {0, 0}, 1000, 3},
    {"stickers", unlock_sticker_ids, &unlock_sticker_ids_n,
     {0xB8A3BA35, 0x2A7A4}, {0xE85996AD, 0x8E0}, {0, 0}, 5000, 1},
};
static const Array cup_progress = {0x55A6E00E, 0xF5B0};
static const Array cup_open_in_gp = {0x9A164573, 0x72C};   // in 1.0.0's table too (50 slots, a 1.0.0 save)

typedef struct { uint32_t list, state, mask, cap; } Located;

static uint32_t rd(const uint8_t *b, uint32_t off) {
    uint32_t v;
    memcpy(&v, b + off, 4);
    return v;
}
static void wr(uint8_t *b, uint32_t off, uint32_t v) { memcpy(b + off, &v, 4); }

// Offset of the array's first element, its slot count in *cap; 0 when the save does not have it.
static uint32_t locate_array(const uint8_t *b, uint64_t n, const Array *a, uint32_t *cap) {
    uint32_t count_at = 0;
    if (n >= KEY_TABLE + 8 && rd(b, 0) == SAVE_MAGIC) {
        uint32_t table_end = rd(b, 8);
        if (table_end <= n && table_end % 8 == 0)
            for (uint32_t e = KEY_TABLE; e + 8 <= table_end && !count_at; e += 8)
                if (rd(b, e) == a->key_hash) count_at = rd(b, e + 4);
    }
    if (!count_at && n == SAVE_SIZE_100 && a->offset_100) count_at = a->offset_100 - 4;
    if (!count_at || count_at % 4 || (uint64_t)count_at + 4 > n) return 0;
    *cap = rd(b, count_at);
    if ((uint64_t)count_at + 4 + 4ull * *cap > n) return 0;
    return count_at + 4;
}

static int locate_category(const uint8_t *b, uint64_t n, const Category *c, Located *at) {
    uint32_t cap = 0;
    if (!(at->list = locate_array(b, n, &c->list, &cap)) || cap != c->cap) return 0;
    if (!(at->state = locate_array(b, n, &c->state, &cap)) || cap != c->cap) return 0;
    at->mask = 0;
    if (c->mask.key_hash && (!(at->mask = locate_array(b, n, &c->mask, &cap)) || cap != c->cap)) return 0;
    at->cap = c->cap;
    return 1;
}

// Appends the ids the list lacks; returns how many.
static int own_all(uint8_t *b, const Category *c, const Located *at) {
    uint32_t have = 0;
    while (have < at->cap && rd(b, at->list + 4 * have)) have++;
    int added = 0;
    for (int i = 0; i < *c->n && have < at->cap; i++) {
        uint32_t id = c->ids[i], k = 0;
        while (k < have && rd(b, at->list + 4 * k) != id) k++;
        if (k < have) continue;
        wr(b, at->list + 4 * have, id);
        wr(b, at->state + 4 * have, c->new_state);
        if (at->mask) wr(b, at->mask + 4 * (have / 32), rd(b, at->mask + 4 * (have / 32)) | 1u << (have % 32));
        have++, added++;
    }
    return added;
}

int unlock_save(uint8_t *b, uint64_t n) {
    Located at[sizeof cats / sizeof *cats];
    uint32_t progress = 0, cup_slots = 0;
    int ok = n <= UINT32_MAX && (progress = locate_array(b, n, &cup_progress, &cup_slots)) &&
             cup_slots > SPECIAL_CUP_SLOT;
    for (size_t k = 0; ok && k < sizeof cats / sizeof *cats; k++) ok = locate_category(b, n, &cats[k], &at[k]);
    if (!ok) {
        rt_log("unlock: account.sav (%llu bytes) is not a layout this knows: left as it is", (unsigned long long)n);
        return 0;
    }
    char note[160];
    int pos = 0, total = 0;
    for (size_t k = 0; k < sizeof cats / sizeof *cats; k++) {
        int a = own_all(b, &cats[k], &at[k]);
        total += a;
        pos += snprintf(note + pos, sizeof note - (size_t)pos, "%s%s +%d", k ? "," : "", cats[k].name, a);
    }
    uint32_t sc = progress + 4 * SPECIAL_CUP_SLOT;
    if (!rd(b, sc)) wr(b, sc, 2), total++;
    uint32_t flag_slots = 0, flags = locate_array(b, n, &cup_open_in_gp, &flag_slots);
    uint32_t special_bit = 1u << SPECIAL_CUP_SLOT;
    if (flags && flag_slots > SPECIAL_CUP_SLOT && !(rd(b, flags) & special_bit))
        wr(b, flags, rd(b, flags) | special_bit), total++;
    rt_log("unlock: %s, Special Cup open (MKW_UNLOCK_ALL)", note);
    return total;
}
