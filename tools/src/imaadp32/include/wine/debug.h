/* Stand-in for Wine's wine/debug.h: imaadp32.c is built outside the Wine tree,
 * so its debug channel and TRACE/WARN/FIXME/ERR messages compile to nothing. */
#ifndef __WINE_DEBUG_H_STUB
#define __WINE_DEBUG_H_STUB
#define WINE_DEFAULT_DEBUG_CHANNEL(ch) struct __wine_debug_channel_##ch
#define TRACE(...) do { } while (0)
#define WARN(...)  do { } while (0)
#define FIXME(...) do { } while (0)
#define ERR(...)   do { } while (0)
/* The Wine tree defines this for its own sources (winnt.h, __WINESRC__). */
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif
#endif
