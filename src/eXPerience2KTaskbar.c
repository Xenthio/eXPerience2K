/* Optional XP-only taskbar experiment. Build once as a controller EXE and once
   with TASKBAR_DLL as a same-architecture, thread-scoped Explorer hook DLL.
   Never subclass a window from another process/thread. No shell binaries are
   patched. The hook is pinned in Explorer so callbacks remain valid if the
   controller crashes; a timer restores the shell when its owner exits. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <stdio.h>
#include <string.h>

#define CONTROL_MESSAGE "eXPerience2K.Taskbar98.Control.v1"
#define ACTIVE_PROPERTY "eXPerience2K.Taskbar98.Active.v1"
#define CONFIG_KEY "Software\\eXPerience2K\\Config"
#define SUBCLASS_ID 0xE298

static int supported_os(void)
{
    OSVERSIONINFOEXA version;
    ZeroMemory(&version, sizeof(version));
    version.dwOSVersionInfoSize = sizeof(version);
    return GetVersionExA((OSVERSIONINFOA *)&version) &&
        version.dwPlatformId == VER_PLATFORM_WIN32_NT &&
        version.dwMajorVersion == 5 &&
        (version.dwMinorVersion == 1 || version.dwMinorVersion == 2) &&
        version.wProductType == VER_NT_WORKSTATION;
}

#ifdef TASKBAR_DLL
typedef struct { UINT id; UINT edge; } BAND_EDGE;
static HWND tray, start_button, rebar, task_list, notification, start_divider;
static LONG rebar_border, notification_edge;
static BAND_EDGE bands[64];
static UINT band_count;
static int start_width, extra_width, content_shift, active, updating;
static HANDLE owner;
static UINT_PTR timer_id;
static HMODULE module_instance;

static BOOL themed(void)
{
#ifdef TASKBAR_TEST
    return FALSE; /* Isolated synthetic controls, never the host taskbar. */
#else
    return IsThemeActive() && IsAppThemed();
#endif
}

static BOOL horizontal(void)
{
    RECT r;
    return GetWindowRect(tray, &r) && r.right - r.left > r.bottom - r.top;
}

static void paint_divider(HWND hwnd, HDC dc)
{
    RECT r, column;
    GetClientRect(hwnd, &r);
    column = r; column.right = 1;
    FillRect(dc, &column, GetSysColorBrush(COLOR_BTNSHADOW));
    column = r; column.left = 1;
    FillRect(dc, &column, GetSysColorBrush(COLOR_BTNHIGHLIGHT));
}

