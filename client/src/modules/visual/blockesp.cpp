// ModCore — module: BlockESP. Full-Y column scan through BlockSource::getBlock
// (budgeted, world-unload gated), ore classification by the numeric
// BlockLegacy id, and overlay rendering with the ENGINE frame transform
// (view matrix + camera pos + fov — exact projection, Solstice pipeline).
#include "modules/registry.h"
#include "game/sdk/SDK.hpp"
#include "game/sdk/actor/LocalPlayer.hpp"
#include "game/sdk/actor/components.h"
#include "game/sdk/world/BlockSource.hpp"
#include "game/sdk/render/LevelRenderer.hpp"
#include "utils/RenderUtil.hpp"
#include "core/log.h"
#include "core/state.h"

#include <windows.h>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cstdint>

namespace mc {

using sdk::StateVectorC;
using sdk::ORE_COAL;
using sdk::ORE_IRON;
using sdk::ORE_COPPER;
using sdk::ORE_GOLD;
using sdk::ORE_REDSTONE;
using sdk::ORE_LAPIS;
using sdk::ORE_DIAMOND;
using sdk::ORE_EMERALD;
using sdk::ORE_NETHER;

static void ore_color(int kind, float& r, float& g, float& b) {
    switch (kind) {
        case ORE_COAL:     r = 0x62 / 255.f; g = 0x6C / 255.f; b = 0x78 / 255.f; break;
        case ORE_IRON:     r = 0xD8 / 255.f; g = 0xAF / 255.f; b = 0x93 / 255.f; break;
        case ORE_COPPER:   r = 0xE0 / 255.f; g = 0x7B / 255.f; b = 0x39 / 255.f; break;
        case ORE_GOLD:     r = 0xF6 / 255.f; g = 0xD3 / 255.f; b = 0x3C / 255.f; break;
        case ORE_REDSTONE: r = 0xE0 / 255.f; g = 0x22 / 255.f; b = 0x22 / 255.f; break;
        case ORE_LAPIS:    r = 0x2A / 255.f; g = 0x4F / 255.f; b = 0xD0 / 255.f; break;
        case ORE_DIAMOND:  r = 0x4A / 255.f; g = 0xED / 255.f; b = 0xD9 / 255.f; break;
        case ORE_EMERALD:  r = 0x17 / 255.f; g = 0xDD / 255.f; b = 0x62 / 255.f; break;
        case ORE_NETHER:   r = 0xD9 / 255.f; g = 0xC9 / 255.f; b = 0xA8 / 255.f; break;
        default:          r = 1.f; g = 1.f; b = 1.f; break;
    }
}

class BlockESP : public Module {
public:
    BlockESP() {
        name = "BlockESP"; category = "VISUAL";
        // XZ radius only — Y always spans the whole world column
        add_float("radius", 16.f, 4.f, 48.f, false, "%.0f");
        add_bool("coal", false);       // spammy — opt-in
        add_bool("iron", false);       // spammy — opt-in
        add_bool("copper", false);     // spammy — opt-in
        add_bool("gold", true);
        add_bool("redstone", false);   // spammy — opt-in
        add_bool("lapis", true);
        add_bool("diamond", true);
        add_bool("emerald", true);
        add_bool("nether", true);      // quartz + ancient debris
    }

