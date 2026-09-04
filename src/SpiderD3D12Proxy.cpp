#include <windows.h>
#include <d3d12.h>

#include <chrono>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <string>
#include <cwchar>

static std::mutex g_logMutex;
static HMODULE g_originalD3D12 = nullptr;

using D3D12CreateDevice_t = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
using MessageBoxA_t = int(WINAPI*)(HWND, LPCSTR, LPCSTR, UINT);
using MessageBoxW_t = int(WINAPI*)(HWND, LPCWSTR, LPCWSTR, UINT);
using CheckFeatureSupport_t = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, D3D12_FEATURE, void*, UINT);
using CreateGraphicsPipelineState_t = HRESULT(STDMETHODCALLTYPE*)(
    ID3D12Device*,
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC*,
    REFIID,
    void**);

static D3D12CreateDevice_t g_originalD3D12CreateDevice = nullptr;
static MessageBoxA_t g_originalMessageBoxA = nullptr;
static MessageBoxW_t g_originalMessageBoxW = nullptr;
static CheckFeatureSupport_t g_originalCheckFeatureSupport = nullptr;
static CreateGraphicsPipelineState_t g_originalCreateGraphicsPipelineState = nullptr;
static LONG g_featureLogBudget = 80;
static LONG g_psoFailureLogBudget = 80;

static constexpr size_t kCreateGraphicsPipelineStateVtableIndex = 10;
static constexpr size_t kCheckFeatureSupportVtableIndex = 13;

static void Log(const char* msg) {
    std::lock_guard<std::mutex> lock(g_logMutex);

    std::ofstream log("SpiderFix_RX580.log", std::ios::app);
    if (!log) {
        return;
    }

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    struct tm tmBuf {};
    localtime_s(&tmBuf, &time);
    log << std::put_time(&tmBuf, "%H:%M:%S") << " - " << msg << std::endl;
}

static bool ContainsInsensitiveAscii(const char* text, const char* needle) {
    if (!text || !needle) {
        return false;
    }

    std::string haystack(text);
    std::string target(needle);
    for (char& ch : haystack) {
        ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
    }
    for (char& ch : target) {
        ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
    }
    return haystack.find(target) != std::string::npos;
}

static bool ContainsInsensitiveWideAscii(const wchar_t* text, const wchar_t* needle) {
    if (!text || !needle) {
        return false;
    }

    std::wstring haystack(text);
    std::wstring target(needle);
    for (wchar_t& ch : haystack) {
        ch = static_cast<wchar_t>(towlower(ch));
    }
    for (wchar_t& ch : target) {
        ch = static_cast<wchar_t>(towlower(ch));
    }
    return haystack.find(target) != std::wstring::npos;
}

static bool IsGpuCompatibilityWarningA(LPCSTR text) {
    return ContainsInsensitiveAscii(text, "gpu") &&
        (ContainsInsensitiveAscii(text, "compat") ||
         ContainsInsensitiveAscii(text, "minimum") ||
         ContainsInsensitiveAscii(text, "requisitos"));
}

static bool IsGpuCompatibilityWarningW(LPCWSTR text) {
    return ContainsInsensitiveWideAscii(text, L"gpu") &&
        (ContainsInsensitiveWideAscii(text, L"compat") ||
         ContainsInsensitiveWideAscii(text, L"minimum") ||
         ContainsInsensitiveWideAscii(text, L"requisitos"));
}

extern "C" int WINAPI HookedMessageBoxA(HWND hwnd, LPCSTR text, LPCSTR caption, UINT type) {
    if (IsGpuCompatibilityWarningA(text)) {
        Log("GPU compatibility warning detected - NOT suppressed");
    }

    return g_originalMessageBoxA ?
        g_originalMessageBoxA(hwnd, text, caption, type) :
        IDOK;
}

extern "C" int WINAPI HookedMessageBoxW(HWND hwnd, LPCWSTR text, LPCWSTR caption, UINT type) {
    if (IsGpuCompatibilityWarningW(text)) {
        Log("GPU compatibility warning detected - NOT suppressed");
    }

    return g_originalMessageBoxW ?
        g_originalMessageBoxW(hwnd, text, caption, type) :
        IDOK;
}