static LRESULT CALLBACK divider_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                     UINT_PTR id, DWORD_PTR data)
{
    (void)id; (void)data;
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        paint_divider(hwnd, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_PRINTCLIENT) { paint_divider(hwnd, (HDC)wp); return 0; }
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, divider_proc, SUBCLASS_ID);
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static void position_divider(void)
{
    RECT r;
    if (!active || !IsWindow(start_button)) return;
    if (!IsWindow(start_divider)) {
        start_divider = CreateWindowExA(0, "STATIC", "",
            WS_CHILD | SS_NOTIFY, 0, 0, 2, 22, tray, NULL, module_instance, NULL);
        if (start_divider && !SetWindowSubclass(start_divider, divider_proc, SUBCLASS_ID, 0)) {
            DestroyWindow(start_divider); start_divider = NULL;
        }
    }
    if (!start_divider) return;
    GetWindowRect(start_button, &r);
    MapWindowPoints(NULL, tray, (POINT *)&r, 2);
    SetWindowPos(start_divider, HWND_TOP, r.right + 2, r.top, 2, r.bottom - r.top,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static void relayout(void)
{
    RECT r;
    if (!IsWindow(tray)) return;
    GetClientRect(tray, &r);
    SendMessage(tray, WM_SIZE, SIZE_RESTORED, MAKELPARAM(r.right, r.bottom));
    RedrawWindow(tray, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
}

/* Keep Explorer's own flag, font, bevel and pressed-state rendering. Move only
   the rendered interior; the frame stays at its native coordinates. */
static void shift_start_contents(HWND hwnd, HDC dc)
{
    RECT inside;
    HDC memory;
    HBITMAP bitmap, previous;
    int w, h;
    GetClientRect(hwnd, &inside);
    InflateRect(&inside, -2, -2);
    w = inside.right - inside.left; h = inside.bottom - inside.top;
    if (w <= content_shift || h <= 0) return;
    memory = CreateCompatibleDC(dc);
    bitmap = CreateCompatibleBitmap(dc, w, h);
    if (!memory || !bitmap) {
        if (memory) DeleteDC(memory);
        if (bitmap) DeleteObject(bitmap);
        return;
    }
    previous = (HBITMAP)SelectObject(memory, bitmap);
    if (BitBlt(memory, 0, 0, w, h, dc, inside.left, inside.top, SRCCOPY)) {
        FillRect(dc, &inside, GetSysColorBrush(COLOR_BTNFACE));
        BitBlt(dc, inside.left + content_shift, inside.top,
               w - content_shift, h, memory, 0, 0, SRCCOPY);
    }
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
}

static LRESULT CALLBACK geometry_proc(HWND hwnd, UINT msg, WPARAM wp,
                                     LPARAM lp, UINT_PTR id, DWORD_PTR data)
{
    LRESULT result;
    (void)id; (void)data;
    if (active && hwnd == start_button && (msg == WM_PAINT || msg == WM_PRINTCLIENT)) {
        PAINTSTRUCT ps;
        RECT r;
        HDC dc, buffer;
        HBITMAP bitmap, old;
        int owns_paint = msg == WM_PAINT && !wp;
        if (owns_paint) InvalidateRect(hwnd, NULL, FALSE);
        dc = owns_paint ? BeginPaint(hwnd, &ps) : (HDC)wp;
        if (!dc) return 0;
        GetClientRect(hwnd, &r);
        buffer = CreateCompatibleDC(dc);
        bitmap = CreateCompatibleBitmap(dc, r.right, r.bottom);
        if (buffer && bitmap) {
            old = (HBITMAP)SelectObject(buffer, bitmap);
            FillRect(buffer, &r, GetSysColorBrush(COLOR_BTNFACE));
            /* Render native content offscreen and present only the final offset
               image. Never expose the unshifted intermediate frame. */
            DefSubclassProc(hwnd, WM_PRINTCLIENT, (WPARAM)buffer, PRF_CLIENT | PRF_ERASEBKGND);
            shift_start_contents(hwnd, buffer);
            BitBlt(dc, 0, 0, r.right, r.bottom, buffer, 0, 0, SRCCOPY);
            SelectObject(buffer, old);
        } else {
            DefSubclassProc(hwnd, WM_PRINTCLIENT, (WPARAM)dc, PRF_CLIENT | PRF_ERASEBKGND);
            shift_start_contents(hwnd, dc);
        }
        if (bitmap) DeleteObject(bitmap);
        if (buffer) DeleteDC(buffer);
        if (owns_paint) EndPaint(hwnd, &ps);
        return 0;
    }
    if (active && hwnd == start_button && msg == BM_SETSTATE) {
        /* Native BUTTON paints immediately on a pressed-state transition.
           Suppress that direct drawing, then synchronously present our buffer.
           Do not suppress WM_LBUTTONDOWN: Explorer can run a menu loop there. */
        DefSubclassProc(hwnd, WM_SETREDRAW, FALSE, 0);
        result = DefSubclassProc(hwnd, msg, wp, lp);
        DefSubclassProc(hwnd, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
        return result;
    }
    /* Let the native control calculate its position before constraining it. */
    result = DefSubclassProc(hwnd, msg, wp, lp);
    if (active && horizontal() && msg == WM_WINDOWPOSCHANGING) {
        WINDOWPOS *pos = (WINDOWPOS *)lp;
        if (hwnd == start_button && !(pos->flags & SWP_NOSIZE)) {
            if (pos->cx < start_width + extra_width)
                pos->cx = start_width + extra_width;
        } else if (hwnd == rebar && !(pos->flags & SWP_NOMOVE)) {
            RECT r;
            int boundary, delta;
            GetWindowRect(start_button, &r);
            MapWindowPoints(NULL, tray, (POINT *)&r, 2);
            boundary = r.left + start_width + extra_width + 4;
            delta = boundary - pos->x;
            if (delta > 0 && !(pos->flags & SWP_NOSIZE) && pos->cx > delta) {
                pos->x += delta;
                pos->cx -= delta;
            }
        }
    }
    if (active && hwnd == start_button && (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_KEYDOWN || msg == WM_KEYUP))
        RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
    if (hwnd != tray && msg == WM_WINDOWPOSCHANGED) position_divider();
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, geometry_proc, SUBCLASS_ID);
    return result;
}

static void set_edge(HWND hwnd, LONG value)
{
    if (!IsWindow(hwnd)) return;
    SetWindowLong(hwnd, GWL_EXSTYLE,
        (GetWindowLong(hwnd, GWL_EXSTYLE) & ~WS_EX_STATICEDGE) | value);
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

static void update_bands(int restore)
{
    UINT i, j, count;
    if (!IsWindow(rebar)) return;
    count = (UINT)SendMessage(rebar, RB_GETBANDCOUNT, 0, 0);
    for (i = 0; i < count; ++i) {
        REBARBANDINFOA info;
        ZeroMemory(&info, sizeof(info));
        info.cbSize = REBARBANDINFO_V3_SIZE;
        info.fMask = RBBIM_ID | RBBIM_STYLE | RBBIM_CHILD;
        if (!SendMessageA(rebar, RB_GETBANDINFOA, i, (LPARAM)&info)) continue;
        for (j = 0; j < band_count && bands[j].id != info.wID; ++j) {}
        if (j == band_count) {
            if (restore || band_count == 64) continue;
            bands[j].id = info.wID;
            bands[j].edge = info.fStyle & RBBS_CHILDEDGE;
            ++band_count;
        }
        if ((info.fStyle & RBBS_CHILDEDGE) == (restore ? bands[j].edge : RBBS_CHILDEDGE)) continue;
        /* Keep the running-window band flat, including its child edges. */
        if (!restore && info.hwndChild == task_list) continue;
        info.fMask = RBBIM_STYLE;
        info.fStyle = (info.fStyle & ~RBBS_CHILDEDGE) | (restore ? bands[j].edge : RBBS_CHILDEDGE);
        SendMessageA(rebar, RB_SETBANDINFOA, i, (LPARAM)&info);
    }
}

static void restore_geometry(void)
{
    if (!active) return;
    active = 0;
    RemoveWindowSubclass(tray, geometry_proc, SUBCLASS_ID);
    RemoveWindowSubclass(start_button, geometry_proc, SUBCLASS_ID);
    RemoveWindowSubclass(rebar, geometry_proc, SUBCLASS_ID);
    update_bands(1);
    if (IsWindow(rebar)) SetWindowLong(rebar, GWL_STYLE,
        (GetWindowLong(rebar, GWL_STYLE) & ~RBS_BANDBORDERS) | rebar_border);
    if (IsWindow(start_divider)) DestroyWindow(start_divider);
    start_divider = NULL;
    set_edge(notification, notification_edge);
    relayout();
    band_count = 0;
}

static void detach(void)
{
    restore_geometry();
    if (timer_id) KillTimer(NULL, timer_id);
    timer_id = 0;
    if (owner) CloseHandle(owner);
    owner = NULL;
    if (IsWindow(tray)) RemovePropA(tray, ACTIVE_PROPERTY);
    tray = NULL;
}

static void refresh_geometry(void)
{
    RECT r;
    HDC dc;
    if (updating) return;
    updating = 1;
    /* Leave Luna and vertical taskbars native; resume after returning to Classic. */
    if (active && (!IsWindow(start_button) || !IsWindow(rebar))) restore_geometry();
    if (!horizontal() || themed()) {
        restore_geometry();
        updating = 0;
        return;
    }
    if (!active) {
        start_button = FindWindowExA(tray, NULL, "Button", NULL);
        rebar = FindWindowExA(tray, NULL, REBARCLASSNAMEA, NULL);
        notification = FindWindowExA(tray, NULL, "TrayNotifyWnd", NULL);
        task_list = FindWindowExA(rebar, NULL, "MSTaskSwWClass", NULL);
        if (!start_button || !rebar) { updating = 0; return; }
        GetWindowRect(start_button, &r);
        start_width = r.right - r.left;
        dc = GetDC(tray);
        extra_width = dc ? MulDiv(3, GetDeviceCaps(dc, LOGPIXELSX), 96) : 3;
        content_shift = dc ? MulDiv(2, GetDeviceCaps(dc, LOGPIXELSX), 96) : 2;
        if (dc) ReleaseDC(tray, dc);
        rebar_border = GetWindowLong(rebar, GWL_STYLE) & RBS_BANDBORDERS;
        notification_edge = GetWindowLong(notification, GWL_EXSTYLE) & WS_EX_STATICEDGE;
        if (!SetWindowSubclass(start_button, geometry_proc, SUBCLASS_ID, 0) ||
            !SetWindowSubclass(rebar, geometry_proc, SUBCLASS_ID, 0) ||
            !SetWindowSubclass(tray, geometry_proc, SUBCLASS_ID, 0)) {
            RemoveWindowSubclass(tray, geometry_proc, SUBCLASS_ID);
            RemoveWindowSubclass(start_button, geometry_proc, SUBCLASS_ID);
            RemoveWindowSubclass(rebar, geometry_proc, SUBCLASS_ID);
            updating = 0; return;
        }
        active = 1;
        SetWindowLong(rebar, GWL_STYLE, GetWindowLong(rebar, GWL_STYLE) | RBS_BANDBORDERS);
        update_bands(0);
        set_edge(notification, WS_EX_STATICEDGE);
        relayout();
        position_divider();
    } else {
        update_bands(0);
        position_divider(); /* Explorer may hide/reorder children after sign-in. */
    }
    updating = 0;
}

static VOID CALLBACK lease_timer(HWND hwnd, UINT msg, UINT_PTR id, DWORD time)
{
    (void)hwnd; (void)msg; (void)id; (void)time;
    if (!IsWindow(tray) || !owner || WaitForSingleObject(owner, 0) != WAIT_TIMEOUT)
        detach();
    else refresh_geometry();
}

__declspec(dllexport) LRESULT CALLBACK TaskbarHook(int code, WPARAM wp, LPARAM lp)
{
    if (code >= 0) {
        const CWPSTRUCT *message = (const CWPSTRUCT *)lp;
        if (message->message == RegisterWindowMessageA(CONTROL_MESSAGE)) {
            char name[64];
            GetClassNameA(message->hwnd, name, sizeof(name));
            if (!strcmp(name, "Shell_TrayWnd") && supported_os()) {
                if (!message->wParam) detach();
                else if (!tray) {
                    HMODULE pinned;
                    /* XP supports PIN. Avoid dangling callbacks after a helper crash. */
                    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCSTR)module_instance, &pinned)) {
                        owner = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)message->wParam);
                        if (owner) {
                            tray = message->hwnd;
                            timer_id = SetTimer(NULL, 0, 500, lease_timer);
                            if (!timer_id) detach();
                            else { SetPropA(tray, ACTIVE_PROPERTY, (HANDLE)1); refresh_geometry(); }
                        }
                    }
                }
            }
        }
    }
    return CallNextHookEx(NULL, code, wp, lp);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        module_instance = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