    void on_tick(LevelCtx& ctx) override {
        // Level::tick fires on TWO threads (client + server level) — one sweeper
        if (InterlockedCompareExchange(&m_scan_busy, 1, 0)) return;
        // HARD GATE: no validated live player == world is unloading/reloading
        if (!ctx.player || !sdk::local_player_valid()) { m_scan_busy = 0; return; }
        // GRACE after world load: chunk tables are MUTATED by the loader
        // threads for the first seconds — walking them lock-free races page
        // reallocation inside getBlock. Let the load storm settle.
        if (GetTickCount64() - sdk::player_loaded_ms() < 4000) { m_scan_busy = 0; return; }
        void* bs = sdk::block_source_find(ctx.level);
        if (!bs) { m_scan_busy = 0; return; }
        StateVectorC* psv = sdk::actor_statevec(ctx.player);
        if (!psv) { m_scan_busy = 0; return; }

        // ---- COLUMN CACHE SCANNER ----
        // Each XZ column is scanned ONCE and then cached; ores do not move,
        // only mining changes them, so a column is re-scanned at most every
        // kColRefreshMs (staggered naturally by the ring order). Steady
        // state costs ~1/10th of the old periodic full pass, and walking
        // into new territory scans only the NEW columns (nearest first).
        const float radius = sets[0].f;
        const int R = (int)radius;
        const int col_lim = (R + 1) * (R + 1);
        const float r2 = radius * radius;
        int pcx = (int)floorf(psv->px);
        int pcz = (int)floorf(psv->pz);
        const int y0 = sdk::block_source_min_y(bs);
        const int y1 = sdk::block_source_max_y(bs);
        if (y1 <= y0) { m_scan_busy = 0; return; }
        const int yspan = y1 - y0 + 1;

        static const std::vector<int16_t>& offs = [] {
            // all offsets up to the max radius (48), sorted NEAREST FIRST
            std::vector<std::pair<int, int>> p;
            for (int dx = -48; dx <= 48; dx++)
                for (int dz = -48; dz <= 48; dz++) p.push_back({ dx, dz });
            std::sort(p.begin(), p.end(), [](const auto& a, const auto& b) {
                return a.first * a.first + a.second * a.second <
                       b.first * b.first + b.second * b.second; });
            std::vector<int16_t> f;
            f.reserve(p.size() * 2);
            for (auto& q : p) { f.push_back((int16_t)q.first); f.push_back((int16_t)q.second); }
            return f;
        }();

        unsigned long long now = GetTickCount64();
        int budget = 4096;                        // getBlock calls this tick
        int scanned = 0, st_ids = 0;
        bool changed = false;
        uint64_t tick_keys[16];
        int nkeys = 0;
        for (size_t oi = 0; oi + 1 < offs.size() && budget >= yspan; oi += 2) {
            int dx = offs[oi], dz = offs[oi + 1];
            int d2 = dx * dx + dz * dz;
            if (d2 > col_lim) break;                         // sorted: rest is farther
            uint64_t key = ((uint64_t)(uint32_t)(pcx + dx) << 32) | (uint32_t)(pcz + dz);
            auto it = m_cols.find(key);
            if (it != m_cols.end()) {
                // NEAR columns (<=20 blocks) refresh slowly to catch mining;
                // FAR columns stay cached as long as they are in radius —
                // invisible ore costs nothing until you walk up to it.
                if (d2 > 400) continue;
                if (now - it->second.ms < 45000ull) continue;
            }
            // scan the column top-down
            std::vector<Box> col;
            col.reserve(8);
            for (int dyi = 0; dyi < yspan; dyi++) {
                if ((dyi & 1023) == 1023 && !sdk::local_player_valid()) {
                    m_scan_busy = 0;                         // world died mid-column
                    return;
                }
                int y = y1 - dyi;
                void* blk = sdk::block_source_get(bs, pcx + dx, y, pcz + dz);
                if (!blk) continue;
                if (sdk::block_legacy_id(blk) == 0xFFFF) continue;
                st_ids++;
                int kind = sdk::block_ore_kind(blk);
                if (kind < 0) continue;
                if (!sets[1 + kind].b) continue;
                float ddx = (float)(pcx + dx) - psv->px;
                float ddy = (float)y - psv->py;
                float ddz = (float)(pcz + dz) - psv->pz;
                if (ddx * ddx + ddy * ddy + ddz * ddz > r2) continue;
                col.push_back({ (float)(pcx + dx), (float)y, (float)(pcz + dz), (uint8_t)kind });
            }
            bool same = it != m_cols.end() &&
                        it->second.boxes.size() == col.size() &&
                        (col.empty() || !memcmp(it->second.boxes.data(), col.data(),
                                               col.size() * sizeof(Box)));
            if (it != m_cols.end()) { it->second.boxes = std::move(col); it->second.ms = now; }
            else m_cols.emplace(key, ColEntry{ std::move(col), now });
            if (nkeys < 16) tick_keys[nkeys++] = key;
            scanned++;
            if (!same) changed = true;         // unchanged column -> no republish
            budget -= yspan;
        }

        // empty-view guard: columns came back with ZERO valid blocks — the
        // view is dead, do not poison the cache, retry silently
        if (scanned > 0 && st_ids == 0) {
            for (int i = 0; i < nkeys; i++) m_cols.erase(tick_keys[i]);
            static unsigned long long s_zero_warn = 0;
            if (now - s_zero_warn > 10000) {
                s_zero_warn = now;
                MC_WARN("blockesp: scan saw 0 valid blocks — waiting for a valid view");
            }
            m_scan_busy = 0;
            return;
        }

        // expire columns that left the radius (throttled to every 2 s)
        if (now - m_expire_ms > 2000) {
            m_expire_ms = now;
            const int far_lim = (R + 4) * (R + 4);
            for (auto it = m_cols.begin(); it != m_cols.end(); ) {
                int ddx = (int32_t)(it->first >> 32) - pcx;
                int ddz = (int32_t)(it->first & 0xFFFFFFFF) - pcz;
                if (ddx * ddx + ddz * ddz > far_lim) { it = m_cols.erase(it); changed = true; }
                else ++it;
            }
        }

        if (changed) {
            m_building.clear();
            for (auto& kv : m_cols)
                for (const Box& b : kv.second.boxes) {
                    if (m_building.size() >= 8192) goto cap;
                    m_building.push_back(b);
                }
            cap:
            // STABLE order: nearest first. unordered_map rehashes shuffle the
            // concat order — with the draw cap that flickered random boxes in
            // and out. Sorting pins the cap to always cut the farthest tail.
            {
                const float fx = psv->px, fy = psv->py, fz = psv->pz;
                std::sort(m_building.begin(), m_building.end(),
                          [fx, fy, fz](const Box& a, const Box& b) {
                              float ax = a.x - fx, ay = a.y - fy, az = a.z - fz;
                              float bx = b.x - fx, by = b.y - fy, bz = b.z - fz;
                              return ax * ax + ay * ay + az * az < bx * bx + by * by + bz * bz;
                          });
            }
            {
                std::lock_guard<std::mutex> g(m_mtx);
                m_boxes.swap(m_building);
                m_boxes_count = (int)m_boxes.size();
            }
        }
        static unsigned long long s_stat = 0;
        if (now - s_stat > 60000) {
            s_stat = now;
            MC_LOG("blockesp: cache %d col(s) -> %d box(es)", (int)m_cols.size(), m_boxes_count);
        }
        m_scan_busy = 0;
    }

