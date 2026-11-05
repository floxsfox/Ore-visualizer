// Visual Outline v1.1.0 - native mod LeviLauncher (Minecraft Bedrock arm64)
// Credit: floxsfox
//
// Outline ESP ore / nether brick / chest (+ air, lava, pasir, kerikil) tembus tembok.
//   ClientInstance::update     -> scan blok di sekitar kamera (bertahap, ada batas waktu)
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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <sys/stat.h>

#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>
#include <pl/memory/Hook.hpp>
#include <pl/memory/Signature.hpp>

namespace {

using Clock = std::chrono::steady_clock;

constexpr const char *kVersion = "1.1.0";
constexpr const char *kCredit = "floxsfox";
constexpr const char *kModuleId = "visual_outline.main";

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
constexpr auto kPassInterval = std::chrono::milliseconds(700);
constexpr auto kFrameScanBudget = std::chrono::microseconds(1200);
constexpr float kPopSeconds = 0.45f;
constexpr size_t kFrameSegBudgetThin = 45000;
constexpr size_t kFrameSegBudgetThick = 9000;
constexpr size_t kBatchSegs = 2500;

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
// tidak disentuh dari sisi mod (layout harus sama dengan game agar ABI return cocok).
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

struct ClassLines {
    std::vector<float> v;    // x1,y1,z1,x2,y2,z2 per garis (dunia)
    std::vector<float> born; // detik (sejak mod dimuat) garis pertama kali terdeteksi; kosong = tanpa pop
};
struct Snapshot {
    std::array<ClassLines, vo::kClassCount> cls;
};

struct Settings {
    std::atomic_bool moduleOn{true};
    std::array<std::atomic_bool, vo::kFeatureCount> feature;
    std::atomic_bool pop{true};
    std::atomic_int level{1};     // 1 = bawaan (tipis), 2 = terang + tebal rapi
    std::atomic_int radius{16};   // blok
    std::atomic_int speed{8};     // ribu blok per frame (juga dibatasi waktu)
    std::atomic_int material{0};  // 0 selection_box, 1 ui_fill_color
    Settings() {
        for (auto &f : feature) f = true;
    }
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

struct Seg {
    float a[3];
    float b[3];
    float rgba[4];
};

struct Globals {
    Settings cfg;

    UpdateFn origUpdate = nullptr;
    RenderLevelFn origRender = nullptr;
    GetBlockFn getBlock = nullptr;
    TessBeginFn tessBegin = nullptr;
    TessColorFn tessColor = nullptr;
    TessVertexFn tessVertex = nullptr;
    RenderMeshFn renderMesh = nullptr;
    uintptr_t renderMaterialGroup = 0;

    bool resolved = false;
    std::atomic_bool ready{false};
    std::atomic_bool probeOk{false};
    int probeFails = 0;
    Clock::time_point nextProbe{};
    Clock::time_point t0 = Clock::now();

    int scanFaults = 0;
    std::atomic_bool scanDisabled{false};
    int drawFaults = 0;
    std::atomic_bool drawBroken{false};

    std::atomic<void *> clientInstance{nullptr};
    std::atomic<float> cam[3]{0.f, 0.f, 0.f};
    std::atomic_bool camValid{false};

