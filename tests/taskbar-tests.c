#define TASKBAR_DLL
#define TASKBAR_TEST
#include "../src/eXPerience2KTaskbar.c"

static int failures;
#define CHECK(c, text) do { if (!(c)) { ++failures; printf("FAIL: %s\n", text); } \
    else printf("PASS: %s\n", text); } while (0)

static LRESULT CALLBACK fixture_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_DRAWITEM) {
        DRAWITEMSTRUCT *d = (DRAWITEMSTRUCT *)lp;
        RECT r = d->rcItem;
        FillRect(d->hDC, &r, GetSysColorBrush(COLOR_BTNFACE));
        FrameRect(d->hDC, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
        SetPixel(d->hDC, 4 + !!(d->itemState & ODS_SELECTED),
                 5 + !!(d->itemState & ODS_SELECTED), RGB(255,0,0));
        return TRUE;
    }
    if (msg == WM_SIZE && start_button && rebar) {
        MoveWindow(start_button, 0, 0, 50, 22, TRUE);
        MoveWindow(rebar, 52, 0, 350, 26, TRUE);
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static int fixture_pressed, fixture_redraw = 1, suppressed_state_changes;
static HDC expected_destination;
static int native_painted_offscreen;
static LRESULT CALLBACK native_button_painter(HWND hwnd, UINT msg, WPARAM wp,
                                              LPARAM lp, UINT_PTR id, DWORD_PTR data)
{
    (void)id; (void)data;
    if (msg == WM_SETREDRAW) { fixture_redraw = !!wp; return 0; }
    if (msg == BM_SETSTATE) {
        if (!fixture_redraw) ++suppressed_state_changes;
        fixture_pressed = !!wp;
        return 0;
    }
    if ((msg == WM_PRINTCLIENT || msg == WM_PAINT) && wp) {
        if (expected_destination && (HDC)wp != expected_destination &&
            GetPixel(expected_destination, 4, 5) == RGB(1,2,3))
            native_painted_offscreen = 1;
        DRAWITEMSTRUCT draw;
        ZeroMemory(&draw, sizeof(draw));
        draw.hDC = (HDC)wp;
        draw.itemState = fixture_pressed ? ODS_SELECTED : 0;
        GetClientRect(hwnd, &draw.rcItem);
        return fixture_proc(tray, WM_DRAWITEM, 0, (LPARAM)&draw);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static int width(HWND hwnd)
{
    RECT r;
    GetWindowRect(hwnd, &r);
    return r.right - r.left;
}

static UINT band_style(UINT band_id)
{
    UINT index = (UINT)SendMessage(rebar, RB_IDTOINDEX, band_id, 0);
    REBARBANDINFOA info;
    ZeroMemory(&info, sizeof(info));
    info.cbSize = REBARBANDINFO_V3_SIZE;
    info.fMask = RBBIM_STYLE;
    SendMessageA(rebar, RB_GETBANDINFOA, index, (LPARAM)&info);
    return info.fStyle;
}

int main(void)
{
    HINSTANCE instance = GetModuleHandle(NULL);
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_COOL_CLASSES | ICC_BAR_CLASSES};
    WNDCLASSA wc;
    REBARBANDINFOA band;
    RECT button_rect, bar_rect;
    LONG original_style;
    int i, expected;
    InitCommonControlsEx(&controls);
    ZeroMemory(&wc, sizeof(wc));
    wc.hInstance = instance;
    wc.lpfnWndProc = fixture_proc;
    wc.lpszClassName = "E2KTaskbarTest";
    RegisterClassA(&wc);
    wc.lpfnWndProc = DefWindowProcA;
    wc.lpszClassName = "MSTaskSwWClass";
    RegisterClassA(&wc);
    wc.lpszClassName = "TrayNotifyWnd";
    RegisterClassA(&wc);
    tray = CreateWindowA("E2KTaskbarTest", "Isolated taskbar fixture", WS_POPUP,
        0, 0, 500, 30, NULL, NULL, instance, NULL);
    start_button = CreateWindowA("Button", "Start", WS_CHILD,
        0, 0, 50, 22, tray, NULL, instance, NULL);
    rebar = CreateWindowA(REBARCLASSNAMEA, "", WS_CHILD | RBS_VARHEIGHT | CCS_NOPARENTALIGN | CCS_NORESIZE,
        52, 0, 350, 26, tray, NULL, instance, NULL);
    notification = CreateWindowA("TrayNotifyWnd", "", WS_CHILD,
        405, 0, 90, 26, tray, NULL, instance, NULL);
    task_list = CreateWindowA("MSTaskSwWClass", "", WS_CHILD,
        0, 0, 100, 20, rebar, NULL, instance, NULL);
    CHECK(tray && start_button && rebar && notification && task_list, "synthetic windows created");
    ZeroMemory(&band, sizeof(band));
    band.cbSize = REBARBANDINFO_V3_SIZE;
    band.fMask = RBBIM_ID | RBBIM_STYLE | RBBIM_SIZE | RBBIM_CHILDSIZE | RBBIM_CHILD;
    band.wID = 17;
    band.hwndChild = task_list;
    band.cx = 150; band.cxMinChild = 50; band.cyMinChild = 20;
    band.fStyle = RBBS_GRIPPERALWAYS;
    CHECK(SendMessageA(rebar, RB_INSERTBANDA, (WPARAM)-1, (LPARAM)&band), "native rebar band inserted");
    original_style = GetWindowLong(rebar, GWL_STYLE);
    SetWindowSubclass(start_button, native_button_painter, 99, 0);
    refresh_geometry();
    expected = 50 + extra_width;
    {
        HDC screen = GetDC(NULL), memory = CreateCompatibleDC(screen);
        HBITMAP bitmap = CreateCompatibleBitmap(screen, 53, 22);
        HBITMAP old = (HBITMAP)SelectObject(memory, bitmap);
        expected_destination = memory;
        SetPixel(memory, 4, 5, RGB(1,2,3));
        SendMessage(start_button, WM_PRINTCLIENT, (WPARAM)memory, PRF_CLIENT);
        CHECK(native_painted_offscreen, "native intermediate painting never touches destination DC");
        expected_destination = NULL;
        CHECK(GetPixel(memory, 4 + content_shift, 5) == RGB(255,0,0) &&
              GetPixel(memory, 4, 5) == GetSysColor(COLOR_BTNFACE) &&
              GetPixel(memory, 0, 0) == RGB(0,0,0),
              "native content moves right while frame stays fixed");
        SendMessage(start_button, WM_PAINT, (WPARAM)memory, 0);
        CHECK(GetPixel(memory, 4 + content_shift, 5) == RGB(255,0,0),
              "paint path shifts fresh native pixels without cumulative movement");
        SendMessage(start_button, BM_SETSTATE, TRUE, 0);
        CHECK(suppressed_state_changes == 1 && fixture_redraw && fixture_pressed,
              "pressed-state update suppresses direct native drawing and restores redraw");
        SendMessage(start_button, WM_PRINTCLIENT, (WPARAM)memory, PRF_CLIENT);
        CHECK(GetPixel(memory, 5 + content_shift, 6) == RGB(255,0,0),
              "native pressed-state content offset is preserved");
        SendMessage(start_divider, WM_PRINTCLIENT, (WPARAM)memory, PRF_CLIENT);
        CHECK(GetPixel(memory, 0, 0) == GetSysColor(COLOR_BTNSHADOW) &&
              GetPixel(memory, 0, 21) == GetSysColor(COLOR_BTNSHADOW) &&
              GetPixel(memory, 1, 0) == GetSysColor(COLOR_BTNHIGHLIGHT) &&
              GetPixel(memory, 1, 21) == GetSysColor(COLOR_BTNHIGHLIGHT),
              "divider is shadow then highlight for its full height without end caps");
        SelectObject(memory, old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(NULL, screen);
    }
    CHECK(active && width(start_button) == expected, "Start gains DPI-scaled three-pixel padding");
    GetWindowRect(start_button, &button_rect);
    GetWindowRect(rebar, &bar_rect);
    printf("Layout: Start right=%ld; rebar left=%ld right=%ld\n", button_rect.right, bar_rect.left, bar_rect.right);
    CHECK(bar_rect.left >= button_rect.right + 4 && bar_rect.right == 402,
        "rebar moves without overlapping Start or stealing notification area");
    CHECK((GetWindowLong(rebar, GWL_STYLE) & RBS_BANDBORDERS) &&
        !(band_style(17) & RBBS_CHILDEDGE), "band separators enabled while running-window band stays flat");
    CHECK(IsWindow(start_divider),
        "etched divider created after Start");
    { RECT divider_rect;
      GetWindowRect(start_divider, &divider_rect);
      CHECK(divider_rect.left == button_rect.right + 2 &&
            divider_rect.right <= bar_rect.left,
            "Start divider occupies its reserved gap without covering toolbar"); }
    CHECK(!(GetWindowLong(task_list, GWL_EXSTYLE) & WS_EX_STATICEDGE), "running windows have no added inset");
    CHECK(GetWindowLong(notification, GWL_EXSTYLE) & WS_EX_STATICEDGE, "notification inset enabled");
    ShowWindow(start_divider, SW_HIDE);
    refresh_geometry();
    CHECK(GetWindowLong(start_divider, GWL_STYLE) & WS_VISIBLE, "hidden divider restored after shell refresh");
    DestroyWindow(start_divider);
    refresh_geometry();
    CHECK(IsWindow(start_divider), "destroyed divider recreated after shell refresh");
    for (i = 0; i < 10; ++i) { relayout(); refresh_geometry(); }
    CHECK(width(start_button) == expected, "repeated Explorer layouts do not grow the button");
    band.wID = 33; band.hwndChild = NULL;
    band.fStyle = RBBS_CHILDEDGE | RBBS_FIXEDSIZE;
    CHECK(SendMessageA(rebar, RB_INSERTBANDA, (WPARAM)-1, (LPARAM)&band), "dynamic pre-edged band inserted");
    refresh_geometry();
    band.wID = 44; band.fStyle = RBBS_GRIPPERALWAYS;
    CHECK(SendMessageA(rebar, RB_INSERTBANDA, (WPARAM)-1, (LPARAM)&band), "additional flat toolbar band inserted");
    refresh_geometry();
    CHECK(band_style(44) & RBBS_CHILDEDGE, "toolbar band receives inset while task band remains flat");
    restore_geometry();
    CHECK(!start_divider, "Start divider removed on restoration");
    CHECK(width(start_button) == 50 && GetWindowLong(rebar, GWL_STYLE) == original_style,
        "native width and rebar style restored");
    CHECK(!(band_style(17) & RBBS_CHILDEDGE) && (band_style(17) & RBBS_GRIPPERALWAYS) &&
        (band_style(33) & RBBS_CHILDEDGE) && (band_style(33) & RBBS_FIXEDSIZE),
        "original edges restored by band ID without losing unrelated flags");
    CHECK(!(GetWindowLong(notification, GWL_EXSTYLE) & WS_EX_STATICEDGE), "notification edge restored");
    CHECK(!(band_style(44) & RBBS_CHILDEDGE), "added toolbar's original flat style restored");
    refresh_geometry();
    MoveWindow(tray, 0, 0, 30, 500, FALSE);
    refresh_geometry();
    CHECK(!active && width(start_button) == 50, "vertical taskbar suspends geometry");
    MoveWindow(tray, 0, 0, 500, 30, FALSE);
    refresh_geometry();
    CHECK(active && width(start_button) == expected, "horizontal taskbar resumes without cumulative padding");
    owner = CreateEventA(NULL, TRUE, TRUE, NULL);
    lease_timer(NULL, 0, 0, 0);
    CHECK(!active && !owner && !tray && width(start_button) == 50,
        "dead owner lease restores geometry and releases state");
    DestroyWindow(GetParent(start_button));
    return failures ? 1 : 0;
}
