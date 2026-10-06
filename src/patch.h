#ifndef ELEX_CLOUD_PATCH_H
#define ELEX_CLOUD_PATCH_H
#include <windows.h>

/* S_FALSE: unrelated shader; S_OK: caller owns *output via HeapFree. */
HRESULT patch_cloud_shader(const void *data, SIZE_T size, unsigned char **output);
#endif
