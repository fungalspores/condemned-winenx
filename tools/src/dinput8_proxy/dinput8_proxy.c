/* dinput8.dll for Condemned: Criminal Origins on Wine-NX.
 *
 * Wine's DirectInput 8 reads the mouse only through raw input (WM_INPUT), and
 * Wine-NX's Horizon server queues raw input for the keyboard but not for the
 * mouse (dlls/ntdll/unix/horizon.c, horizon_server_handle_send_hardware_message),
 * nor does it run low-level hooks. The game's mouse device therefore reports
 * nothing: no clicks in the menus, no looking around in the game.
 *
 * This proxy loads the system dinput8.dll and hands everything to it, except the
 * system mouse's GetDeviceState and GetDeviceData. For those it asks Wine's
 * device first (so acquisition, buffer and parameter errors stay Wine's), then
 * fills in what Wine-NX does provide: cursor movement since the last read
 * (the right stick moves the cursor) and the button state (B clicks, and the
 * server keeps VK_LBUTTON / VK_RBUTTON in the key state; A sends Enter, read as
 * the left button). The game's DirectInput keyboard got no keys from Wine either,
 * so the proxy builds that too, from the key state, and drops Wine's; each arrow
 * key also presses W/S/A/D: the D-pad navigates the menus (window messages) and
 * moves in the game (DirectInput).
 *
 * The cursor is re-centred while the game hides it, unless the game does that
 * itself (CursorCenter): Condemned.exe's SetCursorPos is hooked so those jumps are
 * not read as movement, and the whole-pixel steps are smoothed over a few reads.
 *
 * Not input, but the same trick: the game's sound asks DirectSound for hardware
 * buffers, which Wine refuses, so the proxy also hooks how SndDrv.dll gets its
 * DirectSound and makes those buffers software ones (see "sound" below). It also
 * gives DXVK a folder for its shader cache, which Wine-NX's environment lacks, and
 * buffers the game's tiny file writes, which made every save take minutes.
 *
 * Everything the proxy sees is reported to wine-nx-runtime.log as
 * [DINPUT8 PROXY] lines. */
#define COBJMACROS
#define DIRECTINPUT_VERSION 0x0800
#include <stdarg.h>
#include <windows.h>
#include <winternl.h>
#include <dinput.h>
#include <dsound.h>

__declspec(dllimport) NTSTATUS NTAPI NtDisplayString( const UNICODE_STRING *str );

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/* Built without a C runtime; the compiler still expects these for structures. */
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

void *memmove( void *dst, const void *src, size_t n )
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d <= s || d >= s + n) return memcpy( dst, src, n );
    while (n--) d[n] = s[n];
    return dst;
}

/* ---- log: "[DINPUT8 PROXY] ..." with %s %d %u %x ---- */

static void log_line( const char *fmt, ... )
{
    static const char hex[] = "0123456789abcdef";
    const char *prefix = "[DINPUT8 PROXY] ";
    WCHAR buffer[400];
    UNICODE_STRING str;
    unsigned int n = 0, limit = ARRAY_SIZE(buffer) - 2;
    va_list args;

    va_start( args, fmt );
    while (*prefix) buffer[n++] = *prefix++;
    for (; *fmt && n < limit; fmt++)
    {
        char digits[12];
        unsigned int value, len = 0;
        const char *text;

        if (*fmt != '%' || !fmt[1]) { buffer[n++] = (unsigned char)*fmt; continue; }
        switch (*++fmt)
        {
        case 's':
            for (text = va_arg( args, const char * ); text && *text && n < limit; text++)
                buffer[n++] = (unsigned char)*text;
            continue;
        case 'd':
        {
            int signed_value = va_arg( args, int );
            if (signed_value < 0) { buffer[n++] = '-'; value = 0u - (unsigned int)signed_value; }
            else value = signed_value;
            do digits[len++] = '0' + value % 10; while ((value /= 10));
            break;
        }
        case 'u':
            value = va_arg( args, unsigned int );
            do digits[len++] = '0' + value % 10; while ((value /= 10));
            break;
        case 'x':
            value = va_arg( args, unsigned int );
            do digits[len++] = hex[value & 15]; while ((value >>= 4));
            break;
        default:
            buffer[n++] = (unsigned char)*fmt;
            continue;
        }
        while (len && n < limit) buffer[n++] = digits[--len];
    }
    va_end( args );
    str.Buffer = buffer;
    str.Length = n * sizeof(WCHAR);
    str.MaximumLength = str.Length;
    NtDisplayString( &str );
}

/* ---- GUIDs, defined here instead of linking dxguid ---- */

#define DI_GUID( name, l, w1, w2 ) \
    static const GUID name = { l, w1, w2, { 0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } }
DI_GUID( guid_sysmouse,     0x6f1d2b60, 0xd5a0, 0x11cf );
DI_GUID( guid_syskeyboard,  0x6f1d2b61, 0xd5a0, 0x11cf );
DI_GUID( guid_sysmouse_em,  0x6f1d2b80, 0xd5a0, 0x11cf );
DI_GUID( guid_sysmouse_em2, 0x6f1d2b81, 0xd5a0, 0x11cf );
DI_GUID( guid_xaxis,        0xa36d02e0, 0xc9f3, 0x11cf );
DI_GUID( guid_yaxis,        0xa36d02e1, 0xc9f3, 0x11cf );
DI_GUID( guid_zaxis,        0xa36d02e2, 0xc9f3, 0x11cf );
DI_GUID( guid_button,       0xa36d02f0, 0xc9f3, 0x11cf );
DI_GUID( guid_key,          0x55728220, 0xd33c, 0x11cf );

static BOOL same_guid( const GUID *a, const GUID *b )
{
    const DWORD *x = (const DWORD *)a, *y = (const DWORD *)b;
    return x[0] == y[0] && x[1] == y[1] && x[2] == y[2] && x[3] == y[3];
}

static BOOL is_mouse_guid( const GUID *guid )
{
    return guid && (same_guid( guid, &guid_sysmouse ) || same_guid( guid, &guid_sysmouse_em ) ||
                    same_guid( guid, &guid_sysmouse_em2 ));
}

/* ---- the mouse ---- */

/* Wine's mouse objects in order (dlls/dinput/mouse.c): X, Y and the wheel are
 * relative axes 0-2, button n is a push button with instance 3 + n. */
#define MOUSE_BUTTONS 8
#define MOUSE_OBJECTS (3 + MOUSE_BUTTONS)
#define QUEUE_SIZE 256

struct mouse
{
    IDirectInputDevice8A *device;
    DWORD coop;                     /* SetCooperativeLevel flags */
    BOOL acquired, absolute;
    int ofs[MOUSE_OBJECTS];         /* offset of each object in the game's data format, -1 if absent */
    DWORD data_size;
    LONG state_dx, state_dy, abs_x, abs_y;
    BYTE buttons[MOUSE_BUTTONS];    /* last state handed out, 0x80 = down */
    DIDEVICEOBJECTDATA queue[QUEUE_SIZE];
    unsigned int head, count;
    BOOL overflow;
    DWORD sequence;
};

static struct mouse mice[4];
static IDirectInputDevice8AVtbl mouse_vtbl, orig_mouse;
static BOOL mouse_vtbl_ready;

static POINT last_cursor;
static BOOL last_cursor_valid;
static DWORD last_game_warp;        /* GetTickCount of the game's last SetCursorPos */
static unsigned int game_warps, own_warps, state_reads, data_reads, clicks;
static unsigned int kb_aliased, kb_synth_events, kb_wine_events, kb_wine_state, sound_buffers, soft_buffers;
static LONG moved_x, moved_y;
static DWORD last_report;
static IDirectInputDevice8A *keyboard;

static struct mouse *find_mouse( IDirectInputDevice8A *device )
{
    unsigned int i;
    for (i = 0; i < ARRAY_SIZE(mice); i++) if (mice[i].device == device) return &mice[i];
    return NULL;
}

static const char *device_name( IDirectInputDevice8A *device )
{
    if (device && device == keyboard) return "keyboard";
    return find_mouse( device ) ? "mouse" : "device";
}

/* ---- diagnostics: what reaches the game's window procedure ----
 * GameClient.dll subclasses the game window (SetWindowLongA) and its menus take
 * the mouse from window messages. Once the menus are up, the proxy puts itself in
 * front of that procedure, once, and counts and logs what it is handed. */

static HWND spy_hwnd;
static WNDPROC game_proc;
static BOOL spy_installed;
static unsigned int spy_moves, spy_buttons, spy_keys, spy_raw;

