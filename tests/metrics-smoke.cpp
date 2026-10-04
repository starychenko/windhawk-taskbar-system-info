#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <dxgi.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;

using D3DKMT_HANDLE = UINT32;

struct D3DKMT_OPENADAPTERFROMLUID {
    LUID AdapterLuid;
    D3DKMT_HANDLE hAdapter;
};

struct D3DKMT_CLOSEADAPTER {
    D3DKMT_HANDLE hAdapter;
};

struct D3DKMT_QUERYADAPTERINFO {
    D3DKMT_HANDLE hAdapter;
    UINT Type;
    void* pPrivateDriverData;
    UINT PrivateDriverDataSize;
};

struct D3DKMT_ADAPTER_PERFDATA {
    UINT PhysicalAdapterIndex;
    ULONGLONG MemoryFrequency;
    ULONGLONG MaxMemoryFrequency;
    ULONGLONG MaxMemoryFrequencyOC;
    ULONGLONG MemoryBandwidth;
    ULONGLONG PCIEBandwidth;
    ULONG FanRPM;
    ULONG Power;
    ULONG Temperature;
    UCHAR PowerStateOverride;
};

struct D3DKMT_ADAPTERINFO {
    D3DKMT_HANDLE hAdapter;
    LUID AdapterLuid;
    ULONG NumOfSources;
    BOOL bPresentMoveRegionsPreferred;
};

struct D3DKMT_ENUMADAPTERS2 {
    ULONG NumAdapters;
    D3DKMT_ADAPTERINFO* pAdapters;
};

struct D3DKMT_ADAPTERREGISTRYINFO {
    WCHAR AdapterString[MAX_PATH];
    WCHAR BiosString[MAX_PATH];
    WCHAR DacType[MAX_PATH];
    WCHAR ChipType[MAX_PATH];
};

struct D3DKMT_SEGMENTSIZEINFO {
    ULONGLONG DedicatedVideoMemorySize;
    ULONGLONG DedicatedSystemMemorySize;
    ULONGLONG SharedSystemMemorySize;
};

struct D3DKMT_ADAPTERTYPE {
    UINT Value;
};

constexpr UINT kHybridIntegratedAdapterFlag = 1u << 5;

struct GpuAdapterInfo {
    std::wstring description;
    LUID luid{};
    uint64_t dedicatedVideoMemory = 0;
    uint64_t sharedSystemMemory = 0;
    bool integrated = false;
};

using D3DKMTOpenAdapterFromLuid_t =
    LONG(WINAPI*)(D3DKMT_OPENADAPTERFROMLUID*);
using D3DKMTQueryAdapterInfo_t =
    LONG(WINAPI*)(D3DKMT_QUERYADAPTERINFO*);
using D3DKMTCloseAdapter_t =
    LONG(WINAPI*)(const D3DKMT_CLOSEADAPTER*);
using D3DKMTEnumAdapters2_t = LONG(WINAPI*)(D3DKMT_ENUMADAPTERS2*);

