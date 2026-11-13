// Default memory caps from the player's GPU and RAM (vk_defaults.h). A figure measured on the Radeon 660M is a floor or
// the integrated case, never everyone's cap: 768 MiB of tile slots on every GPU left an RTX 4070 SUPER's ground flat
// (#31).
#include "vk_defaults.h"

// Tile slots of the sparse textures (vk_sparse.c). Integrated: 768, the 660M's (1024 hung it with MKW_ASTC_BC1,
// run-race41). Discrete: a quarter of the VRAM in 256 MiB steps, 768 to 3072; the title binds 1 472 MiB uncapped
// (run-sp3), so 8 GB and up hold all of it and a 4 GB card keeps the 660M's figure.
uint64_t vk_default_sparse_mib(int discrete, uint64_t vram_mib) {
    if (!discrete) return 768;
    uint64_t mib = vram_mib / 4 / 256 * 256;
    return mib < 768 ? 768 : mib > 3072 ? 3072 : mib;
}

// What VMA may allocate on an integrated GPU (vk_device.c), system RAM outside the process's cgroup: 4096 at least
// (3072 makes the race black, sp-v0), else 40% of the RAM in 256 MiB steps (a quarter left a 16 GB Iris Xe at 4096,
// which ran out at a time trial's three ~8K textures; 6144 went through, #174), plus the GPU's carve-out,
// memory the firmware gives it that is not the system's RAM. Without it a ROG Ally X with 8 GB carved out got 2 730 MiB
// of its 10 923 MiB device-local heap, filled it, and hung right after (issue #50).
uint64_t vk_default_mem_limit_mib(uint64_t ram_mib, uint64_t carveout_mib) {
    uint64_t mib = ram_mib * 2 / 5 / 256 * 256;
    return (mib > 4096 ? mib : 4096) + carveout_mib;
}

// Texture pool blocks made at start (vk_tex_image.c, MKW_VK_TEX_POOL_MIN). Discrete only. Half the VRAM, in 256 MiB
// blocks, up to 8 (2 GiB): 8 blocks took the two long frames after «VALE» on the GTX 1050 Ti from 2 642 + 2 471 to
// 1 661 + 1 476 ms (wtp8 / wtp0), and the pool grows past them on demand, so the cap costs only the reserve. Half a
// 16 GB card (31 blocks, 7 750 MiB) was 92% of the runtime's own budget there (8 386 MiB, "heap 1 limited" in a
// 9070 XT's log): the title screen reached 8 224 of 8 667 MiB and lost 19 sparse tile slots (issue #43, 10/11/2026),
// the memory pressure behind a device loss. Integrated GPUs keep no pool (the path was only ever measured on discrete
// ones).
uint32_t vk_default_tex_pool_blocks(int discrete, uint64_t vram_mib) {
    if (!discrete || !vram_mib) return 0;
    uint64_t blocks = (vram_mib + 511) / 512;   // half the VRAM, rounded up to a block
    return (uint32_t)(blocks > 8 ? 8 : blocks);
}
