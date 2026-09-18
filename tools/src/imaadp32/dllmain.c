/* What imaadp32.c gets from Wine's build and C runtime when it is built with
 * llvm-mingw and no C runtime: the DLL entry point and the memory helpers the
 * compiler emits for structure copies. */
#include <windows.h>

BOOL WINAPI DllMainCRTStartup( HINSTANCE instance, DWORD reason, void *reserved )
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls( instance );
    return TRUE;
}

void *memset( void *dst, int c, size_t n )
{
    unsigned char *p = dst;
    while (n--) *p++ = (unsigned char)c;
    return dst;
}

void *memcpy( void *dst, const void *src, size_t n )
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}