static LRESULT CALLBACK spy_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    LRESULT ret = CallWindowProcA( game_proc, hwnd, msg, wparam, lparam );

    switch (msg)
    {
    case WM_MOUSEMOVE:
        spy_moves++;
        break;
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
        if (spy_buttons++ < 24)
            log_line( "window message %x at %d,%d, keys %x -> game returned %x", msg,
                      (int)(short)LOWORD(lparam), (int)(short)HIWORD(lparam), (unsigned int)wparam, (unsigned int)ret );
        break;
    case WM_KEYDOWN: case WM_KEYUP: case WM_CHAR: case WM_SYSKEYDOWN: case WM_SYSKEYUP:
        if (spy_keys++ < 24)
            log_line( "window message %x key %x -> game returned %x", msg, (unsigned int)wparam, (unsigned int)ret );
        break;
    case WM_INPUT:
        spy_raw++;
        break;
    }
    return ret;
}

static void install_spy( void )
{
    WNDPROC current;

    if (spy_installed || !spy_hwnd) return;
    spy_installed = TRUE;
    current = (WNDPROC)GetWindowLongA( spy_hwnd, GWL_WNDPROC );
    if (!current)
    {
        log_line( "window %x: no window procedure to watch", (unsigned int)(UINT_PTR)spy_hwnd );
        return;
    }
    game_proc = current;
    SetWindowLongA( spy_hwnd, GWL_WNDPROC, (LONG)(LONG_PTR)spy_proc );
    log_line( "watching window %x, procedure %x", (unsigned int)(UINT_PTR)spy_hwnd, (unsigned int)(UINT_PTR)current );
}

/* ---- ShowCursor in Condemned.exe: whether the game hides the system cursor ---- */

static int (WINAPI *real_ShowCursor)( BOOL show );
static int cursor_display_count;    /* below 0: the game hides the cursor, i.e. it is playing */

static int WINAPI hook_ShowCursor( BOOL show )
{
    static unsigned int logged;
    int count = real_ShowCursor( show );

    cursor_display_count = count;
    if (logged++ < 10) log_line( "game ShowCursor(%s) -> display count %d", show ? "TRUE" : "FALSE", count );
    return count;
}

/* The key state behind each button. The pad has no spare mouse buttons, so
 * Condemned.keys.txt gives them keys and the proxy reads those as buttons too:
 * A sends Enter (menus: select; game: Fire = left button), R3 sends F24
 * (game: Focus = middle button). */
static BYTE button_state( unsigned int i )
{
    static const int vk[] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2 };

    if (i >= ARRAY_SIZE(vk)) return 0;
    if (GetAsyncKeyState( vk[i] ) & 0x8000) return 0x80;
    if (i == 0 && (GetAsyncKeyState( VK_RETURN ) & 0x8000)) return 0x80;
    if (i == 2 && (GetAsyncKeyState( VK_F24 ) & 0x8000)) return 0x80;
    return 0;
}

static void queue_event( struct mouse *mouse, unsigned int object, DWORD data )
{
    DIDEVICEOBJECTDATA *event;

    if (mouse->ofs[object] < 0) return;
    if (mouse->count == QUEUE_SIZE)
    {
        mouse->head = (mouse->head + 1) % QUEUE_SIZE;
        mouse->count--;
        mouse->overflow = TRUE;
    }
    event = &mouse->queue[(mouse->head + mouse->count) % QUEUE_SIZE];
    event->dwOfs = mouse->ofs[object];
    event->dwData = data;
    event->dwTimeStamp = GetTickCount();
    event->dwSequence = ++mouse->sequence;
    event->uAppData = (UINT_PTR)-1;
    mouse->count++;
}

static void report_stats( void )
{
    DWORD now = GetTickCount();

    if (now - last_report < 10000) return;
    last_report = now;
    log_line( "stats: mouse state reads %u, data reads %u, moved %d,%d, clicks %u, game SetCursorPos %u, "
              "own recentre %u, cursor count %d; window got moves %u, buttons %u, keys %u, raw %u",
              state_reads, data_reads, moved_x, moved_y, clicks, game_warps, own_warps, cursor_display_count,
              spy_moves, spy_buttons, spy_keys, spy_raw );
    log_line( "stats: keyboard events %u (arrow aliases %u), Wine keyboard events dropped %u, "
              "Wine state reads with a key %u; sound buffers %u, made software %u",
              kb_synth_events, kb_aliased, kb_wine_events, kb_wine_state, sound_buffers, soft_buffers );
}

/* The cursor moves in whole pixels and unevenly from one read to the next: a
 * slow stick gives 0, 0, 1, 0, 2..., a burst 3, 0, 2. Half of what is pending
 * goes out on each read (at least a pixel, so nothing is held back), the rest
 * on the following ones: a 3-pixel burst becomes 1, 1, 1. Kept in 1/256 px. */
static LONG pending_x, pending_y;

static LONG smooth( LONG *pending, LONG raw )
{
    LONG out;

    *pending += raw * 256;
    out = *pending / 512;
    if (!out && *pending >= 256) out = 1;
    else if (!out && *pending <= -256) out = -1;
    *pending -= out * 256;
    return out;
}

static void watch_file_modules( void );

/* Cursor movement and button changes since the last read, into both paths. */
static void sample( struct mouse *mouse )
{
    POINT pos = last_cursor;
    LONG dx = 0, dy = 0, sx, sy;
    unsigned int i;

    install_spy();
    watch_file_modules();
    if (GetCursorPos( &pos ))
    {
        if (last_cursor_valid)
        {
            dx = pos.x - last_cursor.x;
            dy = pos.y - last_cursor.y;
        }
        last_cursor = pos;
        last_cursor_valid = TRUE;
    }
    moved_x += dx;
    moved_y += dy;
    sx = smooth( &pending_x, dx );
    sy = smooth( &pending_y, dy );
    if (sx || sy)
    {
        mouse->state_dx += sx;
        mouse->state_dy += sy;
        if (sx) queue_event( mouse, 0, (DWORD)sx );
        if (sy) queue_event( mouse, 1, (DWORD)sy );
    }
    for (i = 0; i < MOUSE_BUTTONS; i++)
    {
        BYTE state = button_state( i );
        if (state == mouse->buttons[i]) continue;
        mouse->buttons[i] = state;
        queue_event( mouse, 3 + i, state );
        if (state) clicks++;
    }

    /* Playing (the game hides the cursor, or holds the mouse exclusively), but not
     * keeping the cursor centred itself: do it, or looking around stops at the
     * screen's edge. In the menus the cursor is shown and left where it is. */
    if (mouse->acquired && (cursor_display_count < 0 || (mouse->coop & DISCL_EXCLUSIVE)) &&
        GetTickCount() - last_game_warp > 1000)
    {
        int cx = GetSystemMetrics( SM_CXSCREEN ) / 2, cy = GetSystemMetrics( SM_CYSCREEN ) / 2;
        if (pos.x < cx - cx / 2 || pos.x > cx + cx / 2 || pos.y < cy - cy / 2 || pos.y > cy + cy / 2)
        {
            SetCursorPos( cx, cy );
            last_cursor.x = cx;
            last_cursor.y = cy;
            own_warps++;
        }
    }
    report_stats();
}

/* Which of the mouse's objects each entry of the game's format stands for, the
 * way Wine matches them (dlls/dinput/device.c, match_device_object). */
static void map_format( struct mouse *mouse, const DIDATAFORMAT *format )
{
    BOOL matched[MOUSE_OBJECTS] = {0};
    DWORD i, j;

    for (j = 0; j < MOUSE_OBJECTS; j++) mouse->ofs[j] = -1;
    mouse->data_size = format->dwDataSize;
    for (i = 0; i < format->dwNumObjs; i++)
    {
        const DIOBJECTDATAFORMAT *obj = &format->rgodf[i];
        DWORD instance = DIDFT_GETINSTANCE( obj->dwType );

        for (j = 0; j < MOUSE_OBJECTS; j++)
        {
            const GUID *guid = j == 0 ? &guid_xaxis : j == 1 ? &guid_yaxis : j == 2 ? &guid_zaxis : &guid_button;
            DWORD type = j < 3 ? DIDFT_RELAXIS : DIDFT_PSHBUTTON;

            if (matched[j]) continue;
            if (obj->pguid && !same_guid( obj->pguid, guid )) continue;
            if (instance != DIDFT_GETINSTANCE( DIDFT_ANYINSTANCE ) && instance != j) continue;
            if (!(DIDFT_GETTYPE( obj->dwType ) & type)) continue;
            matched[j] = TRUE;
            mouse->ofs[j] = obj->dwOfs;
            break;
        }
    }
    log_line( "data format: %u bytes, %u objects; X at %d, Y at %d, wheel at %d, buttons at %d %d %d",
              format->dwDataSize, format->dwNumObjs, mouse->ofs[0], mouse->ofs[1], mouse->ofs[2],
              mouse->ofs[3], mouse->ofs[4], mouse->ofs[5] );
}

