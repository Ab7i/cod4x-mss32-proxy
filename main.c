/*
 * cod4x-mss32-proxy — minimal mss32.dll replacement for the
 * D:\Cod4Project\game-test sandbox.
 *
 * The original CB-Servers mss32.dll proxy locates launcher.dll via
 * SHGetKnownFolderPath(FOLDERID_LocalAppData), which the user's
 * LOCALAPPDATA env var cannot override. This proxy uses an absolute
 * path computed from our own DLL location instead.
 *
 * Chain on DLL_PROCESS_ATTACH:
 *   1. Resolve our own path  -> D:\Cod4Project\game-test\mss32.dll
 *   2. Strip filename        -> D:\Cod4Project\game-test
 *   3. Set LOCALAPPDATA env  -> <game-test>\fake-appdata
 *      (launcher.dll reads this env var directly; verified by strings)
 *   4. LoadLibraryW          -> <fake-appdata>\CallofDuty4MW\bin\launcher.dll
 *   5. Call exported StartLauncher(hinstance, NULL, NULL, 0)
 *      (Rust extern "C" = cdecl; see the call site for the signature).
 *
 * All Miles Sound API calls (340 functions) are pure PE export
 * forwarders to miles32.dll, declared in exports.def. No runtime
 * cost; resolved by the Windows loader.
 *
 * On any failure: a MessageBox surfaces the diagnostic, DllMain
 * returns TRUE, and iw3mp.exe continues without CoD4x patches.
 * Easier to diagnose than a silent crash.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <strsafe.h>

static void show_error(const wchar_t *what)
{
    wchar_t buf[1024];
    DWORD err = GetLastError();
    StringCchPrintfW(buf, ARRAYSIZE(buf),
        L"cod4x-mss32-proxy: %s\n(GetLastError = %lu)", what, err);
    MessageBoxW(NULL, buf, L"cod4x-mss32-proxy", MB_OK | MB_ICONERROR);
}

static void load_chain(HMODULE hSelf)
{
    /* Our own DLL path (e.g. D:\Cod4Project\game-test\mss32.dll) */
    wchar_t self_path[1024];
    DWORD n = GetModuleFileNameW(hSelf, self_path, ARRAYSIZE(self_path));
    if (n == 0 || n >= ARRAYSIZE(self_path)) {
        show_error(L"GetModuleFileNameW failed or truncated");
        return;
    }

    /* Strip filename -> directory */
    wchar_t *slash = wcsrchr(self_path, L'\\');
    if (slash == NULL) {
        show_error(L"unexpected self path (no backslash)");
        return;
    }
    *slash = L'\0';

    /* fake_appdata = <game-test>\fake-appdata */
    wchar_t fake_appdata[1024];
    if (FAILED(StringCchCopyW(fake_appdata, ARRAYSIZE(fake_appdata), self_path)) ||
        FAILED(StringCchCatW (fake_appdata, ARRAYSIZE(fake_appdata), L"\\fake-appdata"))) {
        show_error(L"path build failed (fake_appdata)");
        return;
    }

    /* Redirect LOCALAPPDATA for the Rust launcher's own path search */
    if (!SetEnvironmentVariableW(L"LOCALAPPDATA", fake_appdata)) {
        show_error(L"SetEnvironmentVariableW(LOCALAPPDATA) failed");
        return;
    }

    /* launcher_path = <fake_appdata>\CallofDuty4MW\bin\launcher.dll */
    wchar_t launcher_path[1024];
    if (FAILED(StringCchCopyW(launcher_path, ARRAYSIZE(launcher_path), fake_appdata)) ||
        FAILED(StringCchCatW (launcher_path, ARRAYSIZE(launcher_path),
                              L"\\CallofDuty4MW\\bin\\launcher.dll"))) {
        show_error(L"path build failed (launcher_path)");
        return;
    }

    /* Absolute-path load bypasses Known Folders entirely */
    HMODULE h_launcher = LoadLibraryW(launcher_path);
    if (h_launcher == NULL) {
        show_error(L"LoadLibraryW(launcher.dll) failed");
        return;
    }

    FARPROC p_start = GetProcAddress(h_launcher, "StartLauncher");
    if (p_start == NULL) {
        show_error(L"GetProcAddress(StartLauncher) failed");
        return;
    }

    /* StartLauncher's REAL signature (from CoD4x-launcher entrypoint.rs):
         extern "C" fn StartLauncher(
             hinstance:        HINSTANCE,
             mss32importprocs: *mut *mut c_void,
             mss32importnames: *const *const c_char,
             mss32importcount: i32)
       Rust extern "C" on i686-windows = cdecl, matching our C default.

       Earlier this proxy called it with 0 arguments — the launcher then
       read 4 garbage values off the stack, which broke its miles32 setup
       and made it fall through to vanilla CoD4. We now pass the correct
       four arguments:
         - hinstance     = the EXE module handle (GetModuleHandleW(NULL))
         - importprocs   = NULL  (no import-table slots to fill)
         - importnames   = NULL
         - importcount   = 0     (launcher skips the import-patch loop)
       count = 0 means the launcher's miles32::load_module just does a
       plain LoadLibrary("miles32.dll") and never dereferences the NULL
       arrays. The Miles exports are still resolved because our mss32.dll
       forwards every one of them to miles32.dll at the PE level. */
    typedef void (*start_launcher_fn)(HINSTANCE hinst,
                                      void **importprocs,
                                      const char **importnames,
                                      int importcount);
    HINSTANCE exe_instance = GetModuleHandleW(NULL);
    ((start_launcher_fn)p_start)(exe_instance, NULL, NULL, 0);
}

/* Thread wrapper: DllMain cannot call load_chain directly because
   launcher.dll's StartLauncher does work that needs the loader lock
   (LoadLibrary / thread creation inside Rust runtime). Doing that
   while we still hold the lock from our own DllMain causes a deadlock.
   Spawning a worker thread lets DllMain return immediately, the loader
   lock is released, and the worker can then safely call LoadLibraryW.
   The worker races iw3mp's CRT init and WinMain — in practice it wins
   because CRT init takes longer than launcher.dll's hook setup. The
   original CB-Servers proxy uses the same pattern (imports _beginthreadex). */
static DWORD WINAPI chain_thread(LPVOID param)
{
    load_chain((HMODULE)param);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hSelf, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hSelf);
        HANDLE h = CreateThread(NULL, 0, chain_thread, (LPVOID)hSelf, 0, NULL);
        if (h != NULL) {
            CloseHandle(h);
        } else {
            show_error(L"CreateThread for chain_thread failed");
        }
    }
    return TRUE;
}