static bool PatchImport(const char* moduleName, const char* functionName, void* hook, void** original) {
    HMODULE exe = GetModuleHandleA(nullptr);
    if (!exe) {
        return false;
    }

    auto* base = reinterpret_cast<unsigned char*>(exe);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    const IMAGE_DATA_DIRECTORY& importDir =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!importDir.VirtualAddress) {
        return false;
    }

    auto* importDesc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + importDir.VirtualAddress);
    for (; importDesc->Name; ++importDesc) {
        const char* importedModule = reinterpret_cast<const char*>(base + importDesc->Name);
        if (_stricmp(importedModule, moduleName) != 0) {
            continue;
        }

        auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + importDesc->FirstThunk);
        auto* originalThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(
            base + (importDesc->OriginalFirstThunk ? importDesc->OriginalFirstThunk : importDesc->FirstThunk));

        for (; thunk->u1.Function; ++thunk, ++originalThunk) {
            if (IMAGE_SNAP_BY_ORDINAL(originalThunk->u1.Ordinal)) {
                continue;
            }

            auto* importByName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                base + originalThunk->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(importByName->Name), functionName) != 0) {
                continue;
            }

            DWORD oldProtect = 0;
            if (!VirtualProtect(&thunk->u1.Function, sizeof(thunk->u1.Function), PAGE_READWRITE, &oldProtect)) {
                return false;
            }

            if (original && !*original) {
                *original = reinterpret_cast<void*>(thunk->u1.Function);
            }
            thunk->u1.Function = reinterpret_cast<ULONG_PTR>(hook);

            DWORD ignored = 0;
            VirtualProtect(&thunk->u1.Function, sizeof(thunk->u1.Function), oldProtect, &ignored);
            FlushInstructionCache(GetCurrentProcess(), &thunk->u1.Function, sizeof(thunk->u1.Function));

            char buf[256] {};
            std::snprintf(buf, sizeof(buf), "IAT patch aplicado: %s!%s", moduleName, functionName);
            Log(buf);
            return true;
        }
    }

    return false;
}

static void PatchGpuWarningMessageBox() {
    HMODULE user32 = LoadLibraryA("user32.dll");
    if (user32) {
        if (!g_originalMessageBoxA) {
            g_originalMessageBoxA = reinterpret_cast<MessageBoxA_t>(
                GetProcAddress(user32, "MessageBoxA"));
        }
        if (!g_originalMessageBoxW) {
            g_originalMessageBoxW = reinterpret_cast<MessageBoxW_t>(
                GetProcAddress(user32, "MessageBoxW"));
        }
    }

    PatchImport("user32.dll", "MessageBoxA", reinterpret_cast<void*>(&HookedMessageBoxA),
        reinterpret_cast<void**>(&g_originalMessageBoxA));
    PatchImport("user32.dll", "MessageBoxW", reinterpret_cast<void*>(&HookedMessageBoxW),
        reinterpret_cast<void**>(&g_originalMessageBoxW));
}

static const char* FeatureLevelName(D3D_FEATURE_LEVEL level) {
    switch (level) {
    case D3D_FEATURE_LEVEL_12_1: return "12_1";
    case D3D_FEATURE_LEVEL_12_0: return "12_0";
    case D3D_FEATURE_LEVEL_11_1: return "11_1";
    case D3D_FEATURE_LEVEL_11_0: return "11_0";
    default: return "unknown";
    }
}

static const char* FeatureName(D3D12_FEATURE feature) {
    switch (feature) {
    case D3D12_FEATURE_FEATURE_LEVELS: return "FEATURE_LEVELS";
    case D3D12_FEATURE_SHADER_MODEL: return "SHADER_MODEL";
    case D3D12_FEATURE_D3D12_OPTIONS: return "OPTIONS";
    case D3D12_FEATURE_D3D12_OPTIONS1: return "OPTIONS1";
    case D3D12_FEATURE_D3D12_OPTIONS2: return "OPTIONS2";
    case D3D12_FEATURE_D3D12_OPTIONS3: return "OPTIONS3";
    case D3D12_FEATURE_D3D12_OPTIONS4: return "OPTIONS4";
    case D3D12_FEATURE_D3D12_OPTIONS5: return "OPTIONS5";
    case D3D12_FEATURE_D3D12_OPTIONS6: return "OPTIONS6";
    case D3D12_FEATURE_D3D12_OPTIONS7: return "OPTIONS7";
    case D3D12_FEATURE_D3D12_OPTIONS8: return "OPTIONS8";
    case D3D12_FEATURE_D3D12_OPTIONS9: return "OPTIONS9";
    case D3D12_FEATURE_D3D12_OPTIONS10: return "OPTIONS10";
    case D3D12_FEATURE_D3D12_OPTIONS11: return "OPTIONS11";
    case D3D12_FEATURE_D3D12_OPTIONS12: return "OPTIONS12";
    case D3D12_FEATURE_D3D12_OPTIONS13: return "OPTIONS13";
    default: return "UNKNOWN";
    }
}