static ULONG WINAPI mouse_Release( IDirectInputDevice8A *iface )
{
    struct mouse *mouse = find_mouse( iface );
    ULONG ref = orig_mouse.Release( iface );

    if (!ref && mouse)
    {
        log_line( "mouse %x released", (unsigned int)(UINT_PTR)iface );
        memset( mouse, 0, sizeof(*mouse) );
    }
    return ref;
}

static HRESULT WINAPI mouse_SetProperty( IDirectInputDevice8A *iface, REFGUID prop, const DIPROPHEADER *header )
{
    struct mouse *mouse = find_mouse( iface );
    HRESULT hr = orig_mouse.SetProperty( iface, prop, header );

    if (SUCCEEDED(hr) && mouse && prop == DIPROP_AXISMODE && header && header->dwHow == DIPH_DEVICE)
    {
        mouse->absolute = ((const DIPROPDWORD *)header)->dwData == DIPROPAXISMODE_ABS;
        log_line( "axis mode %s", mouse->absolute ? "absolute" : "relative" );
    }
    return hr;
}

static HRESULT WINAPI mouse_Acquire( IDirectInputDevice8A *iface )
{
    static unsigned int logged;
    struct mouse *mouse = find_mouse( iface );
    HRESULT hr = orig_mouse.Acquire( iface );

    if (logged++ < 20) log_line( "%s Acquire -> %x", device_name( iface ), (unsigned int)hr );
    if (SUCCEEDED(hr) && mouse)
    {
        unsigned int i;
        mouse->acquired = TRUE;
        last_cursor_valid = FALSE;   /* Wine may have warped the cursor */
        for (i = 0; i < MOUSE_BUTTONS; i++) mouse->buttons[i] = button_state( i );
    }
    return hr;
}

static HRESULT WINAPI mouse_Unacquire( IDirectInputDevice8A *iface )
{
    static unsigned int logged;
    struct mouse *mouse = find_mouse( iface );
    HRESULT hr = orig_mouse.Unacquire( iface );

    if (logged++ < 20) log_line( "%s Unacquire -> %x", device_name( iface ), (unsigned int)hr );
    if (mouse) mouse->acquired = FALSE;
    return hr;
}

/* ---- the keyboard ----
 * Wine's DirectInput 8 keyboard is no more use to the game than its mouse: in
 * every run not one arrow key reached it, although the menus got hundreds of key
 * messages. So the proxy builds the game's keyboard data itself and drops what
 * Wine hands over (only counting it, for the log):
 * - the key state comes from GetKeyboardState, once per read, and for the keys
 *   Condemned.keys.txt can send, from GetAsyncKeyState too (the server keeps both
 *   up to date; the mouse buttons already work that way);
 * - the D-pad and the left stick send arrow keys, which the menus navigate with,
 *   while the game moves with W/S/A/D: every arrow also presses its letter. */

static const struct { BYTE from, to; } key_alias[] =
{
    { DIK_UP, DIK_W }, { DIK_DOWN, DIK_S }, { DIK_LEFT, DIK_A }, { DIK_RIGHT, DIK_D },
};

/* Everything Condemned.keys.txt gives the pad, and the letters the arrows stand for */
static const BYTE pad_vks[] =
{
    VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_RETURN, VK_ESCAPE, VK_TAB, VK_SPACE, VK_SHIFT, VK_CONTROL,
    'E', 'F', 'T', 'R', 'W', 'A', 'S', 'D', VK_F24,
};

static int kb_ofs[256];             /* offset of each DIK key in the game's keyboard format, -1 if absent */
static BOOL kb_mapped;
static BYTE kb_vk[256];             /* virtual key behind each DIK key, 0 if none */
static BYTE kb_down[256];           /* state handed out, 0x80 = down */
static DIDEVICEOBJECTDATA kb_queue[128];
static unsigned int kb_head, kb_count;
static BOOL kb_overflow;
static DWORD kb_sequence;

static BYTE dik_to_vk( unsigned int dik )
{
    static const struct { BYTE dik, vk; } extended[] =
    {
        { DIK_UP, VK_UP }, { DIK_DOWN, VK_DOWN }, { DIK_LEFT, VK_LEFT }, { DIK_RIGHT, VK_RIGHT },
        { DIK_HOME, VK_HOME }, { DIK_END, VK_END }, { DIK_PRIOR, VK_PRIOR }, { DIK_NEXT, VK_NEXT },
        { DIK_INSERT, VK_INSERT }, { DIK_DELETE, VK_DELETE }, { DIK_RCONTROL, VK_RCONTROL },
        { DIK_RMENU, VK_RMENU }, { DIK_DIVIDE, VK_DIVIDE }, { DIK_LWIN, VK_LWIN }, { DIK_RWIN, VK_RWIN },
        { DIK_APPS, VK_APPS },
    };
    unsigned int i, vk;

    if (dik & 0x80)
    {
        for (i = 0; i < ARRAY_SIZE(extended); i++) if (extended[i].dik == dik) return extended[i].vk;
        return 0;
    }
    vk = MapVirtualKeyA( dik, MAPVK_VSC_TO_VK_EX );
    /* The numeric keypad's scan codes also map to the arrows and friends: leave
     * those keys out, or every arrow would press a keypad key too. */
    if (dik >= DIK_NUMPAD7 && dik <= DIK_DECIMAL)
        switch (vk)
        {
        case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT: case VK_HOME: case VK_END:
        case VK_PRIOR: case VK_NEXT: case VK_INSERT: case VK_DELETE: case VK_CLEAR: case VK_RETURN:
            return 0;
        }
    return vk < 256 ? vk : 0;
}

static BOOL vk_down( const BYTE *keys, BYTE vk )
{
    if (keys[vk] & 0x80) return TRUE;
    switch (vk)   /* the pad sends the generic modifiers */
    {
    case VK_LSHIFT:   return (keys[VK_SHIFT] & 0x80) != 0;
    case VK_LCONTROL: return (keys[VK_CONTROL] & 0x80) != 0;
    case VK_LMENU:    return (keys[VK_MENU] & 0x80) != 0;
    }
    return FALSE;
}

static void map_keyboard_format( const DIDATAFORMAT *format )
{
    DWORD i;

    for (i = 0; i < 256; i++)
    {
        kb_ofs[i] = -1;
        kb_vk[i] = dik_to_vk( i );
    }
    for (i = 0; i < format->dwNumObjs; i++)
    {
        const DIOBJECTDATAFORMAT *obj = &format->rgodf[i];
        DWORD instance = DIDFT_GETINSTANCE( obj->dwType );

        if (!(DIDFT_GETTYPE( obj->dwType ) & DIDFT_BUTTON)) continue;
        if (obj->pguid && !same_guid( obj->pguid, &guid_key )) continue;
        if (instance < 256) kb_ofs[instance] = obj->dwOfs;
    }
    kb_mapped = TRUE;
    log_line( "keyboard format: %u bytes, %u objects; up %d / w %d, down %d / s %d, left %d / a %d, right %d / d %d",
              format->dwDataSize, format->dwNumObjs, kb_ofs[DIK_UP], kb_ofs[DIK_W], kb_ofs[DIK_DOWN], kb_ofs[DIK_S],
              kb_ofs[DIK_LEFT], kb_ofs[DIK_A], kb_ofs[DIK_RIGHT], kb_ofs[DIK_D] );
    log_line( "keyboard keys: up -> vk %x, return -> vk %x, e -> vk %x, lshift -> vk %x, lcontrol -> vk %x",
              kb_vk[DIK_UP], kb_vk[DIK_RETURN], kb_vk[DIK_E], kb_vk[DIK_LSHIFT], kb_vk[DIK_LCONTROL] );
}

