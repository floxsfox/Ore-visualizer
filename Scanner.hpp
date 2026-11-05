#pragma once

// Logika murni (tanpa dependensi engine): klasifikasi nama block, pembuatan
// garis outline dari grid voxel, easing animasi pop, dan geometri garis tebal.
// Semua bagian di sini dites di host.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace vo {

// Fitur yang bisa dipilih di menu (satu toggle per fitur).
enum Feature : uint8_t {
    FCoal, FIron, FCopper, FGold, FRedstone, FLapis, FDiamond, FEmerald,
    FQuartz, FDebris, FBrick, FLava, FWater, FSand, FGravel, FChest,
    kFeatureCount
};

struct ClassDef {
    uint32_t rgb1;   // level 1: warna bawaan
    uint32_t rgb2;   // level 2: lebih terang (malam hari)
    Feature feature; // toggle yang mengatur kelas ini
    bool pop;        // dapat animasi pop saat baru terdeteksi
    bool box;        // true = kotak per block (chest), false = outline region menyatu
};

constexpr int kClassCount = 20;

// Indeks 0 = bukan apa-apa.
inline constexpr std::array<ClassDef, kClassCount> kClasses = {{
    {0x000000, 0x000000, FCoal, false, false},     // 0  none
    {0x5C5C5C, 0xB4BCCB, FCoal, true, false},      // 1  coal
    {0xD8AF93, 0xFFD6B8, FIron, true, false},      // 2  iron
    {0xE77C56, 0xFFA062, FCopper, true, false},    // 3  copper
    {0xFFD700, 0xFFE450, FGold, true, false},      // 4  gold
    {0xFF1F1F, 0xFF5555, FRedstone, true, false},  // 5  redstone
    {0x2F5BFF, 0x5F92FF, FLapis, true, false},     // 6  lapis
    {0x00FFFF, 0x55FFFF, FDiamond, true, false},   // 7  diamond (cyan)
    {0x17DD62, 0x55FF8E, FEmerald, true, false},   // 8  emerald
    {0xF2F2F2, 0xFFFFFF, FQuartz, true, false},    // 9  quartz
    {0xA0522D, 0xE2935E, FDebris, true, false},    // 10 ancient debris
    {0x9E3B47, 0xFF6683, FBrick, false, false},    // 11 nether brick
    {0xFF6A00, 0xFF9440, FLava, false, false},     // 12 lava
    {0x3F76FF, 0x72A6FF, FWater, false, false},    // 13 water
    {0xE8D58A, 0xFFF0A8, FSand, false, false},     // 14 sand
    {0xC8561E, 0xFF8A4A, FSand, false, false},     // 15 red sand
    {0xA6A6A6, 0xD6D6D6, FGravel, false, false},   // 16 gravel
    {0xE6A000, 0xFFC63A, FChest, true, true},      // 17 chest
    {0xE04A2E, 0xFF7355, FChest, true, true},      // 18 trapped chest
    {0x9A3DFF, 0xC684FF, FChest, true, true},      // 19 ender chest
}};

struct NameDef {
    std::string_view name;
    uint8_t cls;
};

inline constexpr NameDef kNames[] = {
    {"coal_ore", 1},
    {"deepslate_coal_ore", 1},
    {"iron_ore", 2},
    {"deepslate_iron_ore", 2},
    {"copper_ore", 3},
    {"deepslate_copper_ore", 3},
    {"gold_ore", 4},
    {"deepslate_gold_ore", 4},
    {"nether_gold_ore", 4},
    {"redstone_ore", 5},
    {"lit_redstone_ore", 5},
    {"deepslate_redstone_ore", 5},
    {"lit_deepslate_redstone_ore", 5},
    {"lapis_ore", 6},
    {"deepslate_lapis_ore", 6},
    {"diamond_ore", 7},
    {"deepslate_diamond_ore", 7},
    {"emerald_ore", 8},
    {"deepslate_emerald_ore", 8},
    {"quartz_ore", 9},
    {"ancient_debris", 10},
    {"nether_brick", 11},
    {"red_nether_brick", 11},
    {"cracked_nether_bricks", 11},
    {"chiseled_nether_bricks", 11},
    {"nether_brick_fence", 11},
    {"nether_brick_stairs", 11},
    {"red_nether_brick_stairs", 11},
    {"nether_brick_slab", 11},
    {"lava", 12},
    {"flowing_lava", 12},
    {"water", 13},
    {"flowing_water", 13},
    {"sand", 14},
    {"red_sand", 15},
    {"gravel", 16},
    {"chest", 17},
    {"trapped_chest", 18},
    {"ender_chest", 19},
};

