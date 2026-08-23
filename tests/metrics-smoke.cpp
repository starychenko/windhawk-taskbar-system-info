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

using D3DKMTOpenAdapterFromLuid_t =
    LONG(WINAPI*)(D3DKMT_OPENADAPTERFROMLUID*);
using D3DKMTQueryAdapterInfo_t =
    LONG(WINAPI*)(D3DKMT_QUERYADAPTERINFO*);
using D3DKMTCloseAdapter_t =
    LONG(WINAPI*)(const D3DKMT_CLOSEADAPTER*);

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

bool ReadArray(PDH_HCOUNTER counter,
               std::vector<unsigned char>& buffer,
               DWORD& count) {
    DWORD size = 0;
    PDH_STATUS status = PdhGetFormattedCounterArrayW(
        counter, PDH_FMT_DOUBLE, &size, &count, nullptr);
    if (status != static_cast<PDH_STATUS>(PDH_MORE_DATA) || !size) {
        return false;
    }
    buffer.resize(size);
    return PdhGetFormattedCounterArrayW(
               counter, PDH_FMT_DOUBLE, &size, &count,
               reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data())) ==
           ERROR_SUCCESS;
}

}  // namespace

int wmain() {
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
    if (!found || !selected.DedicatedVideoMemory) {
        std::wcerr << L"No dedicated GPU found\n";
        return 2;
    }

    wchar_t luidBuffer[32];
    swprintf(luidBuffer, std::size(luidBuffer), L"0x%08X_0x%08X",
             selected.AdapterLuid.HighPart, selected.AdapterLuid.LowPart);
    std::wstring luid = ToLower(luidBuffer);

    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER gpuCounter = nullptr;
    PDH_HCOUNTER vramCounter = nullptr;
    PDH_HCOUNTER thermalCounter = nullptr;
    if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS ||
        PdhAddEnglishCounterW(query,
                              L"\\GPU Engine(*)\\Utilization Percentage", 0,
                              &gpuCounter) != ERROR_SUCCESS ||
        PdhAddEnglishCounterW(query,
                              L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0,
                              &vramCounter) != ERROR_SUCCESS) {
        std::wcerr << L"PDH setup failed\n";
        if (query) {
            PdhCloseQuery(query);
        }
        return 3;
    }

    PDH_STATUS thermalStatus = PdhAddEnglishCounterW(
        query, L"\\Thermal Zone Information(*)\\Temperature", 0,
        &thermalCounter);
    if (thermalStatus != ERROR_SUCCESS) {
        thermalCounter = nullptr;
    }

    PdhCollectQueryData(query);
    Sleep(1100);
    if (PdhCollectQueryData(query) != ERROR_SUCCESS) {
        PdhCloseQuery(query);
        return 4;
    }

    std::vector<unsigned char> buffer;
    DWORD count = 0;
    std::unordered_map<std::wstring, double> engines;
    if (ReadArray(gpuCounter, buffer, count)) {
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(
            buffer.data());
        for (DWORD i = 0; i < count; i++) {
            std::wstring instance =
                items[i].szName ? ToLower(items[i].szName) : L"";
            double value = items[i].FmtValue.doubleValue;
            if (instance.find(luid) == std::wstring::npos ||
                !std::isfinite(value) || value < 0.0) {
                continue;
            }
            size_t luidPosition = instance.find(L"luid_");
            std::wstring key = luidPosition == std::wstring::npos
                                   ? instance
                                   : instance.substr(luidPosition);
            engines[key] += value;
        }
    }

    double gpuUsage = 0.0;
    for (const auto& [engine, value] : engines) {
        gpuUsage = std::max(gpuUsage, value);
    }
    gpuUsage = std::clamp(gpuUsage, 0.0, 100.0);

    buffer.clear();
    count = 0;
    double vramBytes = 0.0;
    bool vramFound = false;
    if (ReadArray(vramCounter, buffer, count)) {
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(
            buffer.data());
        for (DWORD i = 0; i < count; i++) {
            std::wstring instance =
                items[i].szName ? ToLower(items[i].szName) : L"";
            double value = items[i].FmtValue.doubleValue;
            if (instance.find(luid) == std::wstring::npos ||
                !std::isfinite(value) || value < 0.0) {
                continue;
            }
            vramBytes += value;
            vramFound = true;
        }
    }

    buffer.clear();
    count = 0;
    double thermalSumCelsius = 0.0;
    double thermalHottestCelsius = 0.0;
    size_t thermalCount = 0;
    if (ReadArray(thermalCounter, buffer, count)) {
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

    double totalGb = static_cast<double>(selected.DedicatedVideoMemory) / kGiB;
    double usedGb = vramBytes / kGiB;
    double percent = totalGb > 0.0 ? usedGb / totalGb * 100.0 : 0.0;
    auto gpuTemperature = ReadGpuTemperature(selected.AdapterLuid);

    std::wcout << L"GPU=" << selected.Description << L"\n"
               << L"LUID=" << luid << L"\n"
               << L"GPU_USAGE=" << gpuUsage << L"%\n"
               << L"VRAM=" << usedGb << L"/" << totalGb << L" GiB ("
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

    if (!vramFound || gpuUsage < 0.0 || gpuUsage > 100.0 || usedGb < 0.0 ||
        usedGb > totalGb * 1.25 ||
        (gpuTemperature && (*gpuTemperature < 0.0 || *gpuTemperature > 200.0))) {
        std::wcerr << L"Metric validation failed\n";
        return 5;
    }
    return 0;
}