static void kb_sample( void )
{
    BYTE keys[256], state;
    unsigned int dik, i;
    DIDEVICEOBJECTDATA *event;

    if (!kb_mapped) return;
    if (!GetKeyboardState( keys )) memset( keys, 0, sizeof(keys) );
    for (i = 0; i < ARRAY_SIZE(pad_vks); i++)
        if (GetAsyncKeyState( pad_vks[i] ) & 0x8000) keys[pad_vks[i]] |= 0x80;

    for (dik = 0; dik < 256; dik++)
    {
        if (kb_ofs[dik] < 0) continue;
        state = kb_vk[dik] && vk_down( keys, kb_vk[dik] ) ? 0x80 : 0;
        for (i = 0; i < ARRAY_SIZE(key_alias); i++)
            if (key_alias[i].to == dik && vk_down( keys, kb_vk[key_alias[i].from] ))
            {
                if (!state) kb_aliased++;
                state = 0x80;
            }
        if (state == kb_down[dik]) continue;
        kb_down[dik] = state;
        if (kb_count == ARRAY_SIZE(kb_queue))
        {
            kb_head = (kb_head + 1) % ARRAY_SIZE(kb_queue);
            kb_count--;
            kb_overflow = TRUE;
        }
        event = &kb_queue[(kb_head + kb_count++) % ARRAY_SIZE(kb_queue)];
        event->dwOfs = kb_ofs[dik];
        event->dwData = state;
        event->dwTimeStamp = GetTickCount();
        event->dwSequence = ++kb_sequence;
        event->uAppData = (UINT_PTR)-1;
        kb_synth_events++;
    }
}

static HRESULT keyboard_get_state( HRESULT hr, BYTE *bytes, DWORD size )
{
    unsigned int i;

    if (FAILED(hr) || !bytes || !kb_mapped) return hr;
    for (i = 0; i < size; i++) if (bytes[i]) { kb_wine_state++; break; }
    kb_sample();
    for (i = 0; i < 256; i++)
        if (kb_ofs[i] >= 0 && kb_ofs[i] < (int)size) bytes[kb_ofs[i]] = kb_down[i];
    return hr;
}

static HRESULT keyboard_get_data( HRESULT hr, DIDEVICEOBJECTDATA *data, DWORD size, DWORD *count, DWORD want,
                                  DWORD flags )
{
    DWORD n, i;

    if (FAILED(hr) || !count || !kb_mapped) return hr;
    kb_wine_events += *count;   /* dropped: the proxy's own events replace them */
    kb_sample();
    n = kb_count < want ? kb_count : want;
    if (data)
        for (i = 0; i < n; i++)
            memcpy( (BYTE *)data + i * size, &kb_queue[(kb_head + i) % ARRAY_SIZE(kb_queue)],
                    size < sizeof(DIDEVICEOBJECTDATA) ? size : sizeof(DIDEVICEOBJECTDATA) );
    if (!(flags & DIGDD_PEEK))
    {
        kb_head = (kb_head + n) % ARRAY_SIZE(kb_queue);
        kb_count -= n;
    }
    *count = n;
    if (kb_overflow)
    {
        kb_overflow = FALSE;
        return DI_BUFFEROVERFLOW;
    }
    return hr;
}

static HRESULT WINAPI mouse_GetDeviceState( IDirectInputDevice8A *iface, DWORD size, void *data )
{
    static unsigned int logged;
    struct mouse *mouse = find_mouse( iface );
    HRESULT hr = orig_mouse.GetDeviceState( iface, size, data );
    BYTE *bytes = data;
    unsigned int i;

    if (logged++ < 3) log_line( "%s GetDeviceState(%u) -> %x", device_name( iface ), size, (unsigned int)hr );
    if (iface == keyboard) return keyboard_get_state( hr, bytes, size );
    if (FAILED(hr) || !mouse || !data) return hr;
    state_reads++;
    sample( mouse );
    mouse->abs_x += mouse->state_dx;
    mouse->abs_y += mouse->state_dy;
    if (mouse->ofs[0] >= 0 && mouse->ofs[0] + 4 <= (int)size)
        *(LONG *)(bytes + mouse->ofs[0]) = mouse->absolute ? mouse->abs_x : mouse->state_dx;
    if (mouse->ofs[1] >= 0 && mouse->ofs[1] + 4 <= (int)size)
        *(LONG *)(bytes + mouse->ofs[1]) = mouse->absolute ? mouse->abs_y : mouse->state_dy;
    for (i = 0; i < MOUSE_BUTTONS; i++)
        if (mouse->ofs[3 + i] >= 0 && mouse->ofs[3 + i] < (int)size) bytes[mouse->ofs[3 + i]] = mouse->buttons[i];
    mouse->state_dx = mouse->state_dy = 0;
    return hr;
}

static HRESULT WINAPI mouse_GetDeviceData( IDirectInputDevice8A *iface, DWORD size, DIDEVICEOBJECTDATA *data,
                                           DWORD *count, DWORD flags )
{
    static unsigned int logged;
    struct mouse *mouse = find_mouse( iface );
    DWORD want = count ? *count : 0, n, i;
    HRESULT hr = orig_mouse.GetDeviceData( iface, size, data, count, flags );

    if (logged++ < 3) log_line( "%s GetDeviceData(%u, %u, %x) -> %x", device_name( iface ), size, want, flags,
                                (unsigned int)hr );
    if (iface == keyboard) return keyboard_get_data( hr, data, size, count, want, flags );
    if (FAILED(hr) || !mouse || !count || *count) return hr;   /* Wine's own events, if it ever has any, go first */
    data_reads++;
    sample( mouse );
    n = mouse->count < want ? mouse->count : want;
    if (data)
        for (i = 0; i < n; i++)
            memcpy( (BYTE *)data + i * size, &mouse->queue[(mouse->head + i) % QUEUE_SIZE],
                    size < sizeof(DIDEVICEOBJECTDATA) ? size : sizeof(DIDEVICEOBJECTDATA) );
    if (!(flags & DIGDD_PEEK))
    {
        mouse->head = (mouse->head + n) % QUEUE_SIZE;
        mouse->count -= n;
    }
    *count = n;
    if (mouse->overflow)
    {
        mouse->overflow = FALSE;
        return DI_BUFFEROVERFLOW;
    }
    return hr;
}

static HRESULT WINAPI mouse_SetDataFormat( IDirectInputDevice8A *iface, const DIDATAFORMAT *format )
{
    struct mouse *mouse = find_mouse( iface );
    HRESULT hr = orig_mouse.SetDataFormat( iface, format );

    if (SUCCEEDED(hr) && mouse && format) map_format( mouse, format );
    else if (SUCCEEDED(hr) && iface == keyboard && format) map_keyboard_format( format );
    else log_line( "%s SetDataFormat -> %x", device_name( iface ), (unsigned int)hr );
    return hr;
}

static HRESULT WINAPI mouse_SetCooperativeLevel( IDirectInputDevice8A *iface, HWND hwnd, DWORD flags )
{
    struct mouse *mouse = find_mouse( iface );
    HRESULT hr = orig_mouse.SetCooperativeLevel( iface, hwnd, flags );

    log_line( "%s SetCooperativeLevel(hwnd %x, %s%s%s%s) -> %x", device_name( iface ), (unsigned int)(UINT_PTR)hwnd,
              flags & DISCL_EXCLUSIVE ? "exclusive" : "nonexclusive",
              flags & DISCL_FOREGROUND ? ", foreground" : "", flags & DISCL_BACKGROUND ? ", background" : "",
              flags & DISCL_NOWINKEY ? ", no Windows key" : "", (unsigned int)hr );
    if (SUCCEEDED(hr) && mouse)
    {
        mouse->coop = flags;
        spy_hwnd = hwnd;
    }
    return hr;
}

/* The keyboard keeps working through Wine; it only gets the logging methods, and
 * only if it shares the mouse's method table (Wine's ANSI wrappers are shared). */
static IDirectInputDevice8AVtbl *wine_device_vtbl;

static void watch_keyboard( void )
{
    if (!keyboard || !wine_device_vtbl || keyboard->lpVtbl == &mouse_vtbl) return;
    if (keyboard->lpVtbl != wine_device_vtbl)
    {
        log_line( "keyboard has its own method table; not watched" );
        keyboard = NULL;
        return;
    }
    keyboard->lpVtbl = &mouse_vtbl;
    log_line( "keyboard watched" );
}