    // OVERLAY rendering with the ENGINE frame transform (view matrix + camera
    // pos + fov captured by the setupAndRender hook): exact projection —
    // no yaw/pitch math, no calibration, no drift.
    void on_hud(HudCtx& c) override {
        std::vector<Box> snap;
        {
            std::lock_guard<std::mutex> g(m_mtx);
            if (m_boxes_count == 0) return;
            snap = m_boxes;
        }
        render::ViewTransform vt = sdk::render_frame_transform();
        if (!vt.valid) return;

        const float radius = sets[0].f;
        const float mX = c.W * 0.5f, mY = c.H * 0.5f;
        const float* m = vt.m;
        const float f0 = mX * vt.fov[0], f1 = mY * vt.fov[1];
        const float zspread = 0.5f * (fabsf(m[8]) + fabsf(m[9]) + fabsf(m[10]));
        int drawn = 0;
        for (const Box& b : snap) {
            if (drawn >= 1500) break;                 // overlay draw cap (FPS)
            float px = b.x - vt.origin[0], py = b.y - vt.origin[1], pz = b.z - vt.origin[2];
            float dist2 = px * px + py * py + pz * pz;
            if (dist2 > radius * radius) continue;
            float dist = sqrtf(dist2);

            // SHARED SUBEXPRESSION projection: corner(dx,dy,dz) row value is
            // C + dx*a + dy*b + dz*c — the box center transform is computed
            // ONCE (12 mul) instead of 8 full 4x4 multiplies (~10x cheaper).
            float Cx = m[0] * px + m[1] * py + m[2] * pz + m[3];
            float Cy = m[4] * px + m[5] * py + m[6] * pz + m[7];
            float Cz = m[8] * px + m[9] * py + m[10] * pz + m[11];
            float zc = Cz + 0.5f * (m[8] + m[9] + m[10]);
            if (zc - zspread > 0.f) continue;         // all 8 corners behind

            float cx[8], cy[8];
            bool ok[8];
            int vis = 0;
            float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
            for (int i = 0; i < 8; i++) {
                float ix = (i & 1) ? 1.f : 0.f, iy = (i & 2) ? 1.f : 0.f, iz = (i & 4) ? 1.f : 0.f;
                float Z = Cz + ix * m[8] + iy * m[9] + iz * m[10];
                float nz = -Z;
                if (nz < 1e-6f) { ok[i] = false; continue; }
                float X = Cx + ix * m[0] + iy * m[1] + iz * m[2];
                float Y = Cy + ix * m[4] + iy * m[5] + iz * m[6];
                float sx = mX + f0 * X / nz;
                float sy = mY - f1 * Y / nz;
                if (sx < -c.W * 2.f || sx > c.W * 3.f || sy < -c.H * 2.f || sy > c.H * 3.f) {
                    ok[i] = false;                    // way offscreen — clip the edge
                    continue;
                }
                cx[i] = sx; cy[i] = sy; ok[i] = true; vis++;
                if (sx < minx) minx = sx;
                if (sx > maxx) maxx = sx;
                if (sy < miny) miny = sy;
                if (sy > maxy) maxy = sy;
            }
            if (!vis) continue;
            if (maxx - minx < 2.f && maxy - miny < 2.f) continue;   // subpixel: invisible
            drawn++;

            float fade = 1.f - (dist / radius) * 0.55f;
            float r, g, bl;
            ore_color(b.kind, r, g, bl);
            unsigned col = c.with_a(c, rgb_pack(r, g, bl, 1.f), 0.9f * fade);

            if (maxx - minx < 14.f && maxy - miny < 14.f) {
                // LOD: tiny box — a single diagonal cross instead of 12 edges
                int a = -1, b2 = -1;
                for (int i = 0; i < 8 && a < 0; i++) if (ok[i]) a = i;
                for (int i = 7; i >= 0 && b2 < 0; i--) if (ok[i]) b2 = i;
                if (a >= 0 && b2 >= 0 && a != b2) {
                    c.line(c, cx[a], cy[a], cx[b2], cy[b2], col, 1.2f);
                    int e0 = ok[0] ? 0 : a, e1 = ok[7] ? 7 : b2;
                    if (e0 != e1 && e0 != a && e1 != b2)
                        c.line(c, cx[e0], cy[e0], cx[e1], cy[e1], col, 1.2f);
                }
                continue;
            }
            static const int edges[12][2] = {
                {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
            for (auto& e : edges)
                if (ok[e[0]] && ok[e[1]])
                    c.line(c, cx[e[0]], cy[e[0]], cx[e[1]], cy[e[1]], col, 1.2f);
        }
    }

private:
    struct Box { float x, y, z; uint8_t kind; };
    std::mutex m_mtx;
    std::vector<Box> m_boxes;             // published (render thread reads)
    std::vector<Box> m_building;          // scan in progress
    int m_boxes_count = 0;
    struct ColEntry { std::vector<Box> boxes; unsigned long long ms; };
    std::unordered_map<uint64_t, ColEntry> m_cols;   // XZ column cache
    unsigned long long m_expire_ms = 0;
    volatile LONG m_scan_busy = 0;

    static unsigned rgb_pack(float r, float g, float b, float a) {
        return ((unsigned)(a * 255.f + 0.5f) << 24) | ((unsigned)(b * 255.f + 0.5f) << 16) |
               ((unsigned)(g * 255.f + 0.5f) << 8) | (unsigned)(r * 255.f + 0.5f);
    }
};

Module& mod_blockesp() { static BlockESP m; return m; }

} // namespace mc
