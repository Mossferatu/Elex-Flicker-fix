#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>
#include "patch.h"

typedef HRESULT (WINAPI *CreateDeviceFn)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE,
    UINT, const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **,
    D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
typedef HRESULT (WINAPI *CreateDeviceSwapFn)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE,
    UINT, const D3D_FEATURE_LEVEL *, UINT, UINT, const DXGI_SWAP_CHAIN_DESC *,
    IDXGISwapChain **, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
typedef HRESULT (STDMETHODCALLTYPE *CreateComputeFn)(ID3D11Device *, const void *,
    SIZE_T, ID3D11ClassLinkage *, ID3D11ComputeShader **);

typedef struct HookEntry {
    void **table;
    CreateComputeFn original;
    struct HookEntry *next;
} HookEntry;

static HMODULE self_module;
static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
static SRWLOCK hooks_lock = SRWLOCK_INIT, log_lock = SRWLOCK_INIT;
static HookEntry *hooks;
static CreateDeviceFn create_device;
static CreateDeviceSwapFn create_device_swap;
static WCHAR log_path[MAX_PATH];
static HRESULT init_result = E_FAIL;
static LONG error_shown, examined, applied;

static void log_message(const char *format, ...)
{
    char line[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    OutputDebugStringA(line);
    BOOL ok = FALSE;
    AcquireSRWLockExclusive(&log_lock);
    HANDLE file = CreateFileW(log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written, size = (DWORD)strlen(line);
        ok = WriteFile(file, line, size, &written, NULL) && written == size;
        if (ok) ok = WriteFile(file, "\r\n", 2, &written, NULL) && written == 2;
        CloseHandle(file);
    }
    ReleaseSRWLockExclusive(&log_lock);
    if (!ok && InterlockedCompareExchange(&error_shown, 1, 0) == 0)
        MessageBoxA(NULL, "Cannot write elex-cloud-fix.log beside the mod DLL.",
            "ELEX cloud fix error", MB_OK | MB_ICONERROR);
}

static void report_failure(const char *operation, HRESULT hr)
{
    log_message("ERROR: %s: 0x%08lx", operation, (unsigned long)hr);
    if (InterlockedCompareExchange(&error_shown, 1, 0) == 0) {
        char message[512];
        snprintf(message, sizeof(message),
            "ELEX cloud fix: %s failed (0x%08lx).\n"
            "The correction could not be applied. See elex-cloud-fix.log.",
            operation, (unsigned long)hr);
        MessageBoxA(NULL, message, "ELEX cloud fix error", MB_OK | MB_ICONERROR);
    }
}

static BOOL CALLBACK initialize(PINIT_ONCE init, PVOID parameter, PVOID *context)
{
    (void)init; (void)parameter; (void)context;
    WCHAR path[MAX_PATH];
    DWORD length = GetModuleFileNameW(self_module, path, MAX_PATH);
    if (!length || length >= MAX_PATH) {
        init_result = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        return TRUE;
    }
    WCHAR *name = wcsrchr(path, L'\\');
    if (!name || (SIZE_T)(name - path) + 21 >= MAX_PATH) return TRUE;
    name[1] = 0;
    wcscpy(log_path, path);
    wcscat(log_path, L"elex-cloud-fix.log");
    length = GetSystemDirectoryW(path, MAX_PATH);
    if (!length || length + 11 >= MAX_PATH) {
        init_result = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        return TRUE;
    }
    wcscat(path, L"\\d3d11.dll");
    HMODULE native = LoadLibraryW(path);
    if (!native) {
        init_result = HRESULT_FROM_WIN32(GetLastError());
        return TRUE;
    }
    create_device = (CreateDeviceFn)GetProcAddress(native, "D3D11CreateDevice");
    create_device_swap = (CreateDeviceSwapFn)GetProcAddress(native, "D3D11CreateDeviceAndSwapChain");
    if (!create_device || !create_device_swap) {
        init_result = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
        return TRUE;
    }
    init_result = S_OK;
    log_message("ELEX cloud barrier fix v0.1.0; PID=%lu; native system D3D11; "
        "waiting for exact shader match (not active until APPLIED)", GetCurrentProcessId());
    return TRUE;
}

static HRESULT ensure_initialized(void)
{
    if (!InitOnceExecuteOnce(&once, initialize, NULL, NULL))
        init_result = HRESULT_FROM_WIN32(GetLastError());
    if (FAILED(init_result)) report_failure("initialization", init_result);
    return init_result;
}

static HRESULT STDMETHODCALLTYPE hooked_compute(ID3D11Device *device, const void *data,
    SIZE_T size, ID3D11ClassLinkage *linkage, ID3D11ComputeShader **shader)
{
    CreateComputeFn original = NULL;
    AcquireSRWLockShared(&hooks_lock);
    for (HookEntry *entry = hooks; entry; entry = entry->next)
        if (entry->table == *(void ***)device) {
            original = entry->original;
            break;
        }
    ReleaseSRWLockShared(&hooks_lock);
    if (!original) {
        report_failure("device hook lookup", E_UNEXPECTED);
        if (shader) *shader = NULL;
        return E_UNEXPECTED;
    }
    if (InterlockedIncrement(&examined) == 1)
        log_message("Compute shader interception active; scanning fingerprints");
    unsigned char *patched = NULL;
    HRESULT hr = patch_cloud_shader(data, size, &patched);
    if (hr == S_FALSE) return original(device, data, size, linkage, shader);
    if (FAILED(hr)) {
        report_failure("cloud shader fingerprint/transform validation", hr);
        if (shader) *shader = NULL;
        return hr;
    }
    hr = original(device, patched, size, linkage, shader);
    HeapFree(GetProcessHeap(), 0, patched);
    if (FAILED(hr)) {
        report_failure("corrected compute shader creation", hr);
    } else if (shader && *shader) {
        log_message("APPLIED #%ld: cloud luminance shader; SHA256 verified; "
            "sync_g -> sync_g_t at byte 11576; native creation=0x%08lx",
            InterlockedIncrement(&applied), (unsigned long)hr);
    } else {
        log_message("Corrected cloud shader validation succeeded; no shader object requested");
    }
    return hr;
}

static HRESULT hook_device(ID3D11Device *device)
{
    void **table = *(void ***)device;
    HRESULT hr = S_OK;
    AcquireSRWLockExclusive(&hooks_lock);
    for (HookEntry *entry = hooks; entry; entry = entry->next)
        if (entry->table == table) goto done;
    HookEntry *entry = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*entry));
    if (!entry) {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    DWORD protection;
    if (!VirtualProtect(&table[18], sizeof(void *), PAGE_READWRITE, &protection)) {
        hr = HRESULT_FROM_WIN32(GetLastError());
        HeapFree(GetProcessHeap(), 0, entry);
        goto done;
    }
    entry->table = table;
    entry->original = (CreateComputeFn)table[18];
    entry->next = hooks;
    hooks = entry;
    InterlockedExchangePointer(&table[18], (void *)hooked_compute);
    DWORD ignored;
    if (!VirtualProtect(&table[18], sizeof(void *), protection, &ignored))
        hr = HRESULT_FROM_WIN32(GetLastError());
done:
    ReleaseSRWLockExclusive(&hooks_lock);
    if (FAILED(hr)) report_failure("CreateComputeShader interception", hr);
    return hr;
}