static void wrap_mouse( IDirectInputDevice8A *device )
{
    struct mouse *mouse = find_mouse( NULL );
    unsigned int i;

    if (!mouse)
    {
        log_line( "too many mouse devices, %x left to Wine", (unsigned int)(UINT_PTR)device );
        return;
    }
    if (!mouse_vtbl_ready)
    {
        wine_device_vtbl = device->lpVtbl;
        orig_mouse = *device->lpVtbl;
        mouse_vtbl = orig_mouse;
        mouse_vtbl.Release = mouse_Release;
        mouse_vtbl.SetProperty = mouse_SetProperty;
        mouse_vtbl.Acquire = mouse_Acquire;
        mouse_vtbl.Unacquire = mouse_Unacquire;
        mouse_vtbl.GetDeviceState = mouse_GetDeviceState;
        mouse_vtbl.GetDeviceData = mouse_GetDeviceData;
        mouse_vtbl.SetDataFormat = mouse_SetDataFormat;
        mouse_vtbl.SetCooperativeLevel = mouse_SetCooperativeLevel;
        mouse_vtbl_ready = TRUE;
    }
    memset( mouse, 0, sizeof(*mouse) );
    mouse->device = device;
    for (i = 0; i < MOUSE_OBJECTS; i++) mouse->ofs[i] = -1;
    device->lpVtbl = &mouse_vtbl;
    watch_keyboard();
}

/* ---- IDirectInput8A: only CreateDevice ---- */

static IDirectInput8AVtbl dinput_vtbl, orig_dinput;
static BOOL dinput_vtbl_ready;

static HRESULT WINAPI dinput_CreateDevice( IDirectInput8A *iface, REFGUID guid, IDirectInputDevice8A **out,
                                           IUnknown *outer )
{
    HRESULT hr = orig_dinput.CreateDevice( iface, guid, out, outer );

    log_line( "CreateDevice(%x-%x) -> %x%s", guid ? (unsigned int)guid->Data1 : 0,
              guid ? (unsigned int)guid->Data2 : 0, (unsigned int)hr,
              SUCCEEDED(hr) && is_mouse_guid( guid ) ? ", the mouse: wrapped" : "" );
    if (SUCCEEDED(hr) && out && *out && is_mouse_guid( guid )) wrap_mouse( *out );
    if (SUCCEEDED(hr) && out && *out && guid && same_guid( guid, &guid_syskeyboard ))
    {
        keyboard = *out;
        watch_keyboard();
    }
    return hr;
}

/* ---- SetCursorPos in Condemned.exe ---- */

static BOOL (WINAPI *real_SetCursorPos)( int x, int y );

static BOOL WINAPI hook_SetCursorPos( int x, int y )
{
    BOOL ret = real_SetCursorPos( x, y );

    if (game_warps++ < 5) log_line( "game SetCursorPos(%d, %d)", x, y );
    last_cursor.x = x;
    last_cursor.y = y;
    last_cursor_valid = TRUE;
    last_game_warp = GetTickCount();
    return ret;
}

/* ---- OutputDebugStringA in Condemned.exe: the engine's own messages ----
 * The engine reports sound driver failures ("Failed to initialize sound driver
 * '%s'" and the like) through its console; this makes what it sends to the
 * debugger show up in wine-nx-runtime.log, one line each, up to a limit. */

static void (WINAPI *real_OutputDebugStringA)( const char *text );

static void WINAPI hook_OutputDebugStringA( const char *text )
{
    static unsigned int logged;
    const char *p = text;
    char line[300];
    unsigned int n = 0;

    if (p && logged < 400)
    {
        for (; *p && n < sizeof(line) - 1; p++)
            if (*p != '\r' && *p != '\n') line[n++] = *p;
            else if (n) break;
        line[n] = 0;
        if (n)
        {
            logged++;
            log_line( "engine: %s", line );
        }
    }
    if (real_OutputDebugStringA) real_OutputDebugStringA( text );
}

/* Point the module's import of dll!func at hook; matched by address, so it works
 * whether or not the import table keeps the names. */
static BOOL hook_import( HMODULE module, const char *dll, const char *func, void *hook, void **orig )
{
    BYTE *base = (BYTE *)module;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *desc;
    HMODULE target = GetModuleHandleA( dll );
    void *address = target ? (void *)GetProcAddress( target, func ) : NULL;

    if (!address || !dir->VirtualAddress) return FALSE;
    for (desc = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress); desc->Name; desc++)
    {
        IMAGE_THUNK_DATA *thunk;

        if (lstrcmpiA( (const char *)(base + desc->Name), dll )) continue;
        for (thunk = (IMAGE_THUNK_DATA *)(base + desc->FirstThunk); thunk->u1.Function; thunk++)
        {
            DWORD old;

            if ((void *)thunk->u1.Function != address) continue;
            if (!VirtualProtect( &thunk->u1.Function, sizeof(thunk->u1.Function), PAGE_READWRITE, &old ))
                return FALSE;
            *orig = address;
            thunk->u1.Function = (ULONG_PTR)hook;
            VirtualProtect( &thunk->u1.Function, sizeof(thunk->u1.Function), old, &old );
            return TRUE;
        }
    }
    return FALSE;
}

/* ---- sound: no hardware buffers ----
 * The game's SndDrv.dll asks DirectSound for hardware buffers (DSBCAPS_LOCHARDWARE).
 * Wine has none (dlls/dsound/dsound.c: "unable to create hardware buffer",
 * DSERR_UNSUPPORTED) and the game stays silent. SndDrv.dll gets its DirectSound
 * from EAX.DLL (EAXDirectSoundCreate8, ordinal 6) and Condemned.exe loads it with
 * LoadLibraryA: that import is hooked, then SndDrv's import of the EAX function,
 * then CreateSoundBuffer of the DirectSound it returns, which drops the flag so
 * Wine mixes those buffers in software like any other. */

static HMODULE (WINAPI *real_LoadLibraryA)( const char *name );
static HRESULT (WINAPI *real_EAXDirectSoundCreate8)( const GUID *guid, IDirectSound8 **out, IUnknown *outer );
static IDirectSound8Vtbl ds_vtbl, orig_ds;
static const IDirectSound8Vtbl *wine_ds_vtbl;

static HRESULT WINAPI ds_CreateSoundBuffer( IDirectSound8 *iface, const DSBUFFERDESC *desc,
                                            IDirectSoundBuffer **out, IUnknown *outer )
{
    static unsigned int logged;
    DWORD flags = desc ? desc->dwFlags : 0;
    DSBUFFERDESC copy;
    HRESULT hr;

    if (desc && (flags & DSBCAPS_LOCHARDWARE) && !(flags & DSBCAPS_PRIMARYBUFFER))
    {
        memset( &copy, 0, sizeof(copy) );
        memcpy( &copy, desc, desc->dwSize < sizeof(copy) ? desc->dwSize : sizeof(copy) );
        copy.dwFlags &= ~DSBCAPS_LOCHARDWARE;
        desc = &copy;
        soft_buffers++;
    }
    hr = orig_ds.CreateSoundBuffer( iface, desc, out, outer );
    sound_buffers++;
    if (logged < 12 || (FAILED(hr) && logged < 40))
    {
        logged++;
        log_line( "CreateSoundBuffer(flags %x, %u bytes)%s -> %x", (unsigned int)flags,
                  desc ? (unsigned int)desc->dwBufferBytes : 0,
                  desc == &copy ? " made software" : "", (unsigned int)hr );
    }
    return hr;
}

/* Diagnostics: every other call SndDrv.dll makes on its DirectSound, to see how
 * far its start-up gets (no buffer was ever created in build 6). */
static unsigned int ds_logged;
#define DS_LOG( ... ) do { if (ds_logged++ < 60) log_line( __VA_ARGS__ ); } while (0)

static HRESULT WINAPI ds_QueryInterface( IDirectSound8 *iface, REFIID iid, void **out )
{
    HRESULT hr = orig_ds.QueryInterface( iface, iid, out );
    DS_LOG( "DirectSound QueryInterface(%x-%x) -> %x", iid ? (unsigned int)iid->Data1 : 0,
            iid ? (unsigned int)iid->Data2 : 0, (unsigned int)hr );
    return hr;
}

static ULONG WINAPI ds_Release( IDirectSound8 *iface )
{
    ULONG ref = orig_ds.Release( iface );
    DS_LOG( "DirectSound Release -> %u", (unsigned int)ref );
    return ref;
}

static HRESULT WINAPI ds_GetCaps( IDirectSound8 *iface, DSCAPS *caps )
{
    HRESULT hr = orig_ds.GetCaps( iface, caps );
    DS_LOG( "DirectSound GetCaps -> %x: flags %x, hw mixing %u (free %u), hw 3D %u (free %u)", (unsigned int)hr,
            caps ? (unsigned int)caps->dwFlags : 0, caps ? (unsigned int)caps->dwMaxHwMixingAllBuffers : 0,
            caps ? (unsigned int)caps->dwFreeHwMixingAllBuffers : 0, caps ? (unsigned int)caps->dwMaxHw3DAllBuffers : 0,
            caps ? (unsigned int)caps->dwFreeHw3DAllBuffers : 0 );
    return hr;
}

