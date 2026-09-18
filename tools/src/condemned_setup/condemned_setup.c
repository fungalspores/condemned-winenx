/* Condemned: Criminal Origins setup for Wine-NX: everything the game folder and the
 * card's registry need, done once on the console from one program. Modelled on
 * Wine-NX's own war3_setup.c (wine-nx-probe/tools).
 *
 * - Condemned.exe: the two header fields Wine-NX build 108 refuses (pe_fix.h),
 *   fixed in place, so no computer is needed. Already fixed: left as it is.
 * - Files an installation of the game may carry that break it on Wine-NX are
 *   renamed to NAME.off: d3d9.dll (the widescreen fix's ASI loader, which would
 *   take Direct3D 9 from DXVK) and XInputPlus (dinput.dll, xinput1_3.dll,
 *   XInputPlus.ini: once a program polls XInput, Wine-NX stops sending keys and
 *   mouse from the pad).
 * - dinput8.dll must be the pack's proxy, not the game's 2018 fix: checked.
 * - DirectSound: dsound.dll registered as regsvr32 would, since Wine's first-run
 *   setup does not run on the Switch. The game's EAX.DLL creates its sound device
 *   through CoCreateInstance(CLSID_DirectSound8); unregistered, that fails,
 *   SndDrv.dll shows "Failed the EAXDirectSoundCreate8 function" in a message box,
 *   and the box stays hidden behind the game's Vulkan surface: a black screen that
 *   waits forever. Then the same CoCreateInstance the game makes, as a check.
 *
 * Each step is reported to wine-nx-runtime.log as a [CONDEMNED SETUP] line, and the
 * outcome in a message box at the end; the exit code is 0 when every step worked.
 * Running it again is harmless. */
#define COBJMACROS
#include <windows.h>
#include <winternl.h>
#include <ole2.h>

#include "pe_fix.h"

__declspec(dllimport) NTSTATUS NTAPI NtDisplayString( const UNICODE_STRING *str );

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

static const GUID clsid_directsound8 =
    { 0x3901cc3f, 0x84b5, 0x4fa4, { 0xba, 0x35, 0xaa, 0x81, 0x72, 0xb8, 0xa0, 0x9b } };
static const GUID iid_idirectsound8 =
    { 0xc50a7e93, 0xf395, 0x4834, { 0x9e, 0xf6, 0x7f, 0xa9, 0x9d, 0xe5, 0x09, 0x66 } };

/* One line: "[CONDEMNED SETUP] <label> <name>: <result>", with the value in hex. The
 * runtime ends each NtDisplayString line itself; a '\n' of ours showed as "?". */
static void report( const char *label, const WCHAR *name, const char *result, DWORD value )
{
    static const char hex[] = "0123456789abcdef";
    const char *prefix = "[CONDEMNED SETUP] ";
    WCHAR buffer[400];
    UNICODE_STRING str;
    unsigned int n = 0, i;

    while (*prefix) buffer[n++] = *prefix++;
    while (*label && n < 100) buffer[n++] = *label++;
    if (name)
    {
        buffer[n++] = ' ';
        while (*name && n < 300) buffer[n++] = *name++;
    }
    buffer[n++] = ':';
    buffer[n++] = ' ';
    while (*result && n < 380) buffer[n++] = *result++;
    buffer[n++] = ' ';
    buffer[n++] = '0';
    buffer[n++] = 'x';
    for (i = 0; i < 8; i++) buffer[n++] = hex[(value >> (28 - i * 4)) & 15];
    str.Buffer = buffer;
    str.Length = n * sizeof(WCHAR);
    str.MaximumLength = str.Length;
    NtDisplayString( &str );
}

/* ---- the game folder: this program's own folder ---- */

static WCHAR game_dir[MAX_PATH];
static unsigned int game_dir_len;

static void find_game_dir( void )
{
    unsigned int i;

    game_dir_len = GetModuleFileNameW( NULL, game_dir, MAX_PATH - 32 );
    for (i = game_dir_len; i; i--) if (game_dir[i - 1] == '\\' || game_dir[i - 1] == '/') break;
    game_dir_len = i;
    game_dir[i] = 0;
}

