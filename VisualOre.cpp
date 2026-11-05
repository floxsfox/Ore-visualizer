// Visual Ore - native mod LeviLauncher (Minecraft Bedrock arm64)
// Menggambar outline tipis ore / lava / air / pasir / kerikil tembus tembok.
//
// Alur:
//   ClientInstance::update  -> scan blok di sekitar kamera (bertahap, ada batas waktu)
//   LevelRenderer::renderLevel -> gambar garis lewat tessellator bawaan game
//
// Signature & offset untuk Minecraft Bedrock 1.26.60.x arm64.
// Data signature/offset berasal dari proyek BedrockTools (QYCottage); kodenya ditulis sendiri.

#include "Engine.hpp"
#include "Scanner.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>
#include <pl/memory/Hook.hpp>
#include <pl/memory/Signature.hpp>

namespace {

using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------- signature
constexpr const char *kLib = "libminecraftpe.so";

constexpr const char *kSigClientInstanceUpdate =
    "? ? ? FC ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 ? ? ? D1 59 D0 3B D5 ? ? ? 52 F5 03 02 2A";
constexpr const char *kSigRenderLevel =
    "? ? ? D1 ? ? ? FD ? ? ? 6D ? ? ? 6D ? ? ? 6D ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 57 D0 3B D5";
constexpr const char *kSigTessBegin =
    "? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 FD 03 00 91 ? ? ? 39 ? ? ? 39 08 01 09 2A";
constexpr const char *kSigTessColor =
    "? ? ? 52 ? ? ? 39 04 01 27 1E 00 08 24 1E 21 08 24 1E 42 08 24 1E 63 08 24 1E 08 00 38 1E";
constexpr const char *kSigTessVertex =
    "? ? ? D1 ? ? ? FD ? ? ? 6D ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 5B D0 3B D5 ? ? ? F9 ? ? ? F9";
constexpr const char *kSigRenderMesh =
    "? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 FD 03 00 91 ? ? ? D1 57 D0 3B D5 F6 03 00 AA E0 03 01 AA ? ? ? F9 F4 03 03 AA";
constexpr const char *kSigMaterialGroup =
    "? ? ? ? ? ? ? 91 ? ? ? D1 ? ? ? D1 ? ? ? 91 E3 03 14 AA ? ? ? 52 ? ? ? ? ? ? ? F9 ? ? ? A9 ? ? ? B4 ? ? ? 91 ? ? ? 92 ? ? ? ? ? ? ? B5 ? ? ? F9 E0 03 14 AA ? ? ? F9 00 01 3F D6 E0 03 14 AA ? ? ? ? ? ? ? F9 ? ? ? F9 ? ? ? B4 ? ? ? 91 ? ? ? 92 ? ? ? ? ? ? ? B5 ? ? ? F9 E0 03 14 AA ? ? ? F9 00 01 3F D6 E0 03 14 AA ? ? ? ? ? ? ? 38 ? ? ? 37 ? ? ? 39 ? ? ? 37 ? ? ? F8 ? ? ? A9 ? ? ? B4 ? ? ? 91 ? ? ? 92 ? ? ? ? ? ? ? B5 ? ? ? F9 E0 03 14 AA ? ? ? F9 00 01 3F D6 E0 03 14 AA ? ? ? ? ? ? ? F8 ? ? ? F8 ? ? ? B4 ? ? ? 91 ? ? ? 92 ? ? ? ? ? ? ? B5 ? ? ? F9 E0 03 14 AA ? ? ? F9 00 01 3F D6 E0 03 14 AA ? ? ? ? ? ? ? ? ? ? ? F9 ? ? ? 38 ? ? ? 36 ? ? ? F8 ? ? ? ? ? ? ? 39 ? ? ? 36 ? ? ? F9 ? ? ? ? ? ? ? F8 ? ? ? A9 ? ? ? B5 ? ? ? F8 ? ? ? F9 ? ? ? A9 ? ? ? F9 ? ? ? F9 ? ? ? B4 ? ? ? A9 ? ? ? B4 ? ? ? 91 ? ? ? 52 ? ? ? ? ? ? ? F9 ? ? ? F9 ? ? ? B4 ? ? ? F9 ? ? ? 91 E1 03 13 AA ? ? ? 91 ? ? ? ? ? ? ? 39 ? ? ? A9 ? ? ? 72 ? ? ? D3 29 15 94 9A 08 01 8A 9A ? ? ? A9 ? ? ? D1 ? ? ? 91 ? ? ? ? ? ? ? F9 ? ? ? F9 ? ? ? F9 E8 03 00 91";
constexpr const char *kSigGetBlock =
    "? ? ? D1 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 56 D0 3B D5 ? ? ? F9 ? ? ? F9 ? ? ? B9 ? ? ? 79 1F 01 09 6B ? ? ? 54 ? ? ? 79 F3 03 00 AA 1F 01 09 6B ? ? ? 54 E0 03 00 91";

// ---------------------------------------------------------------- offset (1.26.60.x)
constexpr size_t kVtClientInstanceGetRegion = 30;
constexpr size_t kLevelRendererPlayerOff = 0x430;  // LevelRenderer -> LevelRendererPlayer*
constexpr size_t kCamPosOff = 0x614;               // LevelRendererPlayer: float x,y,z
constexpr size_t kSelectionOverlayMaterialOff = 0x10A0;
constexpr size_t kScreenContextTessellatorOff = 0xB0;
constexpr size_t kScreenContextColorHolderOff = 0x30;
constexpr size_t kShaderColorDirtyOff = 0x10;
constexpr size_t kRenderMaterialGroupOff = 32;
constexpr size_t kBlockTypeOff = 0x68;             // Block -> BlockType*
constexpr size_t kBlockTypeNameInfoOff = 0x88;     // BlockType::mNameInfo
constexpr size_t kNameInfoFullNameOff = 0x40;      // NameInfo::mFullName (HashedString)
constexpr size_t kHashedStringStrOff = 0x8;        // HashedString::mString
constexpr size_t kBlockNameOff = kBlockTypeNameInfoOff + kNameInfoFullNameOff + kHashedStringStrOff;

constexpr int kMinY = -64;
constexpr int kMaxY = 319;
constexpr size_t kMaxFloatsPerClass = 6 * 40000;
constexpr size_t kBatchLines = 4000;
constexpr auto kPassInterval = std::chrono::milliseconds(400);
constexpr auto kFrameScanBudget = std::chrono::microseconds(2500);

// ---------------------------------------------------------------- tipe fungsi game
using UpdateFn = uint64_t (*)(void *, uint64_t, uint64_t, uint64_t, uint64_t);
using RenderLevelFn = void (*)(void *, void *, void *);
using GetRegionFn = void *(*)(void *);
using GetBlockFn = const void *(*)(void *, const int *);
using TessBeginFn = void (*)(void *, void *, int, int, int);
using TessColorFn = void (*)(void *, float, float, float, float);
using TessVertexFn = void (*)(void *, float, float, float);
using RenderMeshFn = void (*)(void *, void *, void *, char *);

// Layout HashedString game: hash FNV-1 64 bit + std::string + pointer cache.
struct HashedString {
    uint64_t hash;
    std::string str;
    const HashedString *lastMatch;
    explicit HashedString(const char *s) : hash(0), str(s ? s : ""), lastMatch(nullptr) {
        if (str.empty()) return;
        uint64_t h = 0xCBF29CE484222325ULL;
        for (const char ch : str) h = static_cast<uint64_t>(static_cast<unsigned char>(ch)) ^ (0x100000001B3ULL * h);
        hash = h;
    }
};

// mce::MaterialPtr (shared_ptr 16 byte). Dtor sengaja kosong supaya refcount game
// tidak disentuh dari sisi mod (layout harus sama dengan milik game agar ABI return cocok).
struct MaterialPtr {
    void *p[2]{nullptr, nullptr};
    MaterialPtr() = default;
    MaterialPtr(const MaterialPtr &) = delete;
    MaterialPtr &operator=(const MaterialPtr &) = delete;
    MaterialPtr(MaterialPtr &&o) noexcept : p{o.p[0], o.p[1]} { o.p[0] = o.p[1] = nullptr; }
    MaterialPtr &operator=(MaterialPtr &&o) noexcept {
        if (this != &o) {
            p[0] = o.p[0];
            p[1] = o.p[1];
            o.p[0] = o.p[1] = nullptr;
        }
        return *this;
    }
    ~MaterialPtr() {}
    explicit operator bool() const { return p[0] != nullptr; }
};

struct Snapshot {
    std::array<std::vector<float>, vo::kClassCount> lines;
};

struct Settings {
    std::atomic_bool moduleOn{true};
    std::atomic_bool ore{true};
    std::atomic_bool lava{true};
    std::atomic_bool water{true};
    std::atomic_bool sand{true};
    std::atomic_bool gravel{true};
    std::atomic_int radius{16};   // blok
    std::atomic_int speed{20};    // ribu blok per frame (dibatasi waktu juga)
    std::atomic_int material{1};  // 0 selection_box, 1 selection_overlay, 2 bawaan LevelRendererPlayer
};

struct ScanState {
    vo::Grid grid;
    bool scanning = false;
    size_t cursor = 0;
    int ox = 0, oy = 0, oz = 0;
    int radius = 16;
    void *blockSource = nullptr;
    Clock::time_point nextPass{};
};

struct Globals {
    Settings cfg;