static HRESULT WINAPI ds_DuplicateSoundBuffer( IDirectSound8 *iface, IDirectSoundBuffer *src, IDirectSoundBuffer **out )
{
    HRESULT hr = orig_ds.DuplicateSoundBuffer( iface, src, out );
    DS_LOG( "DirectSound DuplicateSoundBuffer -> %x", (unsigned int)hr );
    return hr;
}

static HRESULT WINAPI ds_SetCooperativeLevel( IDirectSound8 *iface, HWND hwnd, DWORD level )
{
    HRESULT hr = orig_ds.SetCooperativeLevel( iface, hwnd, level );
    DS_LOG( "DirectSound SetCooperativeLevel(hwnd %x, level %u) -> %x", (unsigned int)(UINT_PTR)hwnd,
            (unsigned int)level, (unsigned int)hr );
    return hr;
}

static HRESULT WINAPI ds_GetSpeakerConfig( IDirectSound8 *iface, DWORD *config )
{
    HRESULT hr = orig_ds.GetSpeakerConfig( iface, config );
    DS_LOG( "DirectSound GetSpeakerConfig -> %x (%x)", (unsigned int)hr, config ? (unsigned int)*config : 0 );
    return hr;
}

static HRESULT WINAPI ds_SetSpeakerConfig( IDirectSound8 *iface, DWORD config )
{
    HRESULT hr = orig_ds.SetSpeakerConfig( iface, config );
    DS_LOG( "DirectSound SetSpeakerConfig(%x) -> %x", (unsigned int)config, (unsigned int)hr );
    return hr;
}

static HRESULT WINAPI ds_Initialize( IDirectSound8 *iface, const GUID *guid )
{
    HRESULT hr = orig_ds.Initialize( iface, guid );
    DS_LOG( "DirectSound Initialize -> %x", (unsigned int)hr );
    return hr;
}

/* How SndDrv.dll finds the window it gives DirectSound */
static HWND (WINAPI *real_FindWindowA)( const char *cls, const char *title );
static HWND (WINAPI *real_GetForegroundWindow)( void );

static HWND WINAPI hook_FindWindowA( const char *cls, const char *title )
{
    HWND hwnd = real_FindWindowA( cls, title );
    DS_LOG( "SndDrv FindWindowA(%s, %s) -> %x", (UINT_PTR)cls > 0xffff ? cls : "(atom)",
            (UINT_PTR)title > 0xffff ? title : "(null)", (unsigned int)(UINT_PTR)hwnd );
    return hwnd;
}

static HWND WINAPI hook_GetForegroundWindow( void )
{
    HWND hwnd = real_GetForegroundWindow();
    DS_LOG( "SndDrv GetForegroundWindow -> %x", (unsigned int)(UINT_PTR)hwnd );
    return hwnd;
}

static HRESULT WINAPI hook_EAXDirectSoundCreate8( const GUID *guid, IDirectSound8 **out, IUnknown *outer )
{
    HRESULT hr = real_EAXDirectSoundCreate8( guid, out, outer );

    log_line( "EAXDirectSoundCreate8 -> %x", (unsigned int)hr );
    if (FAILED(hr) || !out || !*out) return hr;
    if (!wine_ds_vtbl)
    {
        wine_ds_vtbl = (*out)->lpVtbl;
        orig_ds = *wine_ds_vtbl;
        ds_vtbl = orig_ds;
        ds_vtbl.CreateSoundBuffer = ds_CreateSoundBuffer;
        ds_vtbl.QueryInterface = ds_QueryInterface;
        ds_vtbl.Release = ds_Release;
        ds_vtbl.GetCaps = ds_GetCaps;
        ds_vtbl.DuplicateSoundBuffer = ds_DuplicateSoundBuffer;
        ds_vtbl.SetCooperativeLevel = ds_SetCooperativeLevel;
        ds_vtbl.GetSpeakerConfig = ds_GetSpeakerConfig;
        ds_vtbl.SetSpeakerConfig = ds_SetSpeakerConfig;
        ds_vtbl.Initialize = ds_Initialize;
    }
    if ((*out)->lpVtbl == wine_ds_vtbl) (*out)->lpVtbl = &ds_vtbl;
    else if ((*out)->lpVtbl != &ds_vtbl) log_line( "DirectSound with another method table: not watched" );
    return hr;
}

static BOOL ends_with( const char *name, const char *tail )
{
    int n = lstrlenA( name ), t = lstrlenA( tail );
    return n >= t && !lstrcmpiA( name + n - t, tail );
}

/* SndDrv.dll loads the IMA ADPCM and MP3 codecs itself (IMAADP32.ACM, L3CODECA.ACM)
 * when ACM does not list them, and starts no sound without both. */
static HMODULE (WINAPI *snd_LoadLibraryA)( const char *name );

static HMODULE WINAPI hook_snd_LoadLibraryA( const char *name )
{
    HMODULE module = snd_LoadLibraryA( name );
    log_line( "SndDrv LoadLibraryA(%s) -> %x, error %u", name, (unsigned int)(UINT_PTR)module,
              module ? 0u : (unsigned int)GetLastError() );
    return module;
}

static HMODULE WINAPI hook_LoadLibraryA( const char *name )
{
    HMODULE module = real_LoadLibraryA( name );

    /* SndDrv.dll is loaded and freed more than once; patch each copy */
    if (module && name && ends_with( name, "SndDrv.dll" ))
    {
        if (hook_import( module, "EAX.DLL", (const char *)6, hook_EAXDirectSoundCreate8,
                         (void **)&real_EAXDirectSoundCreate8 ))
            log_line( "%s: EAXDirectSoundCreate8 hooked", name );
        hook_import( module, "KERNEL32.dll", "LoadLibraryA", hook_snd_LoadLibraryA, (void **)&snd_LoadLibraryA );
        hook_import( module, "USER32.dll", "FindWindowA", hook_FindWindowA, (void **)&real_FindWindowA );
        hook_import( module, "USER32.dll", "GetForegroundWindow", hook_GetForegroundWindow,
                     (void **)&real_GetForegroundWindow );
    }
    return module;
}

/* ---- saves: buffered writes ----
 * The game writes its saves a few bytes per WriteFile. On Wine-NX each call goes
 * through the Horizon server to the SD card and waits about a millisecond, so an
 * autosave took minutes: 162,000 NtWriteFile calls over three minutes after a level
 * loaded, ~900 a second at a checkpoint, the game frozen and the CPU idle.
 *
 * The modules that write files themselves (CreateFileA/WriteFile) get their file
 * imports hooked. A file they open for writing keeps a 256 KB window of what was
 * written: the proxy keeps the game's file position itself, so writes anywhere in
 * the window and seeks cost nothing (the engine writes a chunk, seeks back to put
 * its size in front, seeks forward again). The window goes to the card in one piece
 * at its offset when a write falls outside it, before any read, time query or
 * close of that file, and before any file is opened, copied, deleted or searched
 * for, so nothing ever sees the file without its data. Build 13 kept a plain
 * buffer that every seek wrote out: saves went from minutes to ~7 s, in ~8,000
 * card writes of ~160 bytes. */
#define WB_FILES 16
#define WB_SIZE (256 * 1024)

struct wbuf
{
    HANDLE file;
    BYTE *data;
    ULONGLONG base;         /* file offset of data[0] */
    ULONGLONG pos;          /* the game's file position */
    ULONGLONG real;         /* where the real file pointer is */
    DWORD len, opened;      /* len: bytes of the window written so far */
    unsigned int writes, seeks, flushes;
    ULONGLONG bytes;
    char name[96];
};

static struct wbuf wbufs[WB_FILES];
static CRITICAL_SECTION wb_lock;
static unsigned int wb_logged;