#else
static int enabled(void)
{
    HKEY key;
    DWORD value = 0, size = sizeof(value), type = 0;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, CONFIG_KEY, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return 0;
    if (RegQueryValueExA(key, "Taskbar98", NULL, &type, (BYTE *)&value, &size) != ERROR_SUCCESS ||
        type != REG_DWORD || size != sizeof(value)) value = 0;
    RegCloseKey(key);
    return value == 1;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show)
{
    char path[MAX_PATH], *slash;
    HANDLE mutex;
    HMODULE library;
    HOOKPROC callback;
    HHOOK hook = NULL;
    HWND attached = NULL;
    DWORD attached_pid = 0;
    UINT control = RegisterWindowMessageA(CONTROL_MESSAGE);
    (void)instance; (void)previous; (void)command; (void)show;
    if (!supported_os()) return 2;
    mutex = CreateMutexA(NULL, FALSE, "Local\\eXPerience2K.Taskbar98.Controller");
    if (!mutex) return 3;
    if (GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(mutex); return 0; }
    if (!GetModuleFileNameA(NULL, path, sizeof(path))) { CloseHandle(mutex); return 4; }
    slash = strrchr(path, '\\');
    if (!slash) { CloseHandle(mutex); return 4; }
    slash[1] = 0;
    if (strlen(path) + 32 >= sizeof(path)) { CloseHandle(mutex); return 4; }
#ifdef _WIN64
    strcat(path, "eXPerience2KTaskbar64.dll");
#else
    strcat(path, "eXPerience2KTaskbar32.dll");
#endif
    library = LoadLibraryA(path);
    if (!library) { CloseHandle(mutex); return 5; }
    callback = (HOOKPROC)(void *)GetProcAddress(library, "TaskbarHook");
    if (!callback) { FreeLibrary(library); CloseHandle(mutex); return 6; }
    while (enabled()) {
        HWND found = FindWindowA("Shell_TrayWnd", NULL);
        DWORD pid = 0, tid = found ? GetWindowThreadProcessId(found, &pid) : 0;
        DWORD_PTR result;
        if (found != attached || pid != attached_pid) {
            if (hook) UnhookWindowsHookEx(hook);
            hook = NULL; attached = NULL; attached_pid = 0;
        }
        if (tid && !hook) {
            hook = SetWindowsHookExA(WH_CALLWNDPROC, callback, library, tid);
            if (hook) { attached = found; attached_pid = pid; }
        }
        if (hook) SendMessageTimeoutA(attached, control, GetCurrentProcessId(), 0,
            SMTO_ABORTIFHUNG | SMTO_BLOCK, 500, &result);
        Sleep(500);
    }
    if (hook) {
        DWORD_PTR result;
        SendMessageTimeoutA(attached, control, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &result);
        UnhookWindowsHookEx(hook);
    }
    FreeLibrary(library);
    CloseHandle(mutex);
    return 0;
}
#endif
