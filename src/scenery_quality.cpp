#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <dxgi.h>
#endif

#include "graphics.hpp"
#include "scenery_quality.hpp"

#if defined(_WIN32)
#include <cstdint>
#include <cwctype>
#include <memory>
#include <string>
#include <string_view>
#endif

namespace {

constexpr SceneryQuality web{1, 0.0f, 125.0f, 15.0f, 220.0f, 530.0f,
    {14.0f, 30.0f, 56.0f, 0.0f}, 0.0f, 23, 0.34f, 16, 2048, 63.0f, 0.0f};
constexpr SceneryQuality low{1, 0.0f, 125.0f, 10.0f, 320.0f, 740.0f,
    {14.0f, 32.0f, 64.0f, 95.0f}, 0.0f, 23, 0.34f, 16, 2048, 63.0f, 0.0f};
constexpr SceneryQuality medium{0, 245.0f, 85.0f, 6.0f, 610.0f, 1060.0f,
    {21.0f, 48.0f, 105.0f, 160.0f}, 9.0f, 29, 0.42f, 22, 3072, 82.0f, 0.0f};
constexpr SceneryQuality high{0, 165.0f, 55.0f, 3.0f, 1170.0f, 1590.0f,
    {30.0f, 65.0f, 153.0f, 315.0f}, 15.0f, 35, 0.50f, 28, 4096, 41.0f, 195.0f};

#if defined(_WIN32)
constexpr std::uint64_t gib = 1024ull * 1024ull * 1024ull;

std::wstring adapterName(std::wstring_view name)
{
    std::wstring result;
    for (const auto letter : name) {
        if (std::iswalnum(letter)) result += static_cast<wchar_t>(std::towlower(letter));
    }
    return result;
}

std::wstring glName(GLenum field)
{
    const auto* name = reinterpret_cast<const char*>(glGetString(field));
    const std::string_view raw = name ? name : "";
    return adapterName(std::wstring(raw.begin(), raw.end()));
}

std::uint64_t videoMemory(std::wstring_view renderer, std::wstring_view vendor)
{
    const auto release = [](auto* ptr) { ptr->Release(); };
    IDXGIFactory1* rawFactory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&rawFactory)))) return 0;
    const std::unique_ptr<IDXGIFactory1, decltype(release)> factory(rawFactory, release);
    const auto vendorId = vendor.find(L"nvidia") != vendor.npos ? 0x10deu
        : vendor.find(L"intel") != vendor.npos ? 0x8086u
        : vendor.find(L"amd") != vendor.npos || vendor.find(L"atitechnologies") != vendor.npos ? 0x1002u : 0u;
    std::uint64_t primaryMemory = 0, vendorMemory = 0;
    unsigned adapters = 0;
    bool foundVendor = false;
    for (UINT i = 0;; ++i) {
        IDXGIAdapter1* rawAdapter = nullptr;
        if (FAILED(factory->EnumAdapters1(i, &rawAdapter))) break;
        const std::unique_ptr<IDXGIAdapter1, decltype(release)> adapter(rawAdapter, release);
        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
        ++adapters;
        primaryMemory = desc.DedicatedVideoMemory;
        const auto name = adapterName(desc.Description);
        if (name.size() >= 6 && renderer.find(name) != renderer.npos) return primaryMemory;
        if (vendorId != 0 && desc.VendorId == vendorId) {
            if (!foundVendor || primaryMemory < vendorMemory) vendorMemory = primaryMemory;
            foundVendor = true;
        }
    }
    if (foundVendor) return vendorMemory;
    return adapters == 1 ? primaryMemory : 0;
}

const SceneryQuality& detectWindowsQuality()
{
    const auto renderer = glName(GL_RENDERER);
    for (const std::wstring_view software : {L"gdigeneric", L"llvmpipe", L"swiftshader", L"microsoftbasic"}) {
        if (renderer.find(software) != renderer.npos) return low;
    }
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (!GlobalMemoryStatusEx(&memory)) return low;
    const auto vram = videoMemory(renderer, glName(GL_VENDOR));
    if (system.dwNumberOfProcessors >= 8 && memory.ullTotalPhys >= 14 * gib && vram >= 6 * gib) return high;
    if (system.dwNumberOfProcessors >= 6 && memory.ullTotalPhys >= 7 * gib && vram >= 2 * gib) return medium;
    return low;
}
#endif

} // namespace

SceneryQuality sceneryQuality(QualityLevel preset, QualityLevel lod)
{
#ifdef __EMSCRIPTEN__
    const auto& automatic = web;
    const auto& lowPreset = web;
#elif defined(_WIN32)
    static const auto& automatic = detectWindowsQuality();
    const auto& lowPreset = low;
#else
    const auto& automatic = medium;
    const auto& lowPreset = low;
#endif
    const auto profile = [&](QualityLevel level) -> const SceneryQuality& {
        switch (level) {
        case QualityLevel::Low: return lowPreset;
        case QualityLevel::Medium: return medium;
        case QualityLevel::High: return high;
        case QualityLevel::Automatic: return automatic;
        }
        return automatic;
    };
    auto quality = profile(preset);
    if (lod != QualityLevel::Automatic) {
        const auto& scenery = profile(lod);
        quality.nearestTreeLod = scenery.nearestTreeLod;
        quality.nearTreePixels = scenery.nearTreePixels;
        quality.midTreePixels = scenery.midTreePixels;
        quality.minTreePixels = scenery.minTreePixels;
        quality.propDistance = scenery.propDistance;
        quality.viewDistance = scenery.viewDistance;
        quality.grassDistances = scenery.grassDistances;
        quality.detailedGrassRadius = scenery.detailedGrassRadius;
        quality.grassSamples = scenery.grassSamples;
    }
#ifdef __EMSCRIPTEN__
    quality.nearestTreeLod = 1;
    quality.nearTreePixels = 0.0f;
#endif
    return quality;
}
