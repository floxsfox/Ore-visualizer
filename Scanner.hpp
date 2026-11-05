#pragma once

// Logika murni (tanpa dependensi engine): klasifikasi nama block dan
// pembuatan garis outline dari grid voxel.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace vo {

enum Group : uint8_t { GroupOre, GroupLava, GroupWater, GroupSand, GroupGravel };

struct ClassDef {
    uint32_t rgb;
    Group group;
};

constexpr int kClassCount = 16;

// Indeks 0 = bukan apa-apa.
inline constexpr std::array<ClassDef, kClassCount> kClasses = {{
    {0x000000, GroupOre},    // 0  none
    {0x5C5C5C, GroupOre},    // 1  coal
    {0xD8AF93, GroupOre},    // 2  iron
    {0xE77C56, GroupOre},    // 3  copper
    {0xFFD700, GroupOre},    // 4  gold
    {0xFF1F1F, GroupOre},    // 5  redstone
    {0x2F5BFF, GroupOre},    // 6  lapis
    {0x00FFFF, GroupOre},    // 7  diamond (cyan)
    {0x17DD62, GroupOre},    // 8  emerald
    {0xF2F2F2, GroupOre},    // 9  quartz
    {0xA0522D, GroupOre},    // 10 ancient debris
    {0xFF6A00, GroupLava},   // 11 lava
    {0x3F76FF, GroupWater},  // 12 water
    {0xE8D58A, GroupSand},   // 13 sand
    {0xC8561E, GroupSand},   // 14 red sand
    {0xA6A6A6, GroupGravel}, // 15 gravel
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
    {"lava", 11},
    {"flowing_lava", 11},
    {"water", 12},
    {"flowing_water", 12},
    {"sand", 13},
    {"red_sand", 14},
    {"gravel", 15},
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

// Bangun garis outline.
//  - radius      : sel yang dipancarkan hanya dalam jarak <= radius dari pusat
//                  (grid harus punya half >= radius + 1 supaya tetangga valid)
//  - enabled     : kelas mana yang dikerjakan
//  - ox,oy,oz    : koordinat dunia sel pusat grid
//  - out[c]      : float x1,y1,z1,x2,y2,z2 per garis (koordinat dunia)
//
// Aturan outline: sebuah sisi (face) kelas c yang menghadap non-c adalah sisi batas.
// Tiap tepi sisi batas digambar HANYA jika sisi batas tetangga yang sebidang tidak
// ada (sudut cembung / cekung / ujung bidang). Jadi permukaan datar besar (air, pasir)
// hanya tergambar garis tepinya, bukan kotak-kotak per block.
inline void buildOutlines(const Grid& g, int radius, const std::array<bool, kClassCount>& enabled,
                          size_t maxFloatsPerClass, int ox, int oy, int oz,
                          std::array<std::vector<float>, kClassCount>& out) {
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
                if (c == 0 || !enabled[c]) continue;
                const int cell[3] = {x, y, z};

                for (int d = 0; d < 6; ++d) {
                    const int a = d / 2;                 // sumbu normal
                    const int sa = (d % 2 == 0) ? 1 : -1; // arah normal
                    if (g.get(x + kDir[d][0], y + kDir[d][1], z + kDir[d][2]) == c) continue;

                    for (int k = 0; k < 2; ++k) {
                        const int p = (a + 1 + k) % 3; // sumbu sisi tempat tepi berada
                        const int q = (a + 2 - k) % 3; // sumbu sejajar tepi
                        for (int tp = -1; tp <= 1; tp += 2) {
                            // sisi batas tetangga yang sebidang?
                            int nb[3] = {x, y, z};
                            nb[p] += tp;
                            const bool continues =
                                g.get(nb[0], nb[1], nb[2]) == c &&
                                g.get(nb[0] + kDir[d][0], nb[1] + kDir[d][1], nb[2] + kDir[d][2]) != c;
                            if (continues) continue;

                            int s[3] = {cell[0], cell[1], cell[2]};
                            s[a] += (sa > 0) ? 1 : 0;
                            s[p] += (tp > 0) ? 1 : 0;
                            // s[q] = cell[q] (awal tepi)
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

} // namespace vo