    ScanState scan;
    std::unordered_map<uintptr_t, uint8_t> blockClass; // Block* -> kelas
    uintptr_t lastBlock = 0;
    uint8_t lastClass = 0;
    std::unordered_map<uint64_t, float> birth;         // kunci garis -> waktu terdeteksi
    bool suppressPop = true;                           // pass pertama di dunia baru tidak pop

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

float nowSec() {
    return std::chrono::duration<float>(Clock::now() - g.t0).count();
}

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

void publish(std::shared_ptr<const Snapshot> s) {
    std::lock_guard<std::mutex> lock(g.snapMutex);
    g.snapshot = std::move(s);
}

std::shared_ptr<const Snapshot> currentSnapshot() {
    std::lock_guard<std::mutex> lock(g.snapMutex);
    return g.snapshot;
}

// Validasi rantai: getBlock di kolom bawah kamera harus mengembalikan block bernama
// "namespace:nama" dan minimal satu bukan udara. getBlock dijalankan di dalam guarded().
// Mengembalikan: 1 = lolos, 0 = gagal validasi, -1 = fault tertangkap.
int runProbe(void *blockSource) {
    const int bx = static_cast<int>(std::floor(g.cam[0].load()));
    const int by = static_cast<int>(std::floor(g.cam[1].load()));
    const int bz = static_cast<int>(std::floor(g.cam[2].load()));
    int pos[48][3];
    const void *res[48];
    int n = 0;
    for (int y = std::min(by, kMaxY); y > by - 48 && y >= kMinY && n < 48; --y) {
        pos[n][0] = bx;
        pos[n][1] = y;
        pos[n][2] = bz;
        ++n;
    }
    const bool ok = vo::engine::guarded([&] {
        for (int i = 0; i < n; ++i) res[i] = g.getBlock(blockSource, pos[i]);
    });
    if (!ok) return -1;
    int valid = 0, nonAir = 0;
    std::string samples;
    for (int i = 0; i < n; ++i) {
        if (!res[i]) continue;
        std::string name;
        if (!readBlockName(res[i], name) || name.find(':') == std::string::npos) continue;
        ++valid;
        if (name != "minecraft:air") {
            ++nonAir;
            if (samples.size() < 120 && samples.find(name) == std::string::npos) samples += name + " ";
        }
    }
    VO_INFO("Probe getBlock: valid={} bukanUdara={} contoh=[{}]", valid, nonAir, samples);
    return (valid >= 3 && nonAir >= 1) ? 1 : 0;
}

void onScanFault(const char *where) {
    ++g.scanFaults;
    VO_WARN("Error tertangkap di {} (ke-{}), scan direset", where, g.scanFaults);
    g.scan = ScanState{};
    g.scan.nextPass = Clock::now() + std::chrono::seconds(3);
    publish(nullptr);
    if (g.scanFaults >= 3) {
        g.scanDisabled = true;
        VO_WARN("Terlalu banyak error, scan dimatikan sampai game dibuka ulang");
    }
}

// ---------------------------------------------------------------- scan (thread update)
void buildSnapshot(const ScanState &st) {
    std::array<bool, vo::kClassCount> enabled{};
    for (int c = 1; c < vo::kClassCount; ++c) enabled[c] = g.cfg.feature[vo::kClasses[c].feature].load();

    vo::LineSet lines;
    vo::buildOutlines(st.grid, st.radius, enabled, kMaxFloatsPerClass, st.ox, st.oy, st.oz, lines);
    vo::buildBoxes(st.grid, st.radius, enabled, kMaxFloatsPerClass, st.ox, st.oy, st.oz, lines);

    auto snap = std::make_shared<Snapshot>();
    const float now = nowSec();
    std::unordered_map<uint64_t, float> newBirth;
    size_t total = 0;
    for (int c = 1; c < vo::kClassCount; ++c) {
        ClassLines &cl = snap->cls[c];
        cl.v = std::move(lines[c]);
        const size_t n = cl.v.size() / 6;
        total += n;
        if (!vo::kClasses[c].pop || n == 0) continue;
        cl.born.resize(n);
        for (size_t i = 0; i < n; ++i) {
            const uint64_t key = vo::lineKey(static_cast<uint8_t>(c), &cl.v[i * 6]);
            const auto it = g.birth.find(key);
            const float b = (it != g.birth.end()) ? it->second : (g.suppressPop ? now - 10.f : now);
            cl.born[i] = b;
            newBirth.emplace(key, b);
        }
    }
    g.birth = std::move(newBirth);
    g.suppressPop = false;

    if (!g.loggedFirstSnapshot.exchange(true)) VO_INFO("Scan pertama selesai: {} garis outline", total);
    publish(std::move(snap));
}

void scanTick() {
    if (!g.cfg.moduleOn.load() || g.scanDisabled.load() || !g.camValid.load()) return;
    void *ci = g.clientInstance.load();
    if (!ci) return;

    void *bs = getRegion(ci);
    if (!bs || reinterpret_cast<uintptr_t>(bs) < 0x10000) {
        if (g.scan.blockSource) {
            g.scan = ScanState{};
            g.birth.clear();
            g.suppressPop = true;
            publish(nullptr);
        }
        return;
    }

    const auto now = Clock::now();
    if (!g.probeOk.load()) {
        if (g.probeFails >= 5 || now < g.nextProbe) return;
        const int r = runProbe(bs);
        if (r == 1) {
            g.probeOk = true;
            VO_INFO("Visual Outline aktif");
        } else {
            ++g.probeFails;
            g.nextProbe = now + std::chrono::seconds(3);
            if (g.probeFails >= 5) VO_WARN("Probe gagal 5x, dinonaktifkan (signature getBlock tidak cocok dengan versi ini)");
        }
        return;
    }

    ScanState &st = g.scan;
    if (st.blockSource != bs) { // dunia/dimensi berganti
        const auto keepNext = st.nextPass;
        st = ScanState{};
        st.nextPass = keepNext;
        st.blockSource = bs;
        g.birth.clear();
        g.suppressPop = true;
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

    const size_t maxBlocks = static_cast<size_t>(std::clamp(g.cfg.speed.load(), 3, 30)) * 1000;
    const auto t0 = Clock::now();
    const size_t total = st.grid.total();
    const int side = st.grid.side;
    const int H = st.grid.half;

    constexpr size_t kChunk = 256;
    int pos[kChunk][3];
    const void *res[kChunk];
    uint32_t cellIdx[kChunk];
    size_t consumed = 0;

    while (st.cursor < total && consumed < maxBlocks) {
        size_t n = 0;
        while (n < kChunk && st.cursor < total && consumed < maxBlocks) {
            const size_t i = st.cursor++;
            ++consumed;
            const int z = static_cast<int>(i % side);
            const int y = static_cast<int>((i / side) % side);
            const int x = static_cast<int>(i / (static_cast<size_t>(side) * side));
            const int wy = st.oy + y - H;
            if (wy < kMinY || wy > kMaxY) continue;
            pos[n][0] = st.ox + x - H;
            pos[n][1] = wy;
            pos[n][2] = st.oz + z - H;
            cellIdx[n] = static_cast<uint32_t>(i);
            ++n;
        }
        if (n) {
            const bool ok = vo::engine::guarded([&] {
                for (size_t k = 0; k < n; ++k) res[k] = g.getBlock(bs, pos[k]);
            });
            if (!ok) {
                onScanFault("getBlock");
                return;
            }
            for (size_t k = 0; k < n; ++k) {
                if (!res[k]) continue;
                const uint8_t c = classOfBlock(res[k]);
                if (c) st.grid.cells[cellIdx[k]] = c;
            }
        }
        if (Clock::now() - t0 > kFrameScanBudget) break;
    }
    if (st.cursor < total) return;

    buildSnapshot(st);
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

// Hanya material milik game (hasil lookup). Tidak ada fallback ke offset embedded:
// pointer yang salah di renderMesh = crash. Kalau material tidak ada, frame dilewati.
// mode 0: selection_box, mode 1: ui_fill_color (ada blending alpha; jatuh ke selection_box).
void *pickMaterial(int mode) {
    const int order[2][2] = {{0, -1}, {1, 0}};
    for (const int slotIdx : order[mode == 1 ? 1 : 0]) {
        if (slotIdx < 0) continue;
        MaterialPtr &slot = g.mat[slotIdx];
        if (!slot && g.matFetchFails[slotIdx] < 30) {
            slot = fetchMaterial(slotIdx == 0 ? "selection_box" : "ui_fill_color");
            if (!slot) ++g.matFetchFails[slotIdx];
        }
        if (slot) return &slot;
    }
    return nullptr;
}

// Warna shader diset putih selama menggambar, lalu dikembalikan (perilaku sama dengan
// ScopedShaderColor milik BedrockTools).
struct ShaderColorGuard {
    unsigned char *holder = nullptr;
    float saved[4]{};
    explicit ShaderColorGuard(void *screenContext) {
        uintptr_t h = 0;
        if (!vo::engine::safeReadValue(reinterpret_cast<uintptr_t>(screenContext) + kScreenContextColorHolderOff, h) || h < 0x10000) return;
        unsigned char raw[kShaderColorDirtyOff + 1];
        if (!vo::engine::safeRead(reinterpret_cast<void *>(h), raw, sizeof(raw))) return;
        holder = reinterpret_cast<unsigned char *>(h);
        std::memcpy(saved, raw, sizeof(saved));
        const float white[4] = {1.f, 1.f, 1.f, 1.f};
        std::memcpy(holder, white, sizeof(white));
        holder[kShaderColorDirtyOff] = 1;
    }
    ~ShaderColorGuard() {
        if (!holder) return;
        std::memcpy(holder, saved, sizeof(saved));
        holder[kShaderColorDirtyOff] = 1;
    }
    ShaderColorGuard(const ShaderColorGuard &) = delete;
    ShaderColorGuard &operator=(const ShaderColorGuard &) = delete;
};

// Kirim segmen ke tessellator. thick=false: LineList 1px. thick=true: quad menghadap
// kamera, dua arah putar (mode primitif 1 = QuadList).
void submit(void *tess, void *screenContext, void *material, const std::vector<Seg> &segs, bool thick,
            bool perSegColor, const float base[4]) {
    for (size_t first = 0; first < segs.size(); first += kBatchSegs) {
        const size_t m = std::min(kBatchSegs, segs.size() - first);
        size_t emitted = 0;
        if (!thick) {
            g.tessBegin(tess, nullptr, 4, static_cast<int>(m * 2), 0);
            g.tessColor(tess, base[0], base[1], base[2], base[3]);
            for (size_t i = 0; i < m; ++i) {
                const Seg &s = segs[first + i];
                if (perSegColor) g.tessColor(tess, s.rgba[0], s.rgba[1], s.rgba[2], s.rgba[3]);
                g.tessVertex(tess, s.a[0], s.a[1], s.a[2]);
                g.tessVertex(tess, s.b[0], s.b[1], s.b[2]);
                emitted += 2;
            }
        } else {
            g.tessBegin(tess, nullptr, 1, static_cast<int>(m * 8), 0);
            g.tessColor(tess, base[0], base[1], base[2], base[3]);
            for (size_t i = 0; i < m; ++i) {
                const Seg &s = segs[first + i];
                float q[24];
                if (!vo::beamQuads(s.a, s.b, q)) continue;
                if (perSegColor) g.tessColor(tess, s.rgba[0], s.rgba[1], s.rgba[2], s.rgba[3]);
                for (int k = 0; k < 8; ++k) g.tessVertex(tess, q[k * 3], q[k * 3 + 1], q[k * 3 + 2]);
                emitted += 8;
            }
            while (emitted == 0) { // jangan kirim mesh kosong: satu quad tanpa luas
                for (int k = 0; k < 4; ++k) g.tessVertex(tess, 0.f, 0.f, -1.f);
                emitted = 4;
            }
        }
        char pad[0x58];
        std::memset(pad, 0, sizeof(pad));
        g.renderMesh(screenContext, tess, material, pad);
    }
}

std::vector<Seg> gNormal; // dipakai ulang tiap frame (thread render)
std::vector<Seg> gYoung;

void drawCore(void *levelRenderer, void *screenContext) {
    uintptr_t lrp = 0;
    if (!vo::engine::safeReadValue(reinterpret_cast<uintptr_t>(levelRenderer) + kLevelRendererPlayerOff, lrp) || lrp < 0x1000) return;
    float cam[3];
    if (!vo::engine::safeRead(reinterpret_cast<void *>(lrp + kCamPosOff), cam, sizeof(cam))) return;
    for (int i = 0; i < 3; ++i) g.cam[i].store(cam[i], std::memory_order_relaxed);
    g.camValid.store(true, std::memory_order_relaxed);

    if (!g.probeOk.load() || g.scanDisabled.load()) return;
    const auto snap = currentSnapshot();
    if (!snap) return;

    uintptr_t tess = 0;
    if (!vo::engine::safeReadValue(reinterpret_cast<uintptr_t>(screenContext) + kScreenContextTessellatorOff, tess) || tess < 0x1000) return;
    void *tessPtr = reinterpret_cast<void *>(tess);

    const int material = g.cfg.material.load();
    void *mat = pickMaterial(material);
    if (!mat) return;
    const bool blend = material == 1;

    const bool thick = g.cfg.level.load() >= 2;
    const bool pop = g.cfg.pop.load();
    const float now = nowSec();
    size_t budget = thick ? kFrameSegBudgetThick : kFrameSegBudgetThin;

    ShaderColorGuard guard(screenContext);

    size_t drawn = 0;
    for (int c = 1; c < vo::kClassCount && budget > 0; ++c) {
        const ClassLines &cl = snap->cls[c];
        const size_t n = cl.v.size() / 6;
        if (n == 0) continue;
        const uint32_t rgb = thick ? vo::kClasses[c].rgb2 : vo::kClasses[c].rgb1;
        const float base[4] = {static_cast<float>((rgb >> 16) & 0xFF) / 255.f, static_cast<float>((rgb >> 8) & 0xFF) / 255.f,
                               static_cast<float>(rgb & 0xFF) / 255.f, 1.0f};
        const bool canPop = pop && vo::kClasses[c].pop && cl.born.size() == n;

        gNormal.clear();
        gYoung.clear();
        for (size_t i = 0; i < n && budget > 0; ++i) {
            const float *p = &cl.v[i * 6];
            Seg s;
            s.a[0] = p[0] - cam[0]; s.a[1] = p[1] - cam[1]; s.a[2] = p[2] - cam[2];
            s.b[0] = p[3] - cam[0]; s.b[1] = p[4] - cam[1]; s.b[2] = p[5] - cam[2];
            --budget;
            if (canPop) {
                const float age = now - cl.born[i];
                if (age < kPopSeconds) {
                    const float t = std::clamp(age / kPopSeconds, 0.0f, 1.0f);
                    const float sc = std::max(0.05f, vo::easeOutBack(t));
                    for (int k = 0; k < 3; ++k) {
                        const float mid = (s.a[k] + s.b[k]) * 0.5f;
                        const float half = (s.b[k] - s.a[k]) * 0.5f * sc;
                        s.a[k] = mid - half;
                        s.b[k] = mid + half;
                    }
                    const float flash = (1.0f - t) * 0.65f;
                    s.rgba[0] = base[0] + (1.0f - base[0]) * flash;
                    s.rgba[1] = base[1] + (1.0f - base[1]) * flash;
                    s.rgba[2] = base[2] + (1.0f - base[2]) * flash;
                    s.rgba[3] = blend ? std::clamp(t * 3.0f, 0.15f, 1.0f) : 1.0f;
                    gYoung.push_back(s);
                    continue;
                }
            }
            gNormal.push_back(s);
        }
        submit(tessPtr, screenContext, mat, gNormal, thick, false, base);
        submit(tessPtr, screenContext, mat, gYoung, thick, true, base);
        drawn += gNormal.size() + gYoung.size();
    }
    if (drawn && !g.loggedFirstDraw.exchange(true)) VO_INFO("Gambar pertama: {} garis", drawn);
}

void renderLevelHook(void *self, void *screenContext, void *a3) {
    if (g.origRender) g.origRender(self, screenContext, a3);
    if (!g.cfg.moduleOn.load() || g.drawBroken.load() || !self || !screenContext) return;
    if (reinterpret_cast<uintptr_t>(screenContext) < 0x1000) return;
    try {
        const bool ok = vo::engine::guarded([&] { drawCore(self, screenContext); });
        if (!ok) {
            ++g.drawFaults;
            VO_WARN("Error tertangkap saat menggambar (ke-{})", g.drawFaults);
            if (g.drawFaults >= 2) {
                g.drawBroken = true;
                VO_WARN("Gambar dimatikan sampai game dibuka ulang");
            }
        }
    } catch (...) {
    }
}

// ---------------------------------------------------------------- menu
constexpr const char *kFeatureKeys[vo::kFeatureCount] = {
    "coal", "iron", "copper", "gold", "redstone", "lapis", "diamond", "emerald",
    "quartz", "debris", "nether_brick", "lava", "water", "sand", "gravel", "chest"};

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

// ---- penyimpanan pengaturan (file teks key=value di folder data aplikasi)
std::mutex gCfgMutex;

std::string processName() {
    char buf[256] = {0};
    FILE *f = std::fopen("/proc/self/cmdline", "rb");
    if (!f) return {};
    const size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    buf[n] = '\0';
    return std::string(buf); // sampai NUL pertama
}

// Kandidat lokasi file; yang pertama bisa ditulis dipakai.
std::vector<std::string> configCandidates() {
    std::vector<std::string> out;
    const std::string pkg = processName();
    if (pkg.empty()) return out;
    out.push_back("/data/data/" + pkg + "/files/visual_outline.cfg");
    out.push_back("/storage/emulated/0/Android/data/" + pkg + "/files/visual_outline.cfg");
    return out;
}

bool applyKey(std::string_view key, std::string_view value) {
    for (int i = 0; i < vo::kFeatureCount; ++i) {
        if (key == kFeatureKeys[i]) {
            g.cfg.feature[i] = parseBool(value, g.cfg.feature[i].load());
            return true;
        }
    }
    if (key == "enabled") g.cfg.moduleOn = parseBool(value, g.cfg.moduleOn.load());
    else if (key == "pop") g.cfg.pop = parseBool(value, g.cfg.pop.load());
    else if (key == "level") g.cfg.level = std::clamp(parseInt(value, g.cfg.level.load()), 1, 2);
    else if (key == "radius") g.cfg.radius = std::clamp(parseInt(value, g.cfg.radius.load()), 8, 32);
    else if (key == "speed") g.cfg.speed = std::clamp(parseInt(value, g.cfg.speed.load()), 3, 30);
    else if (key == "material") g.cfg.material = std::clamp(parseInt(value, g.cfg.material.load()), 0, 1);
    else return false;
    return true;
}

void loadConfig() {
    std::lock_guard<std::mutex> lock(gCfgMutex);
    for (const auto &path : configCandidates()) {
        FILE *f = std::fopen(path.c_str(), "rb");
        if (!f) continue;
        char line[128];
        while (std::fgets(line, sizeof(line), f)) {
            std::string_view sv(line);
            while (!sv.empty() && (sv.back() == '\n' || sv.back() == '\r' || sv.back() == ' ')) sv.remove_suffix(1);
            const size_t eq = sv.find('=');
            if (eq == std::string_view::npos) continue;
            applyKey(sv.substr(0, eq), sv.substr(eq + 1));
        }
        std::fclose(f);
        return;
    }
}

void saveConfig() {
    std::lock_guard<std::mutex> lock(gCfgMutex);
    std::string text;
    auto put = [&](const char *k, const std::string &v) { text += std::string(k) + "=" + v + "\n"; };
    auto b = [](bool v) { return std::string(v ? "true" : "false"); };
    put("enabled", b(g.cfg.moduleOn.load()));
    put("pop", b(g.cfg.pop.load()));
    put("level", std::to_string(g.cfg.level.load()));
    put("radius", std::to_string(g.cfg.radius.load()));
    put("speed", std::to_string(g.cfg.speed.load()));
    put("material", std::to_string(g.cfg.material.load()));
    for (int i = 0; i < vo::kFeatureCount; ++i) put(kFeatureKeys[i], b(g.cfg.feature[i].load()));
    for (const auto &path : configCandidates()) {
        const size_t slash = path.find_last_of('/');
        if (slash != std::string::npos) ::mkdir(path.substr(0, slash).c_str(), 0771);
        FILE *f = std::fopen(path.c_str(), "wb");
        if (!f) continue;
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
        return;
    }
}

void onModuleToggle(std::string_view moduleId, bool enabled) {
    if (moduleId != kModuleId) return;
    g.cfg.moduleOn = enabled;
    if (!enabled) publish(nullptr);
    saveConfig();
}

void onConfigChanged(std::string_view moduleId, std::string_view key, std::string_view value) {
    if (moduleId != kModuleId) return;
    if (applyKey(key, value)) saveConfig();
}

// ---------------------------------------------------------------- mod
class VisualOutlineMod {
public:
    static VisualOutlineMod &instance() {
        static VisualOutlineMod mod;
        return mod;
    }

    VisualOutlineMod() : mSelf(*ll::mod::NativeMod::current()) { gSelf = &mSelf; }

    [[nodiscard]] ll::mod::NativeMod &getSelf() const { return mSelf; }

    bool load() { return true; }

    bool enable() {
        auto &self = getSelf();
        self.getLogger().info("Visual Outline v{} by {}", kVersion, kCredit);

        loadConfig();

        using pl::modmenu::ConfigType;
        auto on = [](bool v) { return std::string(v ? "true" : "false"); };
        auto num = [](int v) { return std::to_string(v); };
        auto &c = g.cfg;
        const bool menuOk =
            pl::modmenu::ModuleBuilder(kModuleId, "Visual Outline")
                .modId(self.getId())
                .description("v1.1.0 - by floxsfox | ESP outline ore, brick nether, chest")
                .defaultEnabled(c.moduleOn.load())
                .onToggle(onModuleToggle)
                .config("level", "Level warna (1 bawaan, 2 terang+rapi)", ConfigType::SliderInt, num(c.level.load()), "1", "2")
                .config("pop", "Animasi pop saat terdeteksi", ConfigType::Toggle, on(c.pop.load()))
                .config("material", "Material (0 selection_box, 1 ui_fill_color)", ConfigType::SliderInt, num(c.material.load()), "0", "1")
                .config("radius", "Jarak scan (blok)", ConfigType::SliderInt, num(c.radius.load()), "8", "32")
                .config("speed", "Kecepatan scan (ribu blok/frame)", ConfigType::SliderInt, num(c.speed.load()), "3", "30")
                .config("coal", "ESP: Coal", ConfigType::Toggle, on(c.feature[vo::FCoal].load()))
                .config("iron", "ESP: Iron", ConfigType::Toggle, on(c.feature[vo::FIron].load()))
                .config("copper", "ESP: Copper", ConfigType::Toggle, on(c.feature[vo::FCopper].load()))
                .config("gold", "ESP: Gold", ConfigType::Toggle, on(c.feature[vo::FGold].load()))
                .config("redstone", "ESP: Redstone", ConfigType::Toggle, on(c.feature[vo::FRedstone].load()))
                .config("lapis", "ESP: Lapis", ConfigType::Toggle, on(c.feature[vo::FLapis].load()))
                .config("diamond", "ESP: Diamond", ConfigType::Toggle, on(c.feature[vo::FDiamond].load()))
                .config("emerald", "ESP: Emerald", ConfigType::Toggle, on(c.feature[vo::FEmerald].load()))
                .config("quartz", "ESP: Nether Quartz", ConfigType::Toggle, on(c.feature[vo::FQuartz].load()))
                .config("debris", "ESP: Ancient Debris", ConfigType::Toggle, on(c.feature[vo::FDebris].load()))
                .config("nether_brick", "ESP: Nether Brick", ConfigType::Toggle, on(c.feature[vo::FBrick].load()))
                .config("chest", "ESP: Chest", ConfigType::Toggle, on(c.feature[vo::FChest].load()))
                .config("lava", "Outline: Lava", ConfigType::Toggle, on(c.feature[vo::FLava].load()))
                .config("water", "Outline: Air", ConfigType::Toggle, on(c.feature[vo::FWater].load()))
                .config("sand", "Outline: Pasir", ConfigType::Toggle, on(c.feature[vo::FSand].load()))
                .config("gravel", "Outline: Kerikil", ConfigType::Toggle, on(c.feature[vo::FGravel].load()))
                .onConfigChanged(onConfigChanged)
                .registerModule();
        if (!menuOk) self.getLogger().error("Gagal mendaftarkan menu Visual Outline");

        vo::engine::installFaultGuard();
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
        getSelf().getLogger().info("Visual Outline dimatikan");
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
        else log.warn("RenderMaterialGroup tidak terdecode, gambar dinonaktifkan");

        g.resolved = true;
        return true;
    }
};

} // namespace

PL_REGISTER_MOD(VisualOutlineMod, VisualOutlineMod::instance())