    // fungsi game
    UpdateFn origUpdate = nullptr;
    RenderLevelFn origRender = nullptr;
    GetBlockFn getBlock = nullptr;
    TessBeginFn tessBegin = nullptr;
    TessColorFn tessColor = nullptr;
    TessVertexFn tessVertex = nullptr;
    RenderMeshFn renderMesh = nullptr;
    uintptr_t renderMaterialGroup = 0;

    bool resolved = false;
    std::atomic_bool ready{false};     // semua hook terpasang
    std::atomic_bool probeOk{false};   // rantai getBlock -> nama block tervalidasi
    int probeFails = 0;
    Clock::time_point nextProbe{};

    // dari hook
    std::atomic<void *> clientInstance{nullptr};
    std::atomic<float> cam[3]{0.f, 0.f, 0.f};
    std::atomic_bool camValid{false};

    ScanState scan;
    std::unordered_map<uintptr_t, uint8_t> blockClass; // Block* -> kelas
    uintptr_t lastBlock = 0;
    uint8_t lastClass = 0;

    std::mutex snapMutex;
    std::shared_ptr<const Snapshot> snapshot;

    MaterialPtr mat[2];
    int matFetchFails[2] = {0, 0};

    std::atomic_bool loggedFirstDraw{false};
    std::atomic_bool loggedFirstSnapshot{false};
    pl::memory::HookHandle updateHook;
    pl::memory::HookHandle renderHook;
};

Globals g;

ll::mod::NativeMod *gSelf = nullptr;

// Makro supaya format string tetap literal (fmt memeriksa saat compile).
#define VO_INFO(...) do { if (gSelf) gSelf->getLogger().info(__VA_ARGS__); } while (0)
#define VO_WARN(...) do { if (gSelf) gSelf->getLogger().warn(__VA_ARGS__); } while (0)

// ---------------------------------------------------------------- block -> kelas
bool readBlockName(const void *block, std::string &name) {
    uintptr_t blockType = 0;
    if (!vo::engine::safeReadValue(reinterpret_cast<uintptr_t>(block) + kBlockTypeOff, blockType)) return false;
    if (blockType < 0x10000) return false;
    return vo::engine::readGameString(blockType + kBlockNameOff, name);
}

uint8_t classOfBlock(const void *block) {
    const auto key = reinterpret_cast<uintptr_t>(block);
    if (key == g.lastBlock) return g.lastClass;
    uint8_t cls = 0;
    const auto it = g.blockClass.find(key);
    if (it != g.blockClass.end()) {
        cls = it->second;
    } else {
        std::string name;
        if (key >= 0x10000 && readBlockName(block, name)) cls = vo::classifyName(name);
        g.blockClass.emplace(key, cls);
    }
    g.lastBlock = key;
    g.lastClass = cls;
    return cls;
}

void *getRegion(void *clientInstance) {
    uintptr_t vtable = 0;
    uintptr_t fn = 0;
    if (!vo::engine::safeReadValue(reinterpret_cast<uintptr_t>(clientInstance), vtable) || vtable < 0x10000) return nullptr;
    if (!vo::engine::safeReadValue(vtable + kVtClientInstanceGetRegion * sizeof(void *), fn) || fn < 0x10000) return nullptr;
    return reinterpret_cast<GetRegionFn>(fn)(clientInstance);
}

// Validasi rantai: getBlock di kolom di bawah kamera harus mengembalikan block
// dengan nama "namespace:nama" dan minimal satu bukan udara.
bool runProbe(void *blockSource) {
    const int bx = static_cast<int>(std::floor(g.cam[0].load()));
    const int by = static_cast<int>(std::floor(g.cam[1].load()));
    const int bz = static_cast<int>(std::floor(g.cam[2].load()));
    int valid = 0, nonAir = 0;
    std::string samples;
    for (int y = std::min(by, kMaxY); y > by - 48 && y >= kMinY; --y) {
        const int pos[3] = {bx, y, bz};
        const void *blk = g.getBlock(blockSource, pos);
        if (!blk) continue;
        std::string name;
        if (!readBlockName(blk, name) || name.find(':') == std::string::npos) continue;
        ++valid;
        if (name != "minecraft:air") {
            ++nonAir;
            if (samples.size() < 120 && samples.find(name) == std::string::npos) samples += name + " ";
        }
    }
    VO_INFO("Probe getBlock: valid={} bukanUdara={} contoh=[{}]", valid, nonAir, samples);
    return valid >= 3 && nonAir >= 1;
}

void publish(std::shared_ptr<const Snapshot> s) {
    std::lock_guard<std::mutex> lock(g.snapMutex);
    g.snapshot = std::move(s);
}

std::shared_ptr<const Snapshot> currentSnapshot() {
    std::lock_guard<std::mutex> lock(g.snapMutex);
    return g.snapshot;
}

// ---------------------------------------------------------------- scan (thread update)
void scanTick() {
    if (!g.cfg.moduleOn.load() || !g.camValid.load()) return;
    void *ci = g.clientInstance.load();
    if (!ci) return;

    void *bs = getRegion(ci);
    if (!bs || reinterpret_cast<uintptr_t>(bs) < 0x10000) {
        if (g.scan.blockSource) {
            g.scan = ScanState{};
            publish(nullptr);
        }
        return;
    }

    const auto now = Clock::now();
    if (!g.probeOk.load()) {
        if (g.probeFails >= 5 || now < g.nextProbe) return;
        if (runProbe(bs)) {
            g.probeOk = true;
            VO_INFO("Visual Ore aktif");
        } else {
            ++g.probeFails;
            g.nextProbe = now + std::chrono::seconds(3);
            if (g.probeFails >= 5) VO_WARN("Probe gagal 5x, Visual Ore dinonaktifkan (signature getBlock tidak cocok dengan versi ini)");
        }
        return;
    }

    ScanState &st = g.scan;
    if (st.blockSource != bs) { // dunia/dimensi berganti
        st = ScanState{};
        st.blockSource = bs;
        publish(nullptr);
    }

    if (!st.scanning) {
        if (now < st.nextPass) return;
        st.radius = std::clamp(g.cfg.radius.load(), 8, 32);
        st.grid.reset(st.radius + 1);
        st.ox = static_cast<int>(std::floor(g.cam[0].load()));
        st.oy = static_cast<int>(std::floor(g.cam[1].load()));
        st.oz = static_cast<int>(std::floor(g.cam[2].load()));
        st.cursor = 0;
        st.scanning = true;
    }

    const size_t maxBlocks = static_cast<size_t>(std::clamp(g.cfg.speed.load(), 5, 60)) * 1000;
    const auto t0 = Clock::now();
    const size_t total = st.grid.total();
    const int side = st.grid.side;
    const int H = st.grid.half;
    size_t done = 0;
    while (st.cursor < total && done < maxBlocks) {
        const size_t i = st.cursor++;
        ++done;
        const int z = static_cast<int>(i % side);
        const int y = static_cast<int>((i / side) % side);
        const int x = static_cast<int>(i / (static_cast<size_t>(side) * side));
        const int wy = st.oy + y - H;
        if (wy < kMinY || wy > kMaxY) continue;
        const int pos[3] = {st.ox + x - H, wy, st.oz + z - H};
        const void *blk = g.getBlock(bs, pos);
        if (!blk) continue;
        const uint8_t c = classOfBlock(blk);
        if (c) st.grid.cells[i] = c;
        if ((done & 511) == 0 && Clock::now() - t0 > kFrameScanBudget) break;
    }
    if (st.cursor < total) return;

    // pass selesai -> bangun outline
    std::array<bool, vo::kClassCount> enabled{};
    for (int c = 1; c < vo::kClassCount; ++c) {
        switch (vo::kClasses[c].group) {
        case vo::GroupOre: enabled[c] = g.cfg.ore.load(); break;
        case vo::GroupLava: enabled[c] = g.cfg.lava.load(); break;
        case vo::GroupWater: enabled[c] = g.cfg.water.load(); break;
        case vo::GroupSand: enabled[c] = g.cfg.sand.load(); break;
        case vo::GroupGravel: enabled[c] = g.cfg.gravel.load(); break;
        }
    }
    auto snap = std::make_shared<Snapshot>();
    vo::buildOutlines(st.grid, st.radius, enabled, kMaxFloatsPerClass, st.ox, st.oy, st.oz, snap->lines);
    if (!g.loggedFirstSnapshot.exchange(true)) {
        size_t lines = 0;
        for (const auto &v : snap->lines) lines += v.size() / 6;
        VO_INFO("Scan pertama selesai: {} garis outline", lines);
    }
    publish(std::move(snap));
    st.scanning = false;
    st.nextPass = Clock::now() + kPassInterval;
}

uint64_t updateHook(void *self, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    g.clientInstance.store(self, std::memory_order_relaxed);
    const uint64_t r = g.origUpdate ? g.origUpdate(self, a1, a2, a3, a4) : 0;
    try {
        scanTick();
    } catch (...) {
    }
    return r;
}

// ---------------------------------------------------------------- render (thread render)
MaterialPtr fetchMaterial(const char *name) {
    if (!g.renderMaterialGroup) return {};
    HashedString hs(name);
    uintptr_t vtable = 0, fn = 0;
    if (!vo::engine::safeReadValue(g.renderMaterialGroup, vtable) || vtable < 0x10000) return {};
    if (!vo::engine::safeReadValue(vtable + 2 * sizeof(void *), fn) || fn < 0x10000) return {};
    using GetMaterialFn = MaterialPtr (*)(void *, const HashedString *);
    return reinterpret_cast<GetMaterialFn>(fn)(reinterpret_cast<void *>(g.renderMaterialGroup), &hs);
}

void *pickMaterial(int mode, uintptr_t levelRendererPlayer) {
    void *fallback = reinterpret_cast<void *>(levelRendererPlayer + kSelectionOverlayMaterialOff);
    if (mode < 0 || mode > 1) return fallback;
    MaterialPtr &slot = g.mat[mode];
    if (!slot && g.matFetchFails[mode] < 30) {
        slot = fetchMaterial(mode == 0 ? "selection_box" : "selection_overlay");
        if (!slot) ++g.matFetchFails[mode];
    }
    return slot ? static_cast<void *>(&slot) : fallback;
}

// Set shader color ke putih selama menggambar, lalu kembalikan.
struct ShaderColorGuard {
    uintptr_t holder = 0;
    float saved[4]{};
    unsigned char savedDirty = 0;
    bool ok = false;
    explicit ShaderColorGuard(void *screenContext) {
        uintptr_t h = 0;
        if (!vo::engine::safeReadValue(reinterpret_cast<uintptr_t>(screenContext) + kScreenContextColorHolderOff, h) || h < 0x10000) return;
        unsigned char raw[kShaderColorDirtyOff + 1];
        if (!vo::engine::safeRead(reinterpret_cast<void *>(h), raw, sizeof(raw))) return;
        holder = h;
        std::memcpy(saved, raw, sizeof(saved));
        savedDirty = raw[kShaderColorDirtyOff];
        const float white[4] = {1.f, 1.f, 1.f, 1.f};
        std::memcpy(reinterpret_cast<void *>(holder), white, sizeof(white));
        *reinterpret_cast<unsigned char *>(holder + kShaderColorDirtyOff) = 1;
        ok = true;
    }
    ~ShaderColorGuard() {
        if (!ok) return;
        std::memcpy(reinterpret_cast<void *>(holder), saved, sizeof(saved));
        *reinterpret_cast<unsigned char *>(holder + kShaderColorDirtyOff) = savedDirty ? savedDirty : 1;
    }
    ShaderColorGuard(const ShaderColorGuard &) = delete;
    ShaderColorGuard &operator=(const ShaderColorGuard &) = delete;
};

void drawOutlines(void *levelRenderer, void *screenContext) {
    if (!g.cfg.moduleOn.load() || !levelRenderer || !screenContext) return;
    if (reinterpret_cast<uintptr_t>(screenContext) < 0x1000) return;

    uintptr_t lrp = 0;
    if (!vo::engine::safeReadValue(reinterpret_cast<uintptr_t>(levelRenderer) + kLevelRendererPlayerOff, lrp) || lrp < 0x1000) return;
    float cam[3];
    if (!vo::engine::safeRead(reinterpret_cast<void *>(lrp + kCamPosOff), cam, sizeof(cam))) return;
    for (int i = 0; i < 3; ++i) g.cam[i].store(cam[i], std::memory_order_relaxed);
    g.camValid.store(true, std::memory_order_relaxed);

    if (!g.probeOk.load()) return;
    const auto snap = currentSnapshot();
    if (!snap) return;

    uintptr_t tess = 0;
    if (!vo::engine::safeReadValue(reinterpret_cast<uintptr_t>(screenContext) + kScreenContextTessellatorOff, tess) || tess < 0x1000) return;

    void *material = pickMaterial(g.cfg.material.load(), lrp);
    ShaderColorGuard guard(screenContext); // kalau holder tidak terbaca, tetap gambar tanpa reset warna

    size_t drawn = 0;
    for (int c = 1; c < vo::kClassCount; ++c) {
        const auto &v = snap->lines[c];
        if (v.empty()) continue;
        const uint32_t rgb = vo::kClasses[c].rgb;
        const float r = static_cast<float>((rgb >> 16) & 0xFF) / 255.f;
        const float gC = static_cast<float>((rgb >> 8) & 0xFF) / 255.f;
        const float b = static_cast<float>(rgb & 0xFF) / 255.f;

        const size_t lineCount = v.size() / 6;
        for (size_t first = 0; first < lineCount; first += kBatchLines) {
            const size_t n = std::min(kBatchLines, lineCount - first);
            g.tessBegin(reinterpret_cast<void *>(tess), nullptr, 4, static_cast<int>(n * 2), 0);
            g.tessColor(reinterpret_cast<void *>(tess), r, gC, b, 1.0f);
            for (size_t i = 0; i < n; ++i) {
                const float *p = &v[(first + i) * 6];
                g.tessVertex(reinterpret_cast<void *>(tess), p[0] - cam[0], p[1] - cam[1], p[2] - cam[2]);
                g.tessVertex(reinterpret_cast<void *>(tess), p[3] - cam[0], p[4] - cam[1], p[5] - cam[2]);
            }
            char pad[0x58];
            std::memset(pad, 0, sizeof(pad));
            g.renderMesh(screenContext, reinterpret_cast<void *>(tess), material, pad);
            drawn += n;
        }
    }
    if (drawn && !g.loggedFirstDraw.exchange(true)) VO_INFO("Gambar pertama: {} garis", drawn);
}

void renderLevelHook(void *self, void *screenContext, void *a3) {
    if (g.origRender) g.origRender(self, screenContext, a3);
    try {
        drawOutlines(self, screenContext);
    } catch (...) {
    }
}

// ---------------------------------------------------------------- menu
constexpr const char *kModuleId = "visual_ore.main";

bool parseBool(std::string_view v, bool fallback) {
    if (v == "true" || v == "1" || v == "on") return true;
    if (v == "false" || v == "0" || v == "off") return false;
    return fallback;
}

int parseInt(std::string_view v, int fallback) {
    const std::string s(v);
    char *end = nullptr;
    const long x = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0') return fallback;
    return static_cast<int>(x);
}

void onModuleToggle(std::string_view moduleId, bool enabled) {
    if (moduleId != kModuleId) return;
    g.cfg.moduleOn = enabled;
    if (!enabled) publish(nullptr);
}

void onConfigChanged(std::string_view moduleId, std::string_view key, std::string_view value) {
    if (moduleId != kModuleId) return;
    if (key == "ore") g.cfg.ore = parseBool(value, g.cfg.ore);
    else if (key == "lava") g.cfg.lava = parseBool(value, g.cfg.lava);
    else if (key == "water") g.cfg.water = parseBool(value, g.cfg.water);
    else if (key == "sand") g.cfg.sand = parseBool(value, g.cfg.sand);
    else if (key == "gravel") g.cfg.gravel = parseBool(value, g.cfg.gravel);
    else if (key == "radius") g.cfg.radius = std::clamp(parseInt(value, g.cfg.radius), 8, 32);
    else if (key == "speed") g.cfg.speed = std::clamp(parseInt(value, g.cfg.speed), 5, 60);
    else if (key == "material") g.cfg.material = std::clamp(parseInt(value, g.cfg.material), 0, 2);
}

// ---------------------------------------------------------------- mod
class VisualOreMod {
public:
    static VisualOreMod &instance() {
        static VisualOreMod mod;
        return mod;
    }

