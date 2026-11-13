// Internal interface of the op-stream replay core, split by responsibility: vk_exec.c (state, passes and barriers, the
// op loop), vk_exec_rt.c (render targets, their views and history) and vk_exec_clear.c (clears). Everything else goes
// through vk_exec.h.
#pragma once
#include <stdint.h>

#include "vk.h"
#include "vk_exec.h"

// Data the parts share is hidden: accessed directly (TLS: local-exec), as when it was all one file.
#ifdef __ELF__
#define EXEC_SHARED __attribute__((visibility("hidden")))
#else
#define EXEC_SHARED
#endif

enum { W_LEVEL, W_LAYER, W_VIEW_FORMAT, W_BLOCK_VIEW, W_BLOCK_LAYERS, W_CLEAR_TARGET, W_CLEAR_MASK, W_STENCIL_MASK, W_COUNT };
void exec_warn_once(int key, const char *fmt, ...);   // vk_exec.c

extern int pt_on EXEC_SHARED;   // MKW_PASS_TIMING (vk_passtime.c)
typedef struct {   // MKW_DEPTH_CLEAR_ALL (dc_layered); per recording thread (MKW_REPLAY_PAR)
    VkImage img;
    uint32_t pending;   // layers cleared ahead and untouched since; dropped (dc_drop) when something may write them
    float depth;
    const char *dropped_by;   // what dropped pending last (the mid-sequence log)
    // Per image, the layers whose clear came (made or skipped) since its last clear of every layer. Unlike pending it
    // outlives submissions, copies and queue switches: a clear of a layer not in it while others are continues the
    // sequence, and clearing every layer there would wipe the layers already drawn (the kart's shadow, 08/11/2026).
    struct { VkImage img; uint32_t done; } seq[4];
    // A MKW_REPLAY_PAR chunk: the depth images earlier chunks bound (nseen -1: any). Which of their layers an earlier
    // chunk cleared ahead or drew into is not known here, and clearing every layer would wipe what it drew (car shadows
    // flickered, 07/11/2026): those clear layer by layer. An image no earlier chunk bound is as in the serial replay.
    const VkImage *seen;
    int nseen;
} ExecDC;
extern _Thread_local ExecDC DC EXEC_SHARED;
static inline void dc_drop(const char *why) {
    if (DC.pending) DC.pending = 0, DC.dropped_by = why;
}

// Summed over the recording threads: samplings of a layer cleared ahead, clears skipped, clears of every layer, and
// clears of one layer that a chunk made layer by layer (an earlier chunk bound the image).
extern uint64_t dc_cnt[4] EXEC_SHARED;

static inline int dc_seen(VkImage img) {
    if (DC.nseen < 0) return 1;
    for (int i = 0; i < DC.nseen; i++)
        if (DC.seen[i] == img) return 1;
    return 0;
}

extern int dc_mode EXEC_SHARED;   // MKW_DEPTH_CLEAR_ALL as read (vk_exec_clear.c dc_layered); -1 not read yet

// vk_exec_rt.c
void resolve_rt(VkExec *ex);
VkImageView view_of(VkExec *ex, const VkExecTarget *t);
void views_release(VkExec *ex);
void tfr_note(const VkExecTarget *t, uint64_t stamp);
int tfr_recent(const VkExecTarget *t, uint64_t stamp);   // rendered in one of the frames just before stamp
// vk_exec_clear.c
void clear_color(VkExec *ex, const p_clear_color *c);
void clear_ds(VkExec *ex, const p_clear_ds *c);