inline uint8_t classifyName(std::string_view n) {
    constexpr std::string_view ns = "minecraft:";
    if (n.size() >= ns.size() && n.substr(0, ns.size()) == ns) n.remove_prefix(ns.size());
    for (const auto& d : kNames) {
        if (d.name == n) return d.cls;
    }
    return 0;
}

// Grid kubus (2*half+1)^3. Koordinat sel 0..side-1; di luar grid dianggap kelas 0.
struct Grid {
    int half = 0;
    int side = 0;
    std::vector<uint8_t> cells;

    void reset(int h) {
        half = h;
        side = 2 * h + 1;
        cells.assign(static_cast<size_t>(side) * side * side, 0);
    }
    size_t total() const { return cells.size(); }
    size_t idx(int x, int y, int z) const {
        return (static_cast<size_t>(x) * side + y) * side + z;
    }
    bool inside(int x, int y, int z) const {
        return x >= 0 && y >= 0 && z >= 0 && x < side && y < side && z < side;
    }
    uint8_t get(int x, int y, int z) const { return inside(x, y, z) ? cells[idx(x, y, z)] : 0; }
    void set(int x, int y, int z, uint8_t c) {
        if (inside(x, y, z)) cells[idx(x, y, z)] = c;
    }
};

using LineSet = std::array<std::vector<float>, kClassCount>;

// Outline region menyatu (ore, brick, air, pasir, kerikil).
//  - radius  : sel yang dipancarkan hanya dalam jarak <= radius dari pusat
//              (grid harus punya half >= radius + 1 supaya tetangga valid)
//  - enabled : kelas mana yang dikerjakan
//  - ox,oy,oz: koordinat dunia sel pusat grid
//  - out[c]  : float x1,y1,z1,x2,y2,z2 per garis (koordinat dunia)
//
// Sisi (face) kelas c yang menghadap non-c = sisi batas. Tepi sisi batas digambar
// HANYA jika sisi batas tetangga yang sebidang tidak ada (sudut cembung / cekung /
// ujung bidang). Permukaan datar besar hanya tergambar tepinya.
inline void buildOutlines(const Grid& g, int radius, const std::array<bool, kClassCount>& enabled,
                          size_t maxFloatsPerClass, int ox, int oy, int oz, LineSet& out) {
    static constexpr int kDir[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
                                       {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    const int H = g.half;
    std::unordered_set<uint32_t> seen;
    seen.reserve(4096);

    for (auto& v : out) v.clear();

    for (int x = H - radius; x <= H + radius; ++x) {
        for (int y = H - radius; y <= H + radius; ++y) {
            for (int z = H - radius; z <= H + radius; ++z) {
                const uint8_t c = g.get(x, y, z);
                if (c == 0 || !enabled[c] || kClasses[c].box) continue;
                const int cell[3] = {x, y, z};

                for (int d = 0; d < 6; ++d) {
                    const int a = d / 2;                  // sumbu normal
                    const int sa = (d % 2 == 0) ? 1 : -1; // arah normal
                    if (g.get(x + kDir[d][0], y + kDir[d][1], z + kDir[d][2]) == c) continue;

                    for (int k = 0; k < 2; ++k) {
                        const int p = (a + 1 + k) % 3; // sumbu sisi tempat tepi berada
                        const int q = (a + 2 - k) % 3; // sumbu sejajar tepi
                        for (int tp = -1; tp <= 1; tp += 2) {
                            int nb[3] = {x, y, z};
                            nb[p] += tp;
                            const bool continues =
                                g.get(nb[0], nb[1], nb[2]) == c &&
                                g.get(nb[0] + kDir[d][0], nb[1] + kDir[d][1], nb[2] + kDir[d][2]) != c;
                            if (continues) continue;

                            int s[3] = {cell[0], cell[1], cell[2]};
                            s[a] += (sa > 0) ? 1 : 0;
                            s[p] += (tp > 0) ? 1 : 0;
                            const uint32_t key = static_cast<uint32_t>(s[0]) |
                                                 (static_cast<uint32_t>(s[1]) << 8) |
                                                 (static_cast<uint32_t>(s[2]) << 16) |
                                                 (static_cast<uint32_t>(q) << 24);
                            if (!seen.insert(key).second) continue;

                            auto& dst = out[c];
                            if (dst.size() + 6 > maxFloatsPerClass) continue;
                            float e[3] = {static_cast<float>(ox + s[0] - H),
                                          static_cast<float>(oy + s[1] - H),
                                          static_cast<float>(oz + s[2] - H)};
                            dst.push_back(e[0]);
                            dst.push_back(e[1]);
                            dst.push_back(e[2]);
                            e[q] += 1.0f;
                            dst.push_back(e[0]);
                            dst.push_back(e[1]);
                            dst.push_back(e[2]);
                        }
                    }
                }
            }
        }
    }
}

// Kotak per block untuk kelas box (chest): ukuran model chest 14/16 x 14/16 x 14/16.
inline void pushBox(std::vector<float>& dst, float x0, float y0, float z0, float x1, float y1, float z1) {
    const float c[8][3] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1},
                           {x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}};
    static constexpr int kEdges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                          {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& e : kEdges) {
        for (int i = 0; i < 3; ++i) dst.push_back(c[e[0]][i]);
        for (int i = 0; i < 3; ++i) dst.push_back(c[e[1]][i]);
    }
}