static HRESULT hook_outputs(ID3D11Device **device, ID3D11DeviceContext **context)
{
    if (device && *device) return hook_device(*device);
    if (context && *context) {
        ID3D11Device *temporary = NULL;
        ID3D11DeviceContext_GetDevice(*context, &temporary);
        HRESULT hr = hook_device(temporary);
        ID3D11Device_Release(temporary);
        return hr;
    }
    return S_OK;
}

static void release_outputs(ID3D11Device **device, ID3D11DeviceContext **context)
{
    if (context && *context) {
        ID3D11DeviceContext_Release(*context);
        *context = NULL;
    }
    if (device && *device) {
        ID3D11Device_Release(*device);
        *device = NULL;
    }
}

HRESULT WINAPI elex_create_device(IDXGIAdapter *adapter, D3D_DRIVER_TYPE type,
    HMODULE software, UINT flags, const D3D_FEATURE_LEVEL *levels, UINT level_count,
    UINT sdk, ID3D11Device **device, D3D_FEATURE_LEVEL *chosen, ID3D11DeviceContext **context)
{
    HRESULT hr = ensure_initialized();
    if (FAILED(hr)) return hr;
    hr = create_device(adapter, type, software, flags, levels, level_count, sdk,
        device, chosen, context);
    if (SUCCEEDED(hr)) {
        HRESULT hooked = hook_outputs(device, context);
        if (FAILED(hooked)) {
            release_outputs(device, context);
            return hooked;
        }
    }
    return hr;
}

HRESULT WINAPI elex_create_device_and_swap_chain(IDXGIAdapter *adapter, D3D_DRIVER_TYPE type,
    HMODULE software, UINT flags, const D3D_FEATURE_LEVEL *levels, UINT level_count,
    UINT sdk, const DXGI_SWAP_CHAIN_DESC *desc, IDXGISwapChain **swap, ID3D11Device **device,
    D3D_FEATURE_LEVEL *chosen, ID3D11DeviceContext **context)
{
    HRESULT hr = ensure_initialized();
    if (FAILED(hr)) return hr;
    hr = create_device_swap(adapter, type, software, flags, levels, level_count, sdk,
        desc, swap, device, chosen, context);
    if (SUCCEEDED(hr)) {
        HRESULT hooked = hook_outputs(device, context);
        if (FAILED(hooked)) {
            if (swap && *swap) {
                IDXGISwapChain_Release(*swap);
                *swap = NULL;
            }
            release_outputs(device, context);
            return hooked;
        }
    }
    return hr;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        self_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
