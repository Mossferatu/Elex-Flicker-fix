#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "patch.h"

#define REQUIRE(test) do { if (!(test)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #test); return 1; } } while (0)
#define CHECK(call) do { HRESULT result = (call); if (FAILED(result)) { \
    fprintf(stderr, "FAIL line %d: %s = 0x%08lx\n", __LINE__, #call, (unsigned long)result); \
    return 1; } } while (0)

typedef HRESULT (WINAPI *CreateFn)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
typedef HRESULT (WINAPI *SwapFn)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL *, UINT, UINT, const DXGI_SWAP_CHAIN_DESC *, IDXGISwapChain **,
    ID3D11Device **, D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
typedef HRESULT (WINAPI *CompileFn)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *,
    ID3DInclude *, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);

static unsigned char *read_shader(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    unsigned char *bytes = malloc(11904);
    if (!bytes) { fclose(file); return NULL; }
    size_t size = fread(bytes, 1, 11904, file);
    int extra = fgetc(file);
    fclose(file);
    if (size != 11904 || extra != EOF) { free(bytes); return NULL; }
    return bytes;
}

int main(int argc, char **argv)
{
    REQUIRE(argc == 1 || argc == 3);
    unsigned char *source = NULL, *expected = NULL;
    unsigned char *patched = NULL;
    if (argc == 3) {
        source = read_shader(argv[1]);
        expected = read_shader(argv[2]);
        REQUIRE(source && expected);
        REQUIRE(patch_cloud_shader(source, 11904, &patched) == S_OK);
        REQUIRE(patched && memcmp(patched, expected, 11904) == 0);
        unsigned differences = 0;
        for (unsigned i = 20; i < 11904; ++i)
            if (source[i] != patched[i]) {
                REQUIRE(i == 11577 && (source[i] ^ patched[i]) == 8);
                ++differences;
            }
        REQUIRE(differences == 1);
        HeapFree(GetProcessHeap(), 0, patched);
        source[1024] ^= 1;
        REQUIRE(patch_cloud_shader(source, 11904, &patched) == E_INVALIDARG && !patched);
        source[1024] ^= 1;
        REQUIRE(patch_cloud_shader(source, 11903, &patched) == E_INVALIDARG && !patched);
        REQUIRE(patch_cloud_shader(expected, 11904, &patched) == S_FALSE && !patched);
        puts("PASS: exact transform, one changed instruction bit, corrupt/truncated input, unrelated pass-through");
    } else {
        puts("SKIP: target shader tests require two private, locally extracted shader paths");
    }
    REQUIRE(patch_cloud_shader(NULL, 0, &patched) == S_FALSE && !patched);

    HMODULE proxy = LoadLibraryW(L".\\d3d11.dll");
    REQUIRE(proxy);
    CreateFn create = (CreateFn)GetProcAddress(proxy, "D3D11CreateDevice");
    SwapFn create_swap = (SwapFn)GetProcAddress(proxy, "D3D11CreateDeviceAndSwapChain");
    REQUIRE(create && create_swap);
    WCHAR compiler_path[MAX_PATH];
    UINT length = GetSystemDirectoryW(compiler_path, MAX_PATH);
    REQUIRE(length && length + 20 < MAX_PATH);
    wcscat(compiler_path, L"\\d3dcompiler_47.dll");
    HMODULE compiler = LoadLibraryW(compiler_path);
    REQUIRE(compiler);
    CompileFn compile = (CompileFn)GetProcAddress(compiler, "D3DCompile");
    REQUIRE(compile);
    const char own_shader[] =
        "RWStructuredBuffer<uint> output : register(u0);"
        "[numthreads(1,1,1)] void main(){output[0]=42;}";
    ID3DBlob *code = NULL, *errors = NULL;
    HRESULT compiled = compile(own_shader, strlen(own_shader), "smoke", NULL, NULL,
        "main", "cs_5_0", 0, 0, &code, &errors);
    if (errors) {
        fputs(ID3D10Blob_GetBufferPointer(errors), stderr);
        ID3D10Blob_Release(errors);
    }
    CHECK(compiled);
    REQUIRE(patch_cloud_shader(ID3D10Blob_GetBufferPointer(code),
        ID3D10Blob_GetBufferSize(code), &patched) == S_FALSE && !patched);

    for (int driver = 0; driver < 2; ++driver) {
        ID3D11Device *device = NULL;
        ID3D11DeviceContext *context = NULL;
        CHECK(create(NULL, driver ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE,
            NULL, 0, NULL, 0, D3D11_SDK_VERSION, &device, NULL, &context));
        for (int repeat = 0; source && repeat < 2; ++repeat) {
            ID3D11ComputeShader *cloud = NULL;
            CHECK(ID3D11Device_CreateComputeShader(device, source, 11904, NULL, &cloud));
            REQUIRE(cloud);
            ID3D11ComputeShader_Release(cloud);
        }
        ID3D11ComputeShader *unrelated = NULL;
        CHECK(ID3D11Device_CreateComputeShader(device, ID3D10Blob_GetBufferPointer(code),
            ID3D10Blob_GetBufferSize(code), NULL, &unrelated));
        D3D11_BUFFER_DESC desc = {0};
        desc.ByteWidth = 4;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = 4;
        ID3D11Buffer *buffer = NULL, *staging = NULL;
        CHECK(ID3D11Device_CreateBuffer(device, &desc, NULL, &buffer));
        ID3D11UnorderedAccessView *view = NULL;
        CHECK(ID3D11Device_CreateUnorderedAccessView(device, (ID3D11Resource *)buffer, NULL, &view));
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = desc.MiscFlags = desc.StructureByteStride = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        CHECK(ID3D11Device_CreateBuffer(device, &desc, NULL, &staging));
        ID3D11DeviceContext_CSSetShader(context, unrelated, NULL, 0);
        ID3D11DeviceContext_CSSetUnorderedAccessViews(context, 0, 1, &view, NULL);
        ID3D11DeviceContext_Dispatch(context, 1, 1, 1);
        ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging, (ID3D11Resource *)buffer);
        D3D11_MAPPED_SUBRESOURCE mapped;
        CHECK(ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &mapped));
        REQUIRE(*(UINT *)mapped.pData == 42);
        ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);
        ID3D11DeviceContext_ClearState(context);
        ID3D11UnorderedAccessView_Release(view);
        ID3D11Buffer_Release(buffer);
        ID3D11Buffer_Release(staging);
        ID3D11ComputeShader_Release(unrelated);
        ID3D11DeviceContext_Release(context);
        ID3D11Device_Release(device);
    }
    ID3D10Blob_Release(code);
    puts("PASS: hardware and WARP devices; unrelated shader dispatch produces 42");
    if (source) puts("PASS: repeated corrected cloud shader creation on hardware and WARP");

    HWND window = CreateWindowExW(0, L"STATIC", L"ELEX cloud fix smoke test", WS_OVERLAPPEDWINDOW,
        0, 0, 320, 240, NULL, NULL, GetModuleHandleW(NULL), NULL);
    REQUIRE(window);
    DXGI_SWAP_CHAIN_DESC desc = {0};
    desc.BufferDesc.Width = 320;
    desc.BufferDesc.Height = 240;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 1;
    desc.OutputWindow = window;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain *swap = NULL;
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    CHECK(create_swap(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION,
        &desc, &swap, &device, NULL, &context));
    if (source) {
        ID3D11ComputeShader *cloud = NULL;
        CHECK(ID3D11Device_CreateComputeShader(device, source, 11904, NULL, &cloud));
        ID3D11ComputeShader_Release(cloud);
        puts("PASS: cloud shader interception on device-and-swap-chain path");
    }
    CHECK(IDXGISwapChain_Present(swap, 0, DXGI_PRESENT_TEST));
    IDXGISwapChain_Release(swap);
    ID3D11DeviceContext_Release(context);
    ID3D11Device_Release(device);
    DestroyWindow(window);
    free(source);
    free(expected);
    puts("PASS: device-and-swap-chain forwarding");
    return 0;
}
