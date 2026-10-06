#include "patch.h"
#include <bcrypt.h>
#include <string.h>

static const unsigned char original_checksum[16] = {
    0x0e,0xe3,0x78,0xa9,0x37,0xd4,0xb0,0xaf,0xf9,0x6a,0xa1,0x31,0x7f,0x12,0x43,0x3a
};
static const unsigned char corrected_checksum[16] = {
    0x99,0xc0,0x9a,0xe6,0x6c,0x92,0xa6,0xff,0x58,0xb0,0x86,0xd9,0x02,0xbc,0xeb,0x5d
};
static const unsigned char original_sha256[32] = {
    0x62,0xc6,0x1e,0x70,0x60,0x1a,0xf0,0xee,0x30,0xa8,0x11,0xcf,0xfb,0xb6,0x6f,0x55,
    0xcd,0x8e,0xda,0x6e,0x0c,0x88,0xb6,0x9c,0xbf,0xbd,0x64,0x63,0x70,0x23,0x71,0x62
};
static const unsigned char corrected_sha256[32] = {
    0x3b,0xbd,0x8c,0x85,0x8d,0xc3,0x1d,0x5d,0xfd,0x45,0x54,0x86,0x07,0x10,0x11,0x30,
    0x8e,0x35,0xa0,0x20,0x16,0xb5,0xc4,0xeb,0x40,0x5a,0xfa,0x0b,0xec,0x2c,0xee,0xd6
};

static HRESULT sha256(const unsigned char *data, ULONG size, unsigned char digest[32])
{
    BCRYPT_ALG_HANDLE algorithm = NULL;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (status < 0) return HRESULT_FROM_NT(status);
    status = BCryptHash(algorithm, NULL, 0, (PUCHAR)data, size, digest, 32);
    NTSTATUS close_status = BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) return HRESULT_FROM_NT(status);
    return close_status < 0 ? HRESULT_FROM_NT(close_status) : S_OK;
}

HRESULT patch_cloud_shader(const void *data, SIZE_T size, unsigned char **output)
{
    *output = NULL;
    const unsigned char *source = data;
    if (!source || size < 20 || memcmp(source, "DXBC", 4) ||
        memcmp(source + 4, original_checksum, 16))
        return S_FALSE;
    if (size != 11904) return E_INVALIDARG;
    unsigned char digest[32];
    HRESULT hr = sha256(source, (ULONG)size, digest);
    if (FAILED(hr)) return hr;
    if (memcmp(digest, original_sha256, 32)) return E_INVALIDARG;
    DWORD before, middle, after;
    memcpy(&before, source + 11316, 4);
    memcpy(&middle, source + 11576, 4);
    memcpy(&after, source + 11612, 4);
    if (before != 0x010018be || middle != 0x010010be || after != 0x010018be)
        return E_INVALIDARG;
    unsigned char *patched = HeapAlloc(GetProcessHeap(), 0, size);
    if (!patched) return E_OUTOFMEMORY;
    memcpy(patched, source, size);
    /* Exact input fingerprint makes the precomputed container checksum valid. */
    middle |= 0x800;
    memcpy(patched + 11576, &middle, 4);
    memcpy(patched + 4, corrected_checksum, 16);
    hr = sha256(patched, (ULONG)size, digest);
    if (SUCCEEDED(hr) && memcmp(digest, corrected_sha256, 32)) hr = E_UNEXPECTED;
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, patched);
        return hr;
    }
    *output = patched;
    return S_OK;
}