    VisualOreMod() : mSelf(*ll::mod::NativeMod::current()) { gSelf = &mSelf; }

    [[nodiscard]] ll::mod::NativeMod &getSelf() const { return mSelf; }

    bool load() { return true; }

    bool enable() {
        auto &self = getSelf();
        const bool menuOk =
            pl::modmenu::ModuleBuilder(kModuleId, "Visual Ore")
                .modId(self.getId())
                .description("Outline ore, lava, air, pasir, kerikil tembus tembok")
                .defaultEnabled(true)
                .onToggle(onModuleToggle)
                .config("ore", "Ore", pl::modmenu::ConfigType::Toggle, "true")
                .config("lava", "Lava", pl::modmenu::ConfigType::Toggle, "true")
                .config("water", "Air", pl::modmenu::ConfigType::Toggle, "true")
                .config("sand", "Pasir", pl::modmenu::ConfigType::Toggle, "true")
                .config("gravel", "Kerikil", pl::modmenu::ConfigType::Toggle, "true")
                .config("radius", "Jarak scan (blok)", pl::modmenu::ConfigType::SliderInt, "16", "8", "32")
                .config("speed", "Kecepatan scan (ribu blok/frame)", pl::modmenu::ConfigType::SliderInt, "20", "5", "60")
                .config("material", "Material garis (0/1/2)", pl::modmenu::ConfigType::SliderInt, "1", "0", "2")
                .onConfigChanged(onConfigChanged)
                .registerModule();
        if (!menuOk) self.getLogger().error("Gagal mendaftarkan menu Visual Ore");

        if (!g.resolved && !resolveEngine()) return true; // tetap lolos, tapi idle
        g.updateHook = pl::memory::HookHandle(reinterpret_cast<void *>(mUpdateTarget), reinterpret_cast<void *>(&updateHook),
                                              reinterpret_cast<void **>(&g.origUpdate));
        g.renderHook = pl::memory::HookHandle(reinterpret_cast<void *>(mRenderTarget), reinterpret_cast<void *>(&renderLevelHook),
                                              reinterpret_cast<void **>(&g.origRender));
        g.ready = g.updateHook.installed() && g.renderHook.installed();
        if (g.ready) self.getLogger().info("Hook terpasang (update + renderLevel)");
        else self.getLogger().error("Hook gagal: update={} render={}", g.updateHook.installed(), g.renderHook.installed());
        return true;
    }