static bool LoadOriginalD3D12() {
    if (g_originalD3D12CreateDevice) {
        return true;
    }

    char systemPath[MAX_PATH] {};
    if (!GetSystemDirectoryA(systemPath, MAX_PATH)) {
        Log("GetSystemDirectoryA falhou");
        return false;
    }

    std::string dllPath = std::string(systemPath) + "\\d3d12.dll";
    g_originalD3D12 = LoadLibraryA(dllPath.c_str());
    if (!g_originalD3D12) {
        Log("LoadLibraryA falhou para d3d12.dll original");
        return false;
    }

    g_originalD3D12CreateDevice = reinterpret_cast<D3D12CreateDevice_t>(
        GetProcAddress(g_originalD3D12, "D3D12CreateDevice")
    );

    if (!g_originalD3D12CreateDevice) {
        Log("GetProcAddress falhou para D3D12CreateDevice original");
        return false;
    }

    return true;
}

extern "C" FARPROC WINAPI GetOriginalProcByName(const char* name) {
    if (!LoadOriginalD3D12()) {
        return nullptr;
    }
    return GetProcAddress(g_originalD3D12, name);
}

extern "C" FARPROC WINAPI GetOriginalProcByOrdinal(WORD ordinal) {
    if (!LoadOriginalD3D12()) {
        return nullptr;
    }
    return GetProcAddress(g_originalD3D12, reinterpret_cast<LPCSTR>(ordinal));
}

static HRESULT STDMETHODCALLTYPE HookedCheckFeatureSupport(
    ID3D12Device* self,
    D3D12_FEATURE feature,
    void* pFeatureSupportData,
    UINT featureSupportDataSize
) {
    CheckFeatureSupport_t original = g_originalCheckFeatureSupport;
    if (!original) {
        return E_FAIL;
    }

    HRESULT hr = original(self, feature, pFeatureSupportData, featureSupportDataSize);

    if (InterlockedDecrement(&g_featureLogBudget) >= 0 || FAILED(hr)) {
        char buf[256] {};
        std::snprintf(
            buf,
            sizeof(buf),
            "CheckFeatureSupport %s (%u): HRESULT 0x%08lX",
            FeatureName(feature),
            static_cast<unsigned>(feature),
            static_cast<unsigned long>(hr)
        );
        Log(buf);
    }

    if (!pFeatureSupportData) {
        return hr;
    }

    if (feature == D3D12_FEATURE_FEATURE_LEVELS &&
        featureSupportDataSize >= sizeof(D3D12_FEATURE_DATA_FEATURE_LEVELS)) {
        auto* levels = reinterpret_cast<D3D12_FEATURE_DATA_FEATURE_LEVELS*>(pFeatureSupportData);
        levels->MaxSupportedFeatureLevel = D3D_FEATURE_LEVEL_12_0;
        Log("Spoof: FEATURE_LEVELS HRESULT -> S_OK, MaxSupportedFeatureLevel -> 12_0");
        return S_OK;
    }

    if (feature == D3D12_FEATURE_SHADER_MODEL &&
        featureSupportDataSize >= sizeof(D3D12_FEATURE_DATA_SHADER_MODEL)) {
        auto* shaderModel = reinterpret_cast<D3D12_FEATURE_DATA_SHADER_MODEL*>(pFeatureSupportData);
        (void)shaderModel;
        Log("Spoof skipped: SHADER_MODEL left native");
        return hr;
    }

    if (feature == D3D12_FEATURE_D3D12_OPTIONS7 &&
        featureSupportDataSize >= sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS7)) {
        auto* opts7 = reinterpret_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS7*>(pFeatureSupportData);
        (void)opts7;
        Log("Spoof skipped: OPTIONS7 left native");
        return hr;
    }

    if (feature == D3D12_FEATURE_D3D12_OPTIONS12 &&
        featureSupportDataSize >= sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS12)) {
        auto* opts12 = reinterpret_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS12*>(pFeatureSupportData);
        (void)opts12;
        Log("Spoof skipped: OPTIONS12 left native");
        return hr;
    }

    return hr;
}