static HANDLE (WINAPI *real_CreateFileA)( LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE );
static BOOL (WINAPI *real_WriteFile)( HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED );
static BOOL (WINAPI *real_ReadFile)( HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED );
static BOOL (WINAPI *real_SetFilePointerEx)( HANDLE, LARGE_INTEGER, PLARGE_INTEGER, DWORD );
static BOOL (WINAPI *real_GetFileSizeEx)( HANDLE, PLARGE_INTEGER );
static BOOL (WINAPI *real_GetFileTime)( HANDLE, LPFILETIME, LPFILETIME, LPFILETIME );
static BOOL (WINAPI *real_CloseHandle)( HANDLE );
static BOOL (WINAPI *real_DeleteFileA)( LPCSTR );
static BOOL (WINAPI *real_CopyFileA)( LPCSTR, LPCSTR, BOOL );
static HANDLE (WINAPI *real_FindFirstFileA)( LPCSTR, LPWIN32_FIND_DATAA );

static struct wbuf *wb_find( HANDLE file )
{
    unsigned int i;
    if (!file || file == INVALID_HANDLE_VALUE) return NULL;
    for (i = 0; i < WB_FILES; i++) if (wbufs[i].file == file) return &wbufs[i];
    return NULL;
}

static BOOL wb_seek_real( struct wbuf *wb, ULONGLONG offset )
{
    LARGE_INTEGER to;

    if (wb->real == offset) return TRUE;
    to.QuadPart = (LONGLONG)offset;
    if (!real_SetFilePointerEx( wb->file, to, NULL, FILE_BEGIN )) return FALSE;
    wb->real = offset;
    return TRUE;
}

/* The window to the card at its offset, and the real file pointer to the game's
 * position, ready for a real call. Called with wb_lock held. */
static BOOL wb_flush( struct wbuf *wb )
{
    DWORD done = 0;
    BOOL ok = TRUE;

    if (wb->len)
    {
        ok = wb_seek_real( wb, wb->base ) && real_WriteFile( wb->file, wb->data, wb->len, &done, NULL ) &&
             done == wb->len;
        wb->real = wb->base + done;
        if (!ok) log_line( "buffered write to %s failed: %u of %u bytes, error %u", wb->name,
                           (unsigned int)done, (unsigned int)wb->len, (unsigned int)GetLastError() );
        wb->len = 0;
        wb->flushes++;
    }
    wb_seek_real( wb, wb->pos );
    return ok;
}

static void wb_flush_all( void )
{
    unsigned int i;

    EnterCriticalSection( &wb_lock );
    for (i = 0; i < WB_FILES; i++) if (wbufs[i].file) wb_flush( &wbufs[i] );
    LeaveCriticalSection( &wb_lock );
}

static HANDLE WINAPI hook_CreateFileA( LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                                       DWORD disposition, DWORD flags, HANDLE template_file )
{
    HANDLE file;
    unsigned int i, n;

    wb_flush_all();
    file = real_CreateFileA( name, access, share, sa, disposition, flags, template_file );
    if (file == INVALID_HANDLE_VALUE || !(access & GENERIC_WRITE) ||
        (flags & (FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH)))
        return file;
    EnterCriticalSection( &wb_lock );
    for (i = 0; i < WB_FILES; i++)
    {
        if (wbufs[i].file) continue;
        memset( &wbufs[i], 0, sizeof(wbufs[i]) );   /* a new handle is at offset 0 */
        wbufs[i].file = file;
        wbufs[i].opened = GetTickCount();
        for (n = 0; name && name[n] && n < sizeof(wbufs[i].name) - 1; n++) wbufs[i].name[n] = name[n];
        break;
    }
    LeaveCriticalSection( &wb_lock );
    return file;
}

static BOOL WINAPI hook_WriteFile( HANDLE file, LPCVOID data, DWORD size, LPDWORD written, LPOVERLAPPED overlapped )
{
    struct wbuf *wb;
    DWORD done = 0;
    BOOL ok;

    if (overlapped) return real_WriteFile( file, data, size, written, overlapped );
    EnterCriticalSection( &wb_lock );
    if (!(wb = wb_find( file )))
    {
        LeaveCriticalSection( &wb_lock );
        return real_WriteFile( file, data, size, written, overlapped );
    }
    wb->writes++;
    wb->bytes += size;
    if (!wb->data) wb->data = VirtualAlloc( NULL, WB_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
    if (!wb->data || size >= WB_SIZE / 2)
    {
        /* straight to the card, after the window: the order stays */
        wb_flush( wb );
        ok = real_WriteFile( file, data, size, &done, NULL );
        wb->pos = wb->real = wb->pos + done;
        if (written) *written = done;
        LeaveCriticalSection( &wb_lock );
        return ok;
    }
    /* the window holds bytes base..base+len with no gaps: a write must start inside
     * it or right at its end, and fit */
    if (wb->len && (wb->pos < wb->base || wb->pos > wb->base + wb->len || wb->pos + size > wb->base + WB_SIZE))
        wb_flush( wb );
    if (!wb->len) wb->base = wb->pos;
    memcpy( wb->data + (DWORD)(wb->pos - wb->base), data, size );
    wb->pos += size;
    if (wb->pos - wb->base > wb->len) wb->len = (DWORD)(wb->pos - wb->base);
    if (written) *written = size;
    LeaveCriticalSection( &wb_lock );
    return TRUE;
}

static BOOL WINAPI hook_ReadFile( HANDLE file, LPVOID data, DWORD size, LPDWORD got, LPOVERLAPPED overlapped )
{
    struct wbuf *wb;
    DWORD done = 0;
    BOOL ok;

    if (overlapped) return real_ReadFile( file, data, size, got, overlapped );
    EnterCriticalSection( &wb_lock );
    if (!(wb = wb_find( file )))
    {
        LeaveCriticalSection( &wb_lock );
        return real_ReadFile( file, data, size, got, overlapped );
    }
    wb_flush( wb );
    ok = real_ReadFile( file, data, size, &done, NULL );
    wb->pos = wb->real = wb->pos + done;
    if (got) *got = done;
    LeaveCriticalSection( &wb_lock );
    return ok;
}

static BOOL WINAPI hook_SetFilePointerEx( HANDLE file, LARGE_INTEGER distance, PLARGE_INTEGER out, DWORD method )
{
    struct wbuf *wb;
    LONGLONG target;
    BOOL ok;

    EnterCriticalSection( &wb_lock );
    if (!(wb = wb_find( file )))
    {
        LeaveCriticalSection( &wb_lock );
        return real_SetFilePointerEx( file, distance, out, method );
    }
    wb->seeks++;
    if (method == FILE_END)   /* needs the size on the card: the window goes out first */
    {
        LARGE_INTEGER now;

        wb_flush( wb );
        ok = real_SetFilePointerEx( file, distance, &now, FILE_END );
        if (ok) wb->pos = wb->real = (ULONGLONG)now.QuadPart;
        if (ok && out) *out = now;
        LeaveCriticalSection( &wb_lock );
        return ok;
    }
    target = (method == FILE_CURRENT ? (LONGLONG)wb->pos : 0) + distance.QuadPart;
    if (target < 0 || (method != FILE_BEGIN && method != FILE_CURRENT))
    {
        LeaveCriticalSection( &wb_lock );
        SetLastError( ERROR_INVALID_PARAMETER );
        return FALSE;
    }
    wb->pos = (ULONGLONG)target;
    if (out) out->QuadPart = target;
    LeaveCriticalSection( &wb_lock );
    return TRUE;
}

static BOOL WINAPI hook_GetFileSizeEx( HANDLE file, PLARGE_INTEGER size )
{
    struct wbuf *wb;
    BOOL ok;

    EnterCriticalSection( &wb_lock );
    ok = real_GetFileSizeEx( file, size );
    /* the card's size, or the end of the window if it reaches further */
    if (ok && (wb = wb_find( file )) && wb->len && (LONGLONG)(wb->base + wb->len) > size->QuadPart)
        size->QuadPart = (LONGLONG)(wb->base + wb->len);
    LeaveCriticalSection( &wb_lock );
    return ok;
}

static BOOL WINAPI hook_GetFileTime( HANDLE file, LPFILETIME created, LPFILETIME accessed, LPFILETIME written )
{
    struct wbuf *wb;
    BOOL ok;

    EnterCriticalSection( &wb_lock );
    if ((wb = wb_find( file ))) wb_flush( wb );
    ok = real_GetFileTime( file, created, accessed, written );
    LeaveCriticalSection( &wb_lock );
    return ok;
}

static BOOL WINAPI hook_CloseHandle( HANDLE handle )
{
    struct wbuf *wb;

    EnterCriticalSection( &wb_lock );
    if ((wb = wb_find( handle )))
    {
        wb_flush( wb );
        if (wb->writes >= 64 && wb_logged++ < 40)
            log_line( "saved %s: %u writes, %u seeks, %u KB, %u card writes, %u ms", wb->name, wb->writes,
                      wb->seeks, (unsigned int)(wb->bytes / 1024), wb->flushes,
                      (unsigned int)(GetTickCount() - wb->opened) );
        if (wb->data) VirtualFree( wb->data, 0, MEM_RELEASE );
        memset( wb, 0, sizeof(*wb) );
    }
    LeaveCriticalSection( &wb_lock );
    return real_CloseHandle( handle );
}

static BOOL WINAPI hook_DeleteFileA( LPCSTR name )
{
    wb_flush_all();
    return real_DeleteFileA( name );
}

static BOOL WINAPI hook_CopyFileA( LPCSTR from, LPCSTR to, BOOL fail_if_exists )
{
    wb_flush_all();
    return real_CopyFileA( from, to, fail_if_exists );
}

static HANDLE WINAPI hook_FindFirstFileA( LPCSTR pattern, LPWIN32_FIND_DATAA data )
{
    wb_flush_all();
    return real_FindFirstFileA( pattern, data );
}

/* A module's file imports, all or none: a handle must never reach an unhooked
 * import while it has data waiting. Returns FALSE when it has no WriteFile. */
static BOOL hook_file_imports( HMODULE module )
{
    static const struct { const char *name; void *hook; void **real; } hooks[] =
    {
        { "CreateFileA",      hook_CreateFileA,      (void **)&real_CreateFileA },
        { "ReadFile",         hook_ReadFile,         (void **)&real_ReadFile },
        { "SetFilePointerEx", hook_SetFilePointerEx, (void **)&real_SetFilePointerEx },
        { "GetFileSizeEx",    hook_GetFileSizeEx,    (void **)&real_GetFileSizeEx },
        { "GetFileTime",      hook_GetFileTime,      (void **)&real_GetFileTime },
        { "CloseHandle",      hook_CloseHandle,      (void **)&real_CloseHandle },
        { "DeleteFileA",      hook_DeleteFileA,      (void **)&real_DeleteFileA },
        { "CopyFileA",        hook_CopyFileA,        (void **)&real_CopyFileA },
        { "FindFirstFileA",   hook_FindFirstFileA,   (void **)&real_FindFirstFileA },
    };
    HMODULE kernel32 = GetModuleHandleA( "kernel32.dll" );
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(hooks); i++)
        if (!*hooks[i].real) *hooks[i].real = (void *)GetProcAddress( kernel32, hooks[i].name );
    if (!real_WriteFile) real_WriteFile = (void *)GetProcAddress( kernel32, "WriteFile" );
    for (i = 0; i < ARRAY_SIZE(hooks); i++) if (!*hooks[i].real) return FALSE;
    if (!real_WriteFile) return FALSE;

    for (i = 0; i < ARRAY_SIZE(hooks); i++)
    {
        void *orig;
        hook_import( module, "KERNEL32.dll", hooks[i].name, hooks[i].hook, &orig );
    }
    {
        void *orig;
        return hook_import( module, "KERNEL32.dll", "WriteFile", hook_WriteFile, &orig );
    }
}