std::optional<GpuAdapterInfo> ReadLiveAdapter() {
    HMODULE gdi32 = LoadLibraryExW(L"gdi32.dll", nullptr,
                                   LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!gdi32) {
        return std::nullopt;
    }

    auto enumAdapters = reinterpret_cast<D3DKMTEnumAdapters2_t>(
        GetProcAddress(gdi32, "D3DKMTEnumAdapters2"));
    auto queryAdapter = reinterpret_cast<D3DKMTQueryAdapterInfo_t>(
        GetProcAddress(gdi32, "D3DKMTQueryAdapterInfo"));
    auto closeAdapter = reinterpret_cast<D3DKMTCloseAdapter_t>(
        GetProcAddress(gdi32, "D3DKMTCloseAdapter"));
    if (!enumAdapters || !queryAdapter || !closeAdapter) {
        FreeLibrary(gdi32);
        return std::nullopt;
    }

    D3DKMT_ADAPTERINFO adapters[16]{};
    D3DKMT_ENUMADAPTERS2 enumeration{std::size(adapters), adapters};
    if (enumAdapters(&enumeration) != 0) {
        FreeLibrary(gdi32);
        return std::nullopt;
    }

    std::optional<GpuAdapterInfo> selected;
    ULONG adapterCount =
        std::min<ULONG>(enumeration.NumAdapters, std::size(adapters));
    for (ULONG index = 0; index < adapterCount; index++) {
        D3DKMT_ADAPTERREGISTRYINFO registryInfo{};
        D3DKMT_QUERYADAPTERINFO registryQuery{
            adapters[index].hAdapter, 8, &registryInfo, sizeof(registryInfo)};
        bool registryAvailable = queryAdapter(&registryQuery) == 0;

        D3DKMT_SEGMENTSIZEINFO segmentInfo{};
        D3DKMT_QUERYADAPTERINFO segmentQuery{
            adapters[index].hAdapter, 3, &segmentInfo, sizeof(segmentInfo)};
        bool segmentsAvailable = queryAdapter(&segmentQuery) == 0;

        D3DKMT_ADAPTERTYPE adapterType{};
        D3DKMT_QUERYADAPTERINFO adapterTypeQuery{
            adapters[index].hAdapter, 15, &adapterType, sizeof(adapterType)};
        bool adapterTypeAvailable = queryAdapter(&adapterTypeQuery) == 0;

        GpuAdapterInfo candidate{
            registryAvailable ? registryInfo.AdapterString : L"",
            adapters[index].AdapterLuid,
            segmentsAvailable ? segmentInfo.DedicatedVideoMemorySize : 0,
            segmentsAvailable ? segmentInfo.SharedSystemMemorySize : 0,
            adapterTypeAvailable &&
                (adapterType.Value & kHybridIntegratedAdapterFlag) != 0,
        };
        std::wcout << L"D3DKMT_CANDIDATE=" << candidate.description
                   << L" LUID=0x" << std::hex
                   << static_cast<DWORD>(candidate.luid.HighPart) << L"_0x"
                   << candidate.luid.LowPart << std::dec << L" dedicated="
                   << static_cast<double>(candidate.dedicatedVideoMemory) / kGiB
                   << L" shared="
                   << static_cast<double>(candidate.sharedSystemMemory) / kGiB
                   << L" integrated=" << candidate.integrated
                   << L"\n";
        bool betterCandidate =
            !selected || candidate.dedicatedVideoMemory >
                             selected->dedicatedVideoMemory ||
            (candidate.dedicatedVideoMemory ==
                 selected->dedicatedVideoMemory &&
             !candidate.description.empty() &&
             selected->description.empty()) ||
            (candidate.dedicatedVideoMemory ==
                 selected->dedicatedVideoMemory &&
             candidate.description.empty() == selected->description.empty() &&
             candidate.sharedSystemMemory > selected->sharedSystemMemory);
        if (betterCandidate) {
            selected = std::move(candidate);
        }
    }

    for (ULONG index = 0; index < adapterCount; index++) {
        if (adapters[index].hAdapter) {
            D3DKMT_CLOSEADAPTER closeInfo{adapters[index].hAdapter};
            closeAdapter(&closeInfo);
        }
    }
    FreeLibrary(gdi32);
    return selected;
}