static const char* TopologyTypeName(D3D12_PRIMITIVE_TOPOLOGY_TYPE type) {
    switch (type) {
    case D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED: return "UNDEFINED";
    case D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT: return "POINT";
    case D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE: return "LINE";
    case D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE: return "TRIANGLE";
    case D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH: return "PATCH";
    default: return "unknown";
    }
}

static HRESULT STDMETHODCALLTYPE HookedCreateGraphicsPipelineState(
    ID3D12Device* self,
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc,
    REFIID riid,
    void** ppPipelineState
) {
    CreateGraphicsPipelineState_t original = g_originalCreateGraphicsPipelineState;
    if (!original) {
        return E_FAIL;
    }

    HRESULT hr = original(self, desc, riid, ppPipelineState);
    if (FAILED(hr) &&
        desc &&
        desc->NumRenderTargets == 0 &&
        desc->DSVFormat != DXGI_FORMAT_UNKNOWN &&
        desc->PS.pShaderBytecode &&
        desc->PS.BytecodeLength > 0) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC patchedDesc = *desc;
        patchedDesc.PS = {};

        HRESULT retryHr = original(self, &patchedDesc, riid, ppPipelineState);
        char retryBuf[256] {};
        std::snprintf(
            retryBuf,
            sizeof(retryBuf),
            "Depth-only PSO retry without PS: original 0x%08lX retry 0x%08lX",
            static_cast<unsigned long>(hr),
            static_cast<unsigned long>(retryHr));
        Log(retryBuf);

        if (SUCCEEDED(retryHr)) {
            return retryHr;
        }
    }

    if (FAILED(hr) && InterlockedDecrement(&g_psoFailureLogBudget) >= 0) {
        char buf[512] {};
        if (desc) {
            std::snprintf(
                buf,
                sizeof(buf),
                "CreateGraphicsPipelineState failed 0x%08lX topo=%s RTVs=%u DSV=0x%X Sample=%u/%u IBStrip=0x%X VS=%zu PS=%zu DS=%zu HS=%zu GS=%zu BlendAlpha=%u Depth=%u Stencil=%u Conservative=%u",
                static_cast<unsigned long>(hr),
                TopologyTypeName(desc->PrimitiveTopologyType),
                static_cast<unsigned>(desc->NumRenderTargets),
                static_cast<unsigned>(desc->DSVFormat),
                static_cast<unsigned>(desc->SampleDesc.Count),
                static_cast<unsigned>(desc->SampleDesc.Quality),
                static_cast<unsigned>(desc->IBStripCutValue),
                desc->VS.BytecodeLength,
                desc->PS.BytecodeLength,
                desc->DS.BytecodeLength,
                desc->HS.BytecodeLength,
                desc->GS.BytecodeLength,
                static_cast<unsigned>(desc->BlendState.AlphaToCoverageEnable),
                static_cast<unsigned>(desc->DepthStencilState.DepthEnable),
                static_cast<unsigned>(desc->DepthStencilState.StencilEnable),
                static_cast<unsigned>(desc->RasterizerState.ConservativeRaster));
        } else {
            std::snprintf(
                buf,
                sizeof(buf),
                "CreateGraphicsPipelineState failed 0x%08lX with null desc",
                static_cast<unsigned long>(hr));
        }
        Log(buf);
    }

    return hr;
}

