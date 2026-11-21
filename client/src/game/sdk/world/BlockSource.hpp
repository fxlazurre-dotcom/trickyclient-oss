// ModCore — game SDK: BlockSource — block access for the local dimension.
// Discovery: the ClientInstance (then the Level) is scanned for an object
// whose vtable slot 2 == the resolved BlockSource::getBlock (the ka_find_gm
// technique). ABI VERIFIED by disasm: getBlock(BlockSource*, BlockPos{x@0,
// y@4, z@8}); int16 world bounds live at bs+0x38 (minY) / bs+0x3A (maxY).
#pragma once
#include <cstdint>

namespace mc {
namespace sdk {

// LevelChunk via BlockSource::getChunk — vtable slot 41 (reference client)
void* block_source_chunk(void* bs, int chunk_x, int chunk_z);
// find + cache the local dimension's BlockSource (vtable-validated)
void* block_source_find(void* level_or_null = nullptr);
// null-safe getBlock call; returns Block const* or nullptr
void* block_source_get(void* bs, int x, int y, int z);
// world Y bounds from the BlockSource header (VERIFIED offsets)
int block_source_min_y(void* bs);
int block_source_max_y(void* bs);

// Block -> BlockType*/BlockLegacy* (BDS candidate offset +0x68)
void* block_type_of(void* block);
// type -> full name ("minecraft:coal_ore"-style); cached per pointer; the
// string layout at +0xE8 is runtime-probed (pointer-to-cstr / MSVC SSO /
// MSVC heap) and the first discoveries are logged for verification
const char* block_type_name(void* type);

// ore classification by the numeric BlockLegacy id (reference-client ids,
// 1.26.x): coal 16/661, iron 15/656, diamond 56/660 (deepslate variants incl.)
enum : int {
    ORE_COAL = 0, ORE_IRON, ORE_COPPER, ORE_GOLD, ORE_REDSTONE,
    ORE_LAPIS, ORE_DIAMOND, ORE_EMERALD, ORE_NETHER,
    ORE_COUNT
};
int block_ore_kind(void* block_or_type);
uint16_t block_legacy_id(void* block_or_type);

void install_block_capture();     // hook the base getBlock, log real contexts
void block_source_latch(void* bs); // capture hook: adopt a heap view
// world (re)load: drop the cached BlockSource (called from the gm invalidation path)
void block_source_invalidate();

} // namespace sdk
} // namespace mc
