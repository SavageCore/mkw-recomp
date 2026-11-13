#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "host.h"
#include "vk.h"
#include "vk_mem_internal.h"

// ---------------------------------------------------------------- shadow storage on non-coherent memory
// The shadow (vk_mem_shadow.c) is read and written by the CPU every frame, so it wants HOST_CACHED memory. Some drivers
// have no type that is both cached and coherent: ANV on Meteor Lake (Mesa 25.3.6 and 26.0.8, #283 and #289) offers
// HOST_VISIBLE | HOST_COHERENT (write-combined, uncached) or HOST_VISIBLE | HOST_CACHED. Requiring coherent put the
// shadow in uncached memory there: the copy-ins and merge bases ran at ~0.25 GB/s, the title at ~6 fps (#283), against
// ~5 GB/s on an Iris Xe with Mesa 26.2.6, which has a cached coherent type. Where none exists, the shadow takes the
// cached non-coherent type, and the CPU flushes what it writes before the GPU reads it and invalidates what the GPU
// wrote before reading it (shadow_flush, shadow_inval at the copy-ins, merges and write-backs).
// MKW_SHADOW_CACHED: auto (default: non-coherent only without a cached coherent type), 0 (coherent, as before), 1
// (cached, coherent or not; the flush and invalidate calls run on every shadow, a no-op in VMA on coherent memory).

static int nc_mode = -1;   // 0 coherent required, 1 cached required

static int pick_mode(const VkCtx *c) {
    const char *e = cfg_get("MKW_SHADOW_CACHED");
    if (e && *e && strcmp(e, "auto") != 0) return atoi(e) != 0 ? 1 : 0;
    const VkPhysicalDeviceMemoryProperties *mp;
    vmaGetMemoryProperties(c->vma, &mp);
    const VkMemoryPropertyFlags want =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    int cached = 0;
    for (uint32_t i = 0; i < mp->memoryTypeCount; i++) {
        VkMemoryPropertyFlags f = mp->memoryTypes[i].propertyFlags;
        if ((f & want) == want) return 0;
        cached |= (f & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT)) ==
                  (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    }
    return cached;
}

void shadow_alloc_flags(VkCtx *c, VmaAllocationCreateInfo *ai) {
    if (nc_mode < 0) {
        nc_mode = pick_mode(c);
        rt_log("mem: shadow storage in %s memory (MKW_SHADOW_CACHED)",
               nc_mode ? "cached (not necessarily coherent: explicit flush and invalidate)" : "coherent, cached if it has it");
    }
    ai->requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        (nc_mode ? VK_MEMORY_PROPERTY_HOST_CACHED_BIT : VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    ai->preferredFlags = nc_mode ? VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
}

void shadow_alloc_note(VkCtx *c, PoolRec *r) {
    VkMemoryPropertyFlags f = 0;
    vmaGetAllocationMemoryProperties(c->vma, r->salloc, &f);
    r->snc = nc_mode == 1 || !(f & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    static int logged;
    if (r->snc && !(f & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) && !logged++)
        rt_log("mem: shadow memory flags %#x: not coherent, flushed and invalidated by range", (unsigned)f);
}

// VMA rounds the range to nonCoherentAtomSize and skips coherent memory.
void shadow_flush(const PoolRec *r, uint64_t lo, uint64_t hi) {
    if (r->snc && hi > lo) vmaFlushAllocation(vk_ctx()->vma, r->salloc, lo - r->import_lo, hi - lo);
}

void shadow_inval(const PoolRec *r, uint64_t lo, uint64_t hi) {
    if (r->snc && hi > lo) vmaInvalidateAllocation(vk_ctx()->vma, r->salloc, lo - r->import_lo, hi - lo);
}