static void PatchDeviceMethods(IUnknown* deviceUnknown) {
    if (!deviceUnknown) {
        return;
    }

    ID3D12Device* device = nullptr;
    HRESULT hr = deviceUnknown->QueryInterface(IID_PPV_ARGS(&device));
    if (FAILED(hr) || !device) {
        Log("Nao foi possivel obter ID3D12Device para patchar CheckFeatureSupport");
        return;
    }

    void*** object = reinterpret_cast<void***>(device);
    void** vtable = *object;
    void* currentCheck = vtable[kCheckFeatureSupportVtableIndex];
    void* currentPso = vtable[kCreateGraphicsPipelineStateVtableIndex];

    if (currentCheck == reinterpret_cast<void*>(&HookedCheckFeatureSupport) &&
        currentPso == reinterpret_cast<void*>(&HookedCreateGraphicsPipelineState)) {
        device->Release();
        return;
    }

    if (!g_originalCheckFeatureSupport) {
        g_originalCheckFeatureSupport = reinterpret_cast<CheckFeatureSupport_t>(currentCheck);
    }

    if (!g_originalCreateGraphicsPipelineState) {
        g_originalCreateGraphicsPipelineState =
            reinterpret_cast<CreateGraphicsPipelineState_t>(currentPso);
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(
            &vtable[kCreateGraphicsPipelineStateVtableIndex],
            sizeof(void*) * (kCheckFeatureSupportVtableIndex - kCreateGraphicsPipelineStateVtableIndex + 1),
            PAGE_EXECUTE_READWRITE,
            &oldProtect)) {
        Log("VirtualProtect falhou ao patchar metodos do ID3D12Device");
        device->Release();
        return;
    }

    vtable[kCreateGraphicsPipelineStateVtableIndex] =
        reinterpret_cast<void*>(&HookedCreateGraphicsPipelineState);
    vtable[kCheckFeatureSupportVtableIndex] = reinterpret_cast<void*>(&HookedCheckFeatureSupport);

    DWORD ignored = 0;
    VirtualProtect(
        &vtable[kCreateGraphicsPipelineStateVtableIndex],
        sizeof(void*) * (kCheckFeatureSupportVtableIndex - kCreateGraphicsPipelineStateVtableIndex + 1),
        oldProtect,
        &ignored);
    FlushInstructionCache(
        GetCurrentProcess(),
        &vtable[kCreateGraphicsPipelineStateVtableIndex],
        sizeof(void*) * (kCheckFeatureSupportVtableIndex - kCreateGraphicsPipelineStateVtableIndex + 1));

    Log("Patch aplicado: ID3D12Device::CreateGraphicsPipelineState + CheckFeatureSupport");
    device->Release();
}

extern "C" HRESULT WINAPI D3D12CreateDevice(
    IUnknown* pAdapter,
    D3D_FEATURE_LEVEL minimumFeatureLevel,
    REFIID riid,
    void** ppDevice
) {
    Log("=== Proxy D3D12CreateDevice interceptado ===");
    PatchGpuWarningMessageBox();

    if (!LoadOriginalD3D12()) {
        if (ppDevice) {
            *ppDevice = nullptr;
        }
        return E_FAIL;
    }

    if (!ppDevice) {
        return g_originalD3D12CreateDevice(pAdapter, minimumFeatureLevel, riid, ppDevice);
    }

    const D3D_FEATURE_LEVEL tryLevels[] = {
        D3D_FEATURE_LEVEL_12_0,
        minimumFeatureLevel,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    D3D_FEATURE_LEVEL lastTried = static_cast<D3D_FEATURE_LEVEL>(0);
    HRESULT lastHr = E_FAIL;

    for (D3D_FEATURE_LEVEL level : tryLevels) {
        if (level == lastTried) {
            continue;
        }
        lastTried = level;

        *ppDevice = nullptr;
        lastHr = g_originalD3D12CreateDevice(pAdapter, level, riid, ppDevice);

        char buf[256] {};
        std::snprintf(
            buf,
            sizeof(buf),
            "Tentativa D3D12CreateDevice FL %s: HRESULT 0x%08lX",
            FeatureLevelName(level),
            static_cast<unsigned long>(lastHr)
        );
        Log(buf);

        if (SUCCEEDED(lastHr)) {
            Log("D3D12CreateDevice liberado pelo proxy");
            PatchDeviceMethods(reinterpret_cast<IUnknown*>(*ppDevice));
            return lastHr;
        }
    }

    *ppDevice = nullptr;
    Log("FALHA TOTAL - nenhum feature level funcionou");
    return lastHr;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        Log("=== d3d12.dll proxy carregado ===");
    }
    return TRUE;
}