std::optional<double> ReadGpuTemperature(const LUID& luid) {
    HMODULE gdi32 = LoadLibraryExW(L"gdi32.dll", nullptr,
                                   LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!gdi32) {
        return std::nullopt;
    }

    auto openAdapter = reinterpret_cast<D3DKMTOpenAdapterFromLuid_t>(
        GetProcAddress(gdi32, "D3DKMTOpenAdapterFromLuid"));
    auto queryAdapter = reinterpret_cast<D3DKMTQueryAdapterInfo_t>(
        GetProcAddress(gdi32, "D3DKMTQueryAdapterInfo"));
    auto closeAdapter = reinterpret_cast<D3DKMTCloseAdapter_t>(
        GetProcAddress(gdi32, "D3DKMTCloseAdapter"));
    if (!openAdapter || !queryAdapter || !closeAdapter) {
        FreeLibrary(gdi32);
        return std::nullopt;
    }

    D3DKMT_OPENADAPTERFROMLUID openInfo{};
    openInfo.AdapterLuid = luid;
    if (openAdapter(&openInfo) != 0) {
        FreeLibrary(gdi32);
        return std::nullopt;
    }

    D3DKMT_ADAPTER_PERFDATA perfData{};
    D3DKMT_QUERYADAPTERINFO queryInfo{};
    queryInfo.hAdapter = openInfo.hAdapter;
    queryInfo.Type = 62;  // KMTQAITYPE_ADAPTERPERFDATA
    queryInfo.pPrivateDriverData = &perfData;
    queryInfo.PrivateDriverDataSize = sizeof(perfData);
    LONG status = queryAdapter(&queryInfo);

    D3DKMT_CLOSEADAPTER closeInfo{};
    closeInfo.hAdapter = openInfo.hAdapter;
    closeAdapter(&closeInfo);
    FreeLibrary(gdi32);

    if (status != 0 || perfData.Temperature == 0 ||
        perfData.Temperature > 2000) {
        return std::nullopt;
    }
    return perfData.Temperature / 10.0;
}

std::wstring ToLower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

bool Contains(const std::wstring& text, const std::wstring& needle) {
    return needle.empty() || text.find(needle) != std::wstring::npos;
}

std::wstring NormalizeAdapterIdentity(std::wstring value) {
    value = ToLower(std::move(value));
    for (wchar_t& character : value) {
        if (!std::iswalnum(character)) {
            character = L' ';
        }
    }
    std::wstring normalized;
    bool previousSpace = true;
    for (wchar_t character : value) {
        bool space = std::iswspace(character) != 0;
        if (space) {
            if (!previousSpace) {
                normalized.push_back(L' ');
            }
        } else {
            normalized.push_back(character);
        }
        previousSpace = space;
    }
    if (!normalized.empty() && normalized.back() == L' ') {
        normalized.pop_back();
    }

    std::wstring filtered;
    size_t start = 0;
    while (start < normalized.size()) {
        size_t end = normalized.find(L' ', start);
        if (end == std::wstring::npos) {
            end = normalized.size();
        }
        std::wstring_view token(normalized.data() + start, end - start);
        if (token != L"r" && token != L"tm") {
            if (!filtered.empty()) {
                filtered.push_back(L' ');
            }
            filtered.append(token);
        }
        start = end + 1;
    }
    return filtered;
}

std::vector<std::wstring> IdentityTokens(const std::wstring& value) {
    std::vector<std::wstring> tokens;
    size_t start = 0;
    while (start < value.size()) {
        size_t end = value.find(L' ', start);
        if (end == std::wstring::npos) {
            end = value.size();
        }
        if (end > start) {
            tokens.emplace_back(value.substr(start, end - start));
        }
        start = end + 1;
    }
    return tokens;
}

bool HasDigit(const std::wstring& value) {
    return std::any_of(value.begin(), value.end(), [](wchar_t character) {
        return std::iswdigit(character) != 0;
    });
}

bool LooksLikeIntegratedGpu(const GpuAdapterInfo& adapter) {
    if (adapter.integrated || adapter.dedicatedVideoMemory == 0) {
        return true;
    }

    constexpr uint64_t kMaximumIntegratedCarveout = 512ull * 1024 * 1024;
    return adapter.dedicatedVideoMemory <= kMaximumIntegratedCarveout &&
           adapter.sharedSystemMemory > adapter.dedicatedVideoMemory;
}