// Tidak membersihkan out (panggil setelah buildOutlines).
inline void buildBoxes(const Grid& g, int radius, const std::array<bool, kClassCount>& enabled,
                       size_t maxFloatsPerClass, int ox, int oy, int oz, LineSet& out) {
    const int H = g.half;
    for (int x = H - radius; x <= H + radius; ++x) {
        for (int y = H - radius; y <= H + radius; ++y) {
            for (int z = H - radius; z <= H + radius; ++z) {
                const uint8_t c = g.get(x, y, z);
                if (c == 0 || !enabled[c] || !kClasses[c].box) continue;
                auto& dst = out[c];
                if (dst.size() + 72 > maxFloatsPerClass) continue;
                const float wx = static_cast<float>(ox + x - H);
                const float wy = static_cast<float>(oy + y - H);
                const float wz = static_cast<float>(oz + z - H);
                pushBox(dst, wx + 0.0625f, wy, wz + 0.0625f, wx + 0.9375f, wy + 0.875f, wz + 0.9375f);
            }
        }
    }
}

// ----------------------------------------------------------------- animasi & geometri

// Easing pop: 0 -> sedikit melewati 1 -> 1.
inline float easeOutBack(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.0f;
    const float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

// Kunci stabil sebuah garis (melacak garis lama vs baru antar pass scan).
inline uint64_t lineKey(uint8_t cls, const float* p) {
    uint64_t h = 1469598103934665603ull ^ cls;
    for (int i = 0; i < 6; ++i) {
        const int32_t v = static_cast<int32_t>(std::lround(p[i] * 16.0f));
        h ^= static_cast<uint32_t>(v);
        h *= 1099511628211ull;
    }
    return h;
}

// Garis tebal menghadap kamera. a,b relatif terhadap kamera. Menghasilkan dua quad
// dengan arah putar berlawanan (out[0..11] dan out[12..23]) supaya tidak hilang
// karena culling. Mengembalikan false kalau garis sejajar arah pandang.
inline bool beamQuads(const float a[3], const float b[3], float out[24]) {
    const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float m[3] = {(a[0] + b[0]) * 0.5f, (a[1] + b[1]) * 0.5f, (a[2] + b[2]) * 0.5f};
    auto cross = [&](const float u[3], const float v[3], float r[3]) {
        r[0] = u[1] * v[2] - u[2] * v[1];
        r[1] = u[2] * v[0] - u[0] * v[2];
        r[2] = u[0] * v[1] - u[1] * v[0];
        return std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    };
    float s[3];
    float len = cross(d, m, s);
    if (len < 1e-6f) { // garis sejajar arah pandang: pakai sumbu lain
        const float up[3] = {0.f, 1.f, 0.f};
        const float rt[3] = {1.f, 0.f, 0.f};
        len = cross(d, up, s);
        if (len < 1e-6f) len = cross(d, rt, s);
    }
    if (len < 1e-6f) return false; // garis panjang nol
    const float dist = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
    const float half = std::clamp(0.0022f * dist, 0.004f, 0.09f);
    const float k = half / len;
    for (float& v : s) v *= k;
    const float v0[4][3] = {{a[0] + s[0], a[1] + s[1], a[2] + s[2]},
                            {b[0] + s[0], b[1] + s[1], b[2] + s[2]},
                            {b[0] - s[0], b[1] - s[1], b[2] - s[2]},
                            {a[0] - s[0], a[1] - s[1], a[2] - s[2]}};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 3; ++j) {
            out[i * 3 + j] = v0[i][j];
            out[12 + i * 3 + j] = v0[3 - i][j];
        }
    return true;
}

} // namespace vo