    bool disable() {
        g.ready = false;
        g.updateHook.reset();
        g.renderHook.reset();
        publish(nullptr);
        pl::modmenu::unregisterModule(kModuleId);
        getSelf().getLogger().info("Visual Ore dimatikan");
        return true;
    }

    bool unload() {
        disable();
        return true;
    }

private:
    ll::mod::NativeMod &mSelf;
    uintptr_t mUpdateTarget = 0;
    uintptr_t mRenderTarget = 0;

    bool resolveEngine() {
        auto &log = getSelf().getLogger();
        struct Item {
            const char *name;
            const char *pattern;
        };
        const Item items[] = {
            {"ClientInstanceUpdate", kSigClientInstanceUpdate},
            {"RenderLevel", kSigRenderLevel},
            {"TessellatorBegin", kSigTessBegin},
            {"TessellatorColor", kSigTessColor},
            {"TessellatorVertex", kSigTessVertex},
            {"RenderMesh", kSigRenderMesh},
            {"MaterialGroup", kSigMaterialGroup},
            {"BlockSourceGetBlock", kSigGetBlock},
        };
        std::vector<std::string> patterns;
        for (const auto &it : items) patterns.emplace_back(it.pattern);
        const auto resolved = pl::memory::resolveSignatures(patterns, kLib);

        uintptr_t addr[8] = {};
        bool allOk = true;
        for (size_t i = 0; i < 8; ++i) {
            const auto found = resolved.find(std::string(items[i].pattern));
            addr[i] = (found != resolved.end()) ? static_cast<uintptr_t>(found->second) : 0;
            if (!addr[i]) {
                log.error("Signature tidak ketemu: {} (versi game mungkin tidak cocok, target 1.26.60.x)", items[i].name);
                allOk = false;
            }
        }
        if (!allOk) return false;

        mUpdateTarget = addr[0];
        mRenderTarget = addr[1];
        g.tessBegin = reinterpret_cast<TessBeginFn>(addr[2]);
        g.tessColor = reinterpret_cast<TessColorFn>(addr[3]);
        g.tessVertex = reinterpret_cast<TessVertexFn>(addr[4]);
        g.renderMesh = reinterpret_cast<RenderMeshFn>(addr[5]);
        g.getBlock = reinterpret_cast<GetBlockFn>(addr[7]);

        const uintptr_t groupBase = vo::engine::decodeAdrpAdd(reinterpret_cast<const uint32_t *>(addr[6]), 0);
        if (groupBase) g.renderMaterialGroup = groupBase + kRenderMaterialGroupOff;
        else log.warn("RenderMaterialGroup tidak terdecode, pakai material bawaan game");

        g.resolved = true;
        return true;
    }
};

} // namespace

PL_REGISTER_MOD(VisualOreMod, VisualOreMod::instance())