PDH_STATUS ReadArray(PDH_HCOUNTER counter,
               std::vector<unsigned char>& buffer,
               DWORD& count) {
    constexpr int kMaxAttempts = 4;
    DWORD size = 0;
    count = 0;
    for (int attempt = 0; attempt < kMaxAttempts; attempt++) {
        auto* items = buffer.empty()
                          ? nullptr
                          : reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(
                                buffer.data());
        PDH_STATUS status = PdhGetFormattedCounterArrayW(
            counter, PDH_FMT_DOUBLE, &size, &count, items);
        if (status == ERROR_SUCCESS) {
            return status;
        }
        if (status != static_cast<PDH_STATUS>(PDH_MORE_DATA) || !size) {
            return status;
        }
        buffer.resize(size);
    }
    return static_cast<PDH_STATUS>(PDH_MORE_DATA);
}

bool ValidCounterValue(const PDH_FMT_COUNTERVALUE& value) {
    return (value.CStatus == PDH_CSTATUS_VALID_DATA || value.CStatus == PDH_CSTATUS_NEW_DATA) &&
           std::isfinite(value.doubleValue) && value.doubleValue >= 0;
}

std::optional<double> GpuMemory(PDH_STATUS status,
                                const PDH_FMT_COUNTERVALUE_ITEM_W* items,
                                DWORD count, const std::wstring& luid) {
    if (status != ERROR_SUCCESS) return std::nullopt;
    double bytes = 0;
    bool found = false;
    for (DWORD i = 0; i < count; ++i) {
        auto name = items[i].szName ? ToLower(items[i].szName) : L"";
        if (name.find(luid) == std::wstring::npos || !ValidCounterValue(items[i].FmtValue)) continue;
        bytes += items[i].FmtValue.doubleValue; found = true;
    }
    return found && std::isfinite(bytes) ? std::optional(bytes) : std::nullopt;
}

std::optional<double> GpuUsage(PDH_STATUS status,
                               const PDH_FMT_COUNTERVALUE_ITEM_W* items,
                               DWORD count, const std::wstring& luid,
                               bool memoryAvailable) {
    if (status != ERROR_SUCCESS && status != static_cast<PDH_STATUS>(PDH_NO_DATA) &&
        status != static_cast<PDH_STATUS>(PDH_CSTATUS_NO_INSTANCE)) return std::nullopt;
    std::unordered_map<std::wstring, double> engines;
    bool matched = false;
    if (status == ERROR_SUCCESS) for (DWORD i = 0; i < count; ++i) {
        auto name = items[i].szName ? ToLower(items[i].szName) : L"";
        if (name.find(luid) == std::wstring::npos) continue;
        matched = true;
        if (!ValidCounterValue(items[i].FmtValue)) continue;
        auto start = name.find(L"luid_");
        engines[start == std::wstring::npos ? name : name.substr(start)] += items[i].FmtValue.doubleValue;
    }
    if (engines.empty()) return !matched && memoryAvailable ? std::optional(0.0) : std::nullopt;
    double usage = 0;
    for (const auto& [engine, value] : engines) {
        if (!std::isfinite(value)) return std::nullopt;
        usage = std::max(usage, value);
    }
    return std::clamp(usage, 0.0, 100.0);
}