/* path = game folder + name (+ suffix) */
static const WCHAR *game_file( WCHAR *path, const WCHAR *name, const WCHAR *suffix )
{
    unsigned int n = 0;

    while (n < game_dir_len) { path[n] = game_dir[n]; n++; }
    while (*name && n < MAX_PATH - 8) path[n++] = *name++;
    while (suffix && *suffix && n < MAX_PATH - 1) path[n++] = *suffix++;
    path[n] = 0;
    return path;
}

/* ---- the steps ---- */

enum exe_state { EXE_FIXED_NOW, EXE_ALREADY_FIXED, EXE_MISSING, EXE_NOT_PE, EXE_WRITE_FAILED };

static enum exe_state fix_exe( void )
{
    WCHAR path[MAX_PATH];
    static unsigned char headers[0x1000];
    DWORD got = 0, put = 0;
    HANDLE file;
    int changes;

    file = CreateFileW( game_file( path, L"Condemned.exe", NULL ), GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
    if (file == INVALID_HANDLE_VALUE)
    {
        report( "open", path, "failed, error", GetLastError() );
        return EXE_MISSING;
    }
    if (!ReadFile( file, headers, sizeof(headers), &got, NULL ) || (changes = pe_fix_headers( headers, got )) < 0)
    {
        report( "headers of", path, "not a 32-bit program, size read", got );
        CloseHandle( file );
        return EXE_NOT_PE;
    }
    if (!changes)
    {
        report( "headers of", path, "already fixed, changes", 0 );
        CloseHandle( file );
        return EXE_ALREADY_FIXED;
    }
    if (SetFilePointer( file, 0, NULL, FILE_BEGIN ) || !WriteFile( file, headers, got, &put, NULL ) || put != got)
    {
        report( "write", path, "failed, error", GetLastError() );
        CloseHandle( file );
        return EXE_WRITE_FAILED;
    }
    CloseHandle( file );
    report( "headers of", path, "fixed, changes", changes );
    return EXE_FIXED_NOW;
}

/* NAME -> NAME.off; returns how many files it renamed. */
static unsigned int disable_conflicts( void )
{
    static const WCHAR *const names[] = { L"d3d9.dll", L"dinput.dll", L"xinput1_3.dll", L"XInputPlus.ini" };
    WCHAR path[MAX_PATH], off[MAX_PATH];
    unsigned int i, renamed = 0;

    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    {
        if (GetFileAttributesW( game_file( path, names[i], NULL ) ) == INVALID_FILE_ATTRIBUTES) continue;
        if (MoveFileExW( path, game_file( off, names[i], L".off" ), MOVEFILE_REPLACE_EXISTING ))
        {
            report( "renamed to NAME.off", path, "ok", 0 );
            renamed++;
        }
        else report( "rename", path, "failed, error", GetLastError() );
    }
    return renamed;
}

/* The pack's dinput8.dll logs "[DINPUT8 PROXY]" lines; the game's own does not. */
static BOOL check_proxy( void )
{
    static const char marker[] = "[DINPUT8 PROXY]";
    static unsigned char data[0x40000];
    WCHAR path[MAX_PATH];
    DWORD got = 0, i, j;
    HANDLE file;

    file = CreateFileW( game_file( path, L"dinput8.dll", NULL ), GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, 0, NULL );
    if (file == INVALID_HANDLE_VALUE)
    {
        report( "open", path, "failed, error", GetLastError() );
        return FALSE;
    }
    ReadFile( file, data, sizeof(data), &got, NULL );
    CloseHandle( file );
    for (i = 0; i + sizeof(marker) - 1 <= got; i++)
    {
        for (j = 0; j < sizeof(marker) - 1 && data[i + j] == (unsigned char)marker[j]; j++) ;
        if (j == sizeof(marker) - 1)
        {
            report( "check", path, "the pack's proxy, size", got );
            return TRUE;
        }
    }
    report( "check", path, "NOT the pack's proxy (copy the pack over the game), size", got );
    return FALSE;
}

/* What regsvr32 does for a DLL: load it and call its DllRegisterServer. */
static BOOL register_dll( const WCHAR *name )
{
    HRESULT (WINAPI *register_server)(void);
    HMODULE module;
    HRESULT hr;

    if (!(module = LoadLibraryW( name )))
    {
        report( "load", name, "failed, error", GetLastError() );
        return FALSE;
    }
    if (!(register_server = (void *)GetProcAddress( module, "DllRegisterServer" )))
    {
        report( "register", name, "has no DllRegisterServer, error", GetLastError() );
        FreeLibrary( module );
        return FALSE;
    }
    hr = register_server();
    report( "register", name, SUCCEEDED(hr) ? "ok, hr" : "failed, hr", (DWORD)hr );
    FreeLibrary( module );
    return SUCCEEDED(hr);
}

/* The call EAX.DLL's EAXDirectSoundCreate8 makes. */
static BOOL check_directsound8( void )
{
    IUnknown *sound = NULL;
    HRESULT hr = CoCreateInstance( &clsid_directsound8, NULL, CLSCTX_INPROC_SERVER,
                                   &iid_idirectsound8, (void **)&sound );

    report( "CoCreateInstance", L"CLSID_DirectSound8", SUCCEEDED(hr) ? "ok, hr" : "failed, hr", (DWORD)hr );
    if (sound) IUnknown_Release( sound );
    return SUCCEEDED(hr);
}

/* ---- the message at the end ---- */

static WCHAR message[1024];
static unsigned int message_len;

static void say( const WCHAR *text )
{
    while (*text && message_len < sizeof(message) / sizeof(message[0]) - 1) message[message_len++] = *text++;
    message[message_len] = 0;
}

void __stdcall start(void)
{
    static const WCHAR *const exe_lines[] =
    {
        L"Condemned.exe: fixed / исправлен\n",
        L"Condemned.exe: OK\n",
        L"Condemned.exe: NOT FOUND, copy the game into this folder / НЕ НАЙДЕН, скопируйте игру в эту папку\n",
        L"Condemned.exe: not a 32-bit program / не 32-битная программа\n",
        L"Condemned.exe: could not be written / не удалось записать\n",
    };
    enum exe_state exe;
    BOOL ok, proxy, sound;
    unsigned int renamed;
    HRESULT hr;

    report( "start", NULL, "build", 2 );
    find_game_dir();

    exe = fix_exe();
    renamed = disable_conflicts();
    proxy = check_proxy();

    hr = OleInitialize( NULL );
    report( "OleInitialize", NULL, SUCCEEDED(hr) ? "ok, hr" : "failed, hr", (DWORD)hr );
    sound = register_dll( L"dsound.dll" );
    sound &= check_directsound8();
    if (SUCCEEDED(hr)) OleUninitialize();

    ok = exe <= EXE_ALREADY_FIXED && proxy && sound;
    report( ok ? "done, all steps worked" : "done, a step FAILED (see above)", NULL, "exit code", !ok );

    say( ok ? L"Setup done, start Condemned.\nНастройка завершена, запускайте Condemned.\n\n"
            : L"Setup found problems:\nНастройка нашла проблемы:\n\n" );
    say( exe_lines[exe] );
    if (renamed) say( L"d3d9.dll / XInputPlus: renamed to .off / переименованы в .off\n" );
    say( proxy ? L"dinput8.dll: OK\n"
               : L"dinput8.dll: not the pack's, copy the pack over the game / не из пака, скопируйте пак поверх игры\n" );
    say( sound ? L"DirectSound: OK\n" : L"DirectSound: FAILED / ОШИБКА (wine-nx-runtime.log)\n" );
    MessageBoxW( NULL, message, L"Condemned: Setup", ok ? MB_OK | MB_ICONINFORMATION : MB_OK | MB_ICONWARNING );
    ExitProcess( !ok );
}