/* Condemned.exe at once; the DLLs as they appear (GameServer.dll with the first level). */
static void watch_file_modules( void )
{
    static const char *const names[] = { NULL, "EngineServer.dll", "GameServer.dll", "GameClient.dll", "GameDatabase.dll" };
    static unsigned int done;
    unsigned int i;

    if (done == (1u << ARRAY_SIZE(names)) - 1) return;
    for (i = 0; i < ARRAY_SIZE(names); i++)
    {
        HMODULE module;

        if (done & (1u << i)) continue;
        if (!(module = GetModuleHandleA( names[i] ))) continue;
        done |= 1u << i;
        log_line( "%s: writes %s", names[i] ? names[i] : "Condemned.exe",
                  hook_file_imports( module ) ? "buffered" : "left as they are (no WriteFile import)" );
    }
}

/* ---- export ---- */

static HMODULE self;

/* Exported through dinput8.def. */
HRESULT WINAPI DirectInput8Create( HINSTANCE instance, DWORD version, REFIID iid, void **out, IUnknown *outer )
{
    static HRESULT (WINAPI *real_create)( HINSTANCE, DWORD, REFIID, void **, IUnknown * );
    HRESULT hr;

    if (!real_create)
    {
        char path[MAX_PATH];
        UINT len = GetSystemDirectoryA( path, MAX_PATH - 16 );
        HMODULE real;

        memcpy( path + len, "\\dinput8.dll", sizeof("\\dinput8.dll") );
        real = LoadLibraryA( path );
        if (!real || real == self)
        {
            log_line( "could not load %s (error %u)", path, (unsigned int)GetLastError() );
            return DIERR_GENERIC;
        }
        real_create = (void *)GetProcAddress( real, "DirectInput8Create" );
        log_line( "build 14: system DirectInput from %s", path );
        if (!real_create) return DIERR_GENERIC;
        if (hook_import( GetModuleHandleA( NULL ), "user32.dll", "SetCursorPos",
                         hook_SetCursorPos, (void **)&real_SetCursorPos ))
            log_line( "SetCursorPos of the game hooked" );
        else
            log_line( "SetCursorPos of the game not found in its imports" );
        if (!hook_import( GetModuleHandleA( NULL ), "user32.dll", "ShowCursor",
                          hook_ShowCursor, (void **)&real_ShowCursor ))
            log_line( "ShowCursor of the game not found in its imports" );
        if (hook_import( GetModuleHandleA( NULL ), "kernel32.dll", "LoadLibraryA",
                         hook_LoadLibraryA, (void **)&real_LoadLibraryA ))
            log_line( "LoadLibraryA of the game hooked (for SndDrv.dll)" );
        if (hook_import( GetModuleHandleA( NULL ), "kernel32.dll", "OutputDebugStringA",
                         hook_OutputDebugStringA, (void **)&real_OutputDebugStringA ))
            log_line( "OutputDebugStringA of the game hooked: engine messages follow as \"engine:\" lines" );
        else
            log_line( "LoadLibraryA of the game not found in its imports: sound stays as it is" );
        if (GetModuleHandleA( "SndDrv.dll" )) log_line( "SndDrv.dll was loaded before DirectInput" );
    }

    hr = real_create( instance, version, iid, out, outer );
    log_line( "DirectInput8Create(version %x, iid %x) -> %x", (unsigned int)version,
              iid ? (unsigned int)iid->Data1 : 0, (unsigned int)hr );
    if (SUCCEEDED(hr) && out && *out)
    {
        IDirectInput8A *dinput = *out;
        if (!dinput_vtbl_ready)
        {
            orig_dinput = *dinput->lpVtbl;
            dinput_vtbl = orig_dinput;
            dinput_vtbl.CreateDevice = dinput_CreateDevice;
            dinput_vtbl_ready = TRUE;
        }
        dinput->lpVtbl = &dinput_vtbl;
    }
    return hr;
}

/* ---- DXVK's shader cache ----
 * DXVK keeps the shaders it has translated in DXVK_SHADER_CACHE_PATH, or else in
 * %LOCALAPPDATA%\dxvk (dxvk_shader_cache.cpp, getDefaultFilePaths). Wine-NX starts
 * programs with a fixed environment that has neither (runtime.c, runtime_environment),
 * so DXVK logs "No path found for shader cache" and translates every shader again on
 * each start. The proxy is loaded before the game creates its Direct3D device, so it
 * points the variable at dxvk-cache next to the game; DXVK creates the folder. */
static void set_shader_cache_path( void )
{
    char path[MAX_PATH];
    char *slash = NULL, *p;
    DWORD len;

    if (GetEnvironmentVariableA( "DXVK_SHADER_CACHE_PATH", NULL, 0 )) return;
    len = GetModuleFileNameA( NULL, path, MAX_PATH );
    if (!len || len >= MAX_PATH - sizeof("dxvk-cache")) return;
    for (p = path; *p; p++) if (*p == '\\' || *p == '/') slash = p;
    if (!slash) return;
    memcpy( slash + 1, "dxvk-cache", sizeof("dxvk-cache") );
    if (SetEnvironmentVariableA( "DXVK_SHADER_CACHE_PATH", path ))
        log_line( "DXVK shader cache: %s", path );
}

BOOL WINAPI DllMainCRTStartup( HINSTANCE instance, DWORD reason, void *reserved )
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        self = instance;
        DisableThreadLibraryCalls( instance );
        InitializeCriticalSection( &wb_lock );
        set_shader_cache_path();
    }
    return TRUE;
}