bool MetricValidationChecks() {
    const std::wstring luid = L"luid_0x00000000_0x00000042";
    wchar_t engine[] = L"pid_1_luid_0x00000000_0x00000042_phys_0_eng_0";
    PDH_FMT_COUNTERVALUE_ITEM_W item{};
    item.szName = engine; item.FmtValue.CStatus = PDH_CSTATUS_VALID_DATA; item.FmtValue.doubleValue = 42;
    if (GpuUsage(ERROR_SUCCESS, &item, 1, luid, true) != 42 ||
        GpuMemory(ERROR_SUCCESS, &item, 1, luid) != 42 ||
        GpuUsage(PDH_INVALID_HANDLE, nullptr, 0, luid, true) ||
        GpuUsage(PDH_MORE_DATA, nullptr, 0, luid, true) ||
        GpuUsage(PDH_NO_DATA, nullptr, 0, luid, true) != 0 ||
        GpuUsage(PDH_CSTATUS_NO_INSTANCE, nullptr, 0, luid, true) != 0 ||
        GpuUsage(ERROR_SUCCESS, nullptr, 0, luid, true) != 0 ||
        GpuUsage(PDH_NO_DATA, nullptr, 0, luid, false)) return false;
    item.FmtValue.CStatus = PDH_CSTATUS_INVALID_DATA;
    if (GpuUsage(ERROR_SUCCESS, &item, 1, luid, true) || GpuMemory(ERROR_SUCCESS, &item, 1, luid)) return false;
    item.FmtValue.CStatus = PDH_CSTATUS_NEW_DATA; item.FmtValue.doubleValue = NAN;
    if (GpuUsage(ERROR_SUCCESS, &item, 1, luid, true) || GpuMemory(ERROR_SUCCESS, &item, 1, luid)) return false;
    item.FmtValue.doubleValue = -1;
    if (GpuUsage(ERROR_SUCCESS, &item, 1, luid, true) || GpuMemory(ERROR_SUCCESS, &item, 1, luid)) return false;
    item.FmtValue.doubleValue = 0;
    return GpuUsage(ERROR_SUCCESS, &item, 1, luid, true) == 0 &&
           GpuMemory(ERROR_SUCCESS, &item, 1, luid) == 0 &&
           !GpuMemory(PDH_INVALID_HANDLE, &item, 1, luid);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (!MetricValidationChecks()) {
        std::wcerr << L"Metric status/CStatus regression checks failed\n";
        return 8;
    }
    if (argc == 2 && std::wstring_view(argv[1]) == L"--self-test") {
        std::wcout << L"PASS: GPU hard errors, soft absence, invalid CStatus, NaN and real idle zero\n";
        return 0;
    }
    std::wstring_view fault = argc == 2 ? argv[1] : L"";
    if (argc > 2 || (argc == 2 && fault != L"--inject-engine-error" &&
                     fault != L"--inject-invalid-engine-data")) return 2;
    constexpr uint64_t kMiB = 1024ull * 1024;
    constexpr uint64_t kSyntheticSharedMemory = 8ull * 1024 * 1024 * 1024;
    if (!LooksLikeIntegratedGpu({L"Intel(R) Arc(TM) 140V GPU", {},
                                 128 * kMiB, kSyntheticSharedMemory, false}) ||
        !LooksLikeIntegratedGpu({L"Intel(R) HD Graphics 4000", {},
                                 128 * kMiB, kSyntheticSharedMemory, false}) ||
        !LooksLikeIntegratedGpu({L"AMD Radeon 890M", {}, 512 * kMiB,
                                 kSyntheticSharedMemory, false}) ||
        !LooksLikeIntegratedGpu({L"AMD Radeon(TM) 8060S Graphics", {},
                                 512 * kMiB, kSyntheticSharedMemory, false}) ||
        // The difficult legacy case stays visible: auto mode is ambiguous;
        // regression.cpp separately verifies the dedicated-memory override.
        !LooksLikeIntegratedGpu({L"AMD Radeon HD 6450", {}, 512 * kMiB,
                                  kSyntheticSharedMemory, false}) ||
        !LooksLikeIntegratedGpu({L"AMD Radeon HD 6470M", {}, 512 * kMiB,
                                  kSyntheticSharedMemory, false}) ||
        LooksLikeIntegratedGpu({L"Intel Arc A380", {}, 6ull * 1024 * 1024 *
                                                          1024,
                                kSyntheticSharedMemory, false})) {
        std::wcerr << L"Synthetic integrated-GPU classification failed\n";
        return 8;
    }

    auto liveAdapter = ReadLiveAdapter();
    if (!liveAdapter) {
        std::wcerr << L"D3DKMT adapter enumeration failed\n";
        return 1;
    }

    IDXGIFactory* factory = nullptr;
    if (FAILED(CreateDXGIFactory(__uuidof(IDXGIFactory),
                                 reinterpret_cast<void**>(&factory)))) {
        std::wcerr << L"DXGI factory failed\n";
        return 1;
    }

    DXGI_ADAPTER_DESC selected{};
    bool found = false;
    for (UINT index = 0;; index++) {
        IDXGIAdapter* adapter = nullptr;
        HRESULT result = factory->EnumAdapters(index, &adapter);
        if (result == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (SUCCEEDED(result) && adapter) {
            DXGI_ADAPTER_DESC description{};
            if (SUCCEEDED(adapter->GetDesc(&description)) &&
                (!found || description.DedicatedVideoMemory >
                               selected.DedicatedVideoMemory)) {
                selected = description;
                found = true;
            }
            adapter->Release();
        }
    }
    factory->Release();
    if (!found ||
        (!selected.DedicatedVideoMemory && !selected.SharedSystemMemory)) {
        std::wcerr << L"No GPU memory capacity found\n";
        return 2;
    }

    wchar_t luidBuffer[32];
    swprintf(luidBuffer, std::size(luidBuffer), L"0x%08X_0x%08X",
             selected.AdapterLuid.HighPart, selected.AdapterLuid.LowPart);
    std::wstring luid = ToLower(luidBuffer);

    wchar_t liveLuidBuffer[32];
    swprintf(liveLuidBuffer, std::size(liveLuidBuffer),
             L"0x%08X_0x%08X", liveAdapter->luid.HighPart,
             liveAdapter->luid.LowPart);
    std::wstring liveLuid = ToLower(liveLuidBuffer);
    selected.AdapterLuid = liveAdapter->luid;
    selected.DedicatedVideoMemory = liveAdapter->dedicatedVideoMemory;
    selected.SharedSystemMemory = liveAdapter->sharedSystemMemory;
    if (!liveAdapter->description.empty()) {
        wcsncpy_s(selected.Description, liveAdapter->description.c_str(),
                  _TRUNCATE);
    }
    luid = liveLuid;

    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER cpuUtilityCounter = nullptr;
    PDH_HCOUNTER gpuCounter = nullptr;
    PDH_HCOUNTER vramCounter = nullptr;
    PDH_HCOUNTER sharedVramCounter = nullptr;
    PDH_HCOUNTER thermalCounter = nullptr;
    if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS ||
        PdhAddEnglishCounterW(query,
                              L"\\GPU Engine(*)\\Utilization Percentage", 0,
                              &gpuCounter) != ERROR_SUCCESS ||
        PdhAddEnglishCounterW(query,
                              L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0,
                              &vramCounter) != ERROR_SUCCESS ||
        PdhAddEnglishCounterW(query,
                              L"\\GPU Adapter Memory(*)\\Shared Usage", 0,
                              &sharedVramCounter) != ERROR_SUCCESS) {
        std::wcerr << L"PDH setup failed\n";
        if (query) {
            PdhCloseQuery(query);
        }
        return 3;
    }

    PDH_STATUS cpuUtilityStatus = PdhAddEnglishCounterW(
        query, L"\\Processor Information(_Total)\\% Processor Utility", 0,
        &cpuUtilityCounter);
    if (cpuUtilityStatus != ERROR_SUCCESS) {
        cpuUtilityCounter = nullptr;
    }

    PDH_STATUS thermalStatus = PdhAddEnglishCounterW(
        query, L"\\Thermal Zone Information(*)\\Temperature", 0,
        &thermalCounter);
    if (thermalStatus != ERROR_SUCCESS) {
        thermalCounter = nullptr;
    }

    PdhCollectQueryData(query);
    // Raw memory gauges need one collect, unlike utilization rate counters.
    // Exercise the premise used by the production fresh-query recovery probe.
    {
        std::vector<unsigned char> firstBuffer;
        DWORD firstCount = 0;
        bool firstMemoryFound = false;
        bool shared = LooksLikeIntegratedGpu(*liveAdapter);
        if (ReadArray(shared ? sharedVramCounter : vramCounter, firstBuffer, firstCount) == ERROR_SUCCESS) {
            auto* firstItems = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(firstBuffer.data());
            for (DWORD i = 0; i < firstCount; ++i) {
                const auto& item = firstItems[i];
                std::wstring name = item.szName ? ToLower(item.szName) : L"";
                if (name.find(luid) != std::wstring::npos &&
                    (item.FmtValue.CStatus == PDH_CSTATUS_VALID_DATA ||
                     item.FmtValue.CStatus == PDH_CSTATUS_NEW_DATA) &&
                    std::isfinite(item.FmtValue.doubleValue) && item.FmtValue.doubleValue >= 0) {
                    firstMemoryFound = true;
                }
            }
        }
        std::wcout << L"FRESH_MEMORY_ONE_COLLECT="
                   << (firstMemoryFound ? L"available" : L"unavailable") << L"\n";
    }
    Sleep(1100);
    if (PdhCollectQueryData(query) != ERROR_SUCCESS) {
        PdhCloseQuery(query);
        return 4;
    }

    std::optional<double> cpuUtility;
    if (cpuUtilityCounter) {
        PDH_FMT_COUNTERVALUE value{};
        cpuUtilityStatus = PdhGetFormattedCounterValue(
            cpuUtilityCounter, PDH_FMT_DOUBLE, nullptr, &value);
        if (cpuUtilityStatus == ERROR_SUCCESS &&
            (value.CStatus == PDH_CSTATUS_VALID_DATA ||
             value.CStatus == PDH_CSTATUS_NEW_DATA) &&
            std::isfinite(value.doubleValue) && value.doubleValue >= 0.0) {
            cpuUtility = std::clamp(value.doubleValue, 0.0, 100.0);
        }
    }

    std::vector<unsigned char> buffer;
    DWORD count = 0;
    std::vector<unsigned char> gpuBuffer;
    DWORD gpuCount = 0;
    auto gpuStatus = ReadArray(gpuCounter, gpuBuffer, gpuCount);
    // Exercise the same failure/exit path as the live run, without changing
    // the system counters. Fault injection exists only in this smoke program.
    std::wstring injectedName = L"pid_1_" + luid + L"_phys_0_eng_0";
    if (fault == L"--inject-engine-error") gpuStatus = static_cast<PDH_STATUS>(PDH_INVALID_HANDLE);
    if (fault == L"--inject-invalid-engine-data") {
        gpuBuffer.resize(sizeof(PDH_FMT_COUNTERVALUE_ITEM_W));
        auto* item = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(gpuBuffer.data());
        *item = {}; item->szName = injectedName.data();
        item->FmtValue.CStatus = PDH_CSTATUS_INVALID_DATA;
        item->FmtValue.doubleValue = 42;
        gpuCount = 1; gpuStatus = ERROR_SUCCESS;
    }

    buffer.clear();
    count = 0;
    bool useDedicatedMemory =
        !LooksLikeIntegratedGpu(*liveAdapter) &&
        selected.DedicatedVideoMemory > 0;
    PDH_HCOUNTER selectedMemoryCounter =
        useDedicatedMemory ? vramCounter : sharedVramCounter;
    auto memoryStatus = ReadArray(selectedMemoryCounter, buffer, count);
    auto vramBytes = GpuMemory(memoryStatus,
        reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data()), count, luid);
    auto gpuUsage = GpuUsage(gpuStatus,
        reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(gpuBuffer.data()), gpuCount, luid, vramBytes.has_value());
    if (!gpuUsage || !vramBytes) {
        std::wcerr << L"GPU metrics unavailable: engine_status=0x" << std::hex
                   << static_cast<unsigned long>(gpuStatus) << L" memory_status=0x"
                   << static_cast<unsigned long>(memoryStatus) << std::dec
                   << L" (matching values must have valid CStatus)\n";
        PdhCloseQuery(query);
        return 5;
    }

    buffer.clear();
    count = 0;
    double thermalSumCelsius = 0.0;
    double thermalHottestCelsius = 0.0;
    size_t thermalCount = 0;
    if (ReadArray(thermalCounter, buffer, count) == ERROR_SUCCESS) {
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(
            buffer.data());
        for (DWORD i = 0; i < count; i++) {
            const auto& value = items[i].FmtValue;
            if ((value.CStatus != PDH_CSTATUS_VALID_DATA &&
                 value.CStatus != PDH_CSTATUS_NEW_DATA) ||
                !std::isfinite(value.doubleValue) ||
                value.doubleValue < 200.0 || value.doubleValue > 473.15) {
                continue;
            }

            double celsius = value.doubleValue - 273.15;
            thermalSumCelsius += celsius;
            thermalHottestCelsius =
                thermalCount ? std::max(thermalHottestCelsius, celsius)
                             : celsius;
            thermalCount++;
        }
    }
    PdhCloseQuery(query);

    uint64_t totalBytes = useDedicatedMemory
                              ? selected.DedicatedVideoMemory
                              : selected.SharedSystemMemory;
    double totalGb = static_cast<double>(totalBytes) / kGiB;
    double usedGb = *vramBytes / kGiB;
    double percent = totalGb > 0.0 ? usedGb / totalGb * 100.0 : 0.0;
    auto gpuTemperature = ReadGpuTemperature(selected.AdapterLuid);

    if (cpuUtility) {
        std::wcout << L"CPU_UTILITY=" << *cpuUtility << L"%\n";
    } else {
        std::wcout << L"CPU_UTILITY=unavailable add_status=0x" << std::hex
                   << static_cast<unsigned long>(cpuUtilityStatus) << std::dec
                   << L"\n";
    }
    std::wcout << L"GPU=" << selected.Description << L"\n"
               << L"LUID_D3DKMT=" << luid << L"\n"
               << L"GPU_INTEGRATED=" << liveAdapter->integrated << L"\n"
               << L"GPU_USAGE=" << *gpuUsage << L"%\n"
               << L"GPU_MEMORY=" << usedGb << L"/" << totalGb << L" GiB ("
               << (useDedicatedMemory ? L"dedicated, " : L"shared, ")
               << percent << L"%)\n";
    if (gpuTemperature) {
        std::wcout << L"GPU_TEMP_D3DKMT=" << *gpuTemperature << L" C\n";
    } else {
        std::wcout << L"GPU_TEMP_D3DKMT=unavailable\n";
    }
    if (thermalCount) {
        std::wcout << L"WINDOWS_THERMAL_ZONES=" << thermalCount
                   << L" average=" << thermalSumCelsius / thermalCount
                   << L" C hottest=" << thermalHottestCelsius << L" C\n";
    } else {
        std::wcout << L"WINDOWS_THERMAL_ZONES=unavailable"
                   << L" add_status=0x" << std::hex
                   << static_cast<unsigned long>(thermalStatus) << std::dec
                   << L"\n";
    }

    if (*gpuUsage < 0.0 || *gpuUsage > 100.0 || usedGb < 0.0 ||
        usedGb > totalGb * 1.25 ||
        (gpuTemperature && (*gpuTemperature < 0.0 || *gpuTemperature > 200.0))) {
        std::wcerr << L"Metric validation failed\n";
        return 5;
    }
    return 0;
}
