// Only this test executable sees the injected APIs. The production module is
// included unchanged below; no test switches or synthetic fixtures ship in it.
#pragma once
#include <windows.h>
#include <dxgi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <commctrl.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <windhawk_api.h>

namespace fake {
struct Query { bool closed = false; bool fail = false; int collects = 0; };
struct Counter { Query* query; std::wstring path; };
inline std::vector<std::unique_ptr<Query>> queries;
inline std::vector<std::unique_ptr<Counter>> counters;
inline Query* stale = nullptr;
inline bool freshMemory = true;
inline bool memoryAvailable = true;
inline bool enginesAvailable = true;
inline bool hardArrays = false;
inline bool invalidEngine = false;
inline Query* invalidEngineQuery = nullptr;
inline int growArrayAttempts = 0;
inline int opens = 0;
inline std::wstring instance = L"luid_0x00000000_0x00000042_phys_0";
inline std::vector<BYTE> mapping;
inline size_t mappingOffset = 0;
inline bool mutexTimeout = false;
inline bool mutexAvailable = true;
inline const HANDLE mappingHandle = reinterpret_cast<HANDLE>(10001);
inline const HANDLE mutexHandle = reinterpret_cast<HANDLE>(10002);
inline const HKEY registryKey = reinterpret_cast<HKEY>(10003);
inline std::map<std::wstring, std::wstring> registry;
inline HKEY nativeRegistrySource = nullptr;
inline bool registryInfoFails = false;
inline bool registryChangesDuringRead = false;
inline size_t registryValueReads = 0;
inline uint64_t registryWriteTime = 0;
inline void TouchRegistry() {
    FILETIME now{}; GetSystemTimeAsFileTime(&now);
    uint64_t ticks = (uint64_t{now.dwHighDateTime} << 32) | now.dwLowDateTime;
    registryWriteTime = std::max(ticks, registryWriteTime + 1);
}

inline PDH_STATUS OpenQuery(PCWSTR, DWORD_PTR, PDH_HQUERY* result) {
    queries.push_back(std::make_unique<Query>());
    *result = reinterpret_cast<PDH_HQUERY>(queries.back().get());
    ++opens;
    return ERROR_SUCCESS;
}
inline PDH_STATUS CloseQuery(PDH_HQUERY handle) {
    reinterpret_cast<Query*>(handle)->closed = true;
    return ERROR_SUCCESS;
}
inline PDH_STATUS AddCounter(PDH_HQUERY query, PCWSTR path, DWORD_PTR,
                             PDH_HCOUNTER* result) {
    counters.push_back(std::make_unique<Counter>(
        Counter{reinterpret_cast<Query*>(query), path}));
    *result = reinterpret_cast<PDH_HCOUNTER>(counters.back().get());
    return ERROR_SUCCESS;
}
inline PDH_STATUS RemoveCounter(PDH_HCOUNTER) { return ERROR_SUCCESS; }
inline PDH_STATUS Collect(PDH_HQUERY handle) {
    auto* query = reinterpret_cast<Query*>(handle);
    ++query->collects;
    return query->fail || query->closed ? PDH_INVALID_HANDLE : ERROR_SUCCESS;
}
inline PDH_STATUS Value(PDH_HCOUNTER, DWORD, LPDWORD, PPDH_FMT_COUNTERVALUE value) {
    value->CStatus = PDH_CSTATUS_VALID_DATA;
    value->doubleValue = 25.0;
    return ERROR_SUCCESS;
}
inline PDH_STATUS Array(PDH_HCOUNTER handle, DWORD, LPDWORD bytes,
                        LPDWORD count, PPDH_FMT_COUNTERVALUE_ITEM_W values) {
    const auto& counter = *reinterpret_cast<Counter*>(handle);
    if (hardArrays) return PDH_INVALID_HANDLE;
    bool thermal = counter.path.find(L"Thermal Zone") != std::wstring::npos;
    bool engine = counter.path.find(L"GPU Engine") != std::wstring::npos;
    bool present = thermal || (counter.query != stale &&
        (engine ? enginesAvailable : memoryAvailable &&
                                      (!stale || freshMemory)));
    if (!present) { *bytes = 0; *count = 0; return ERROR_SUCCESS; }
    DWORD required = sizeof(PDH_FMT_COUNTERVALUE_ITEM_W);
    if (growArrayAttempts > 0) {
        --growArrayAttempts;
        *bytes += required;
        *count = 1;
        return PDH_MORE_DATA;
    }
    if (!values || *bytes < required) {
        *bytes = required; *count = 1; return PDH_MORE_DATA;
    }
    values[0].szName = const_cast<PWSTR>(instance.c_str());
    values[0].FmtValue.CStatus = PDH_CSTATUS_VALID_DATA;
    if (engine && (invalidEngine || counter.query == invalidEngineQuery))
        values[0].FmtValue.CStatus = PDH_CSTATUS_INVALID_DATA;
    values[0].FmtValue.doubleValue = thermal ? 323.15 : engine ? 14.0 : 268435456.0;
    *bytes = required; *count = 1;
    return ERROR_SUCCESS;
}
inline HANDLE OpenMapping(DWORD, BOOL, LPCWSTR) {
    return mapping.empty() ? nullptr : mappingHandle;
}
inline HANDLE OpenMutex(DWORD, BOOL, LPCWSTR) { return mutexAvailable ? mutexHandle : nullptr; }
inline DWORD Wait(HANDLE handle, DWORD timeout) {
    return handle == mutexHandle ? (mutexTimeout ? WAIT_TIMEOUT : WAIT_OBJECT_0)
                                 : WaitForSingleObject(handle, timeout);
}
inline LPVOID Map(HANDLE, DWORD, DWORD, DWORD, SIZE_T) {
    return mapping.data() + mappingOffset;
}
inline SIZE_T VirtualMemory(LPCVOID address, PMEMORY_BASIC_INFORMATION info, SIZE_T size) {
    if (address == mapping.data() + mappingOffset) {
        *info = {};
        info->BaseAddress = mapping.data();
        info->RegionSize = mapping.size();
        return sizeof(*info);
    }
    return VirtualQuery(address, info, size);
}
inline BOOL Unmap(LPCVOID) { return TRUE; }
inline BOOL Release(HANDLE) { return TRUE; }
inline BOOL CloseHandleChecked(HANDLE handle) {
    return handle == mappingHandle || handle == mutexHandle ? TRUE : CloseHandle(handle);
}
inline LSTATUS OpenRegistry(HKEY, LPCWSTR, DWORD, REGSAM access, PHKEY key) {
    if (nativeRegistrySource) return RegOpenKeyExW(nativeRegistrySource, L"", 0, access, key);
    *key = registryKey;
    return ERROR_SUCCESS;
}
inline LSTATUS CloseRegistry(HKEY key) { return key == registryKey ? ERROR_SUCCESS : RegCloseKey(key); }
inline LSTATUS RegistryInfo(HKEY key, LPWSTR cls, LPDWORD clsLength, LPDWORD reserved,
                           LPDWORD subkeys, LPDWORD maxSubkey, LPDWORD maxClass,
                           LPDWORD values, LPDWORD maxName, LPDWORD maxValue,
                           LPDWORD security, PFILETIME lastWrite) {
    if (key != registryKey) return RegQueryInfoKeyW(key, cls, clsLength, reserved,
        subkeys, maxSubkey, maxClass, values, maxName, maxValue, security, lastWrite);
    if (registryInfoFails) return ERROR_ACCESS_DENIED;
    *lastWrite = {static_cast<DWORD>(registryWriteTime), static_cast<DWORD>(registryWriteTime >> 32)};
    return ERROR_SUCCESS;
}
inline LSTATUS EnumRegistry(HKEY key, DWORD index, LPWSTR name, LPDWORD length,
                             LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD bytes) {
    if (key != registryKey) return RegEnumValueW(key, index, name, length, reserved, type, data, bytes);
    if (index >= registry.size()) return ERROR_NO_MORE_ITEMS;
    auto it = registry.begin();
    std::advance(it, index);
    DWORD required = static_cast<DWORD>(it->first.size());
    if (*length <= required) { *length = required; return ERROR_MORE_DATA; }
    std::copy(it->first.begin(), it->first.end(), name);
    name[required] = 0;
    *length = required;
    return ERROR_SUCCESS;
}
inline LSTATUS RegistryValue(HKEY key, LPCWSTR name, LPDWORD reserved, LPDWORD type,
                                LPBYTE buffer, LPDWORD bytes) {
    if (key != registryKey) return RegQueryValueExW(key, name, reserved, type, buffer, bytes);
    ++registryValueReads;
    if (registryChangesDuringRead) TouchRegistry();
    auto it = registry.find(name);
    if (it == registry.end()) return ERROR_FILE_NOT_FOUND;
    *type = REG_SZ;
    DWORD required = static_cast<DWORD>((it->second.size() + 1) * sizeof(wchar_t));
    if (buffer) {
        if (*bytes < required) { *bytes = required; return ERROR_MORE_DATA; }
        std::memcpy(buffer, it->second.c_str(), required);
    }
    *bytes = required;
    return ERROR_SUCCESS;
}
inline std::map<std::wstring, std::wstring> localStorage;
inline bool localSaveFails = false;
inline size_t GetLocalString(PCWSTR key, PWSTR buffer, size_t capacity) {
    auto it = localStorage.find(key);
    if (it == localStorage.end()) return 0;
    if (capacity <= it->second.size()) return it->second.size() + 1;
    std::copy(it->second.begin(), it->second.end(), buffer);
    buffer[it->second.size()] = 0;
    return it->second.size();
}
inline BOOL SetLocalString(PCWSTR key, PCWSTR value) {
    if (localSaveFails) return FALSE;
    localStorage[key] = value; return TRUE;
}
} // namespace fake

#define PdhOpenQueryW fake::OpenQuery
#define PdhCloseQuery fake::CloseQuery
#define PdhAddEnglishCounterW fake::AddCounter
#define PdhRemoveCounter fake::RemoveCounter
#define PdhCollectQueryData fake::Collect
#define PdhGetFormattedCounterValue fake::Value
#define PdhGetFormattedCounterArrayW fake::Array
#define OpenFileMappingW fake::OpenMapping
#define OpenMutexW fake::OpenMutex
#define WaitForSingleObject fake::Wait
#define MapViewOfFile fake::Map
#define VirtualQuery fake::VirtualMemory
#define UnmapViewOfFile fake::Unmap
#define ReleaseMutex fake::Release
#define CloseHandle fake::CloseHandleChecked
#define RegOpenKeyExW fake::OpenRegistry
#define RegCloseKey fake::CloseRegistry
#define RegQueryValueExW fake::RegistryValue
#define RegQueryInfoKeyW fake::RegistryInfo
#define RegEnumValueW fake::EnumRegistry

#define Wh_GetStringValue fake::GetLocalString
#define Wh_SetStringValue fake::SetLocalString
