/* Isolated integration tests: predefined registry roots are redirected to a
   throwaway HKCU test tree for this process. Never apply actual host settings. */
#define WinMain e2k_application_entry_not_invoked
#include "../src/eXPerience2KConfig.c"
#undef WinMain

static int failures;
#define CHECK(condition, label) do { \
    if (!(condition)) { printf("FAIL: %s\n", label); ++failures; } \
    else printf("PASS: %s\n", label); \
} while (0)

static int default_equals(const char *key, const char *name, const char *value, DWORD expected_type)
{
    char data[1024] = {0};
    DWORD type = 0, length = sizeof(data);
    return read_default_user_value(key, name, &type, (BYTE *)data, &length) &&
           type == expected_type && length == strlen(value) + 1 && !strcmp(data, value);
}

static int state_test(const WCHAR *directory, const WCHAR *source)
{
    HKEY real_user = NULL, test_root = NULL, roots[3] = {NULL, NULL, NULL};
    const HKEY predefined[3] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE, HKEY_USERS};
    const char *names[3] = {"User", "Machine", "Users"};
    char registry_path[160], assets[MAX_PATH], path[MAX_PATH];
    WCHAR temporary[MAX_PATH];
    DWORD size, type, preset;
    BYTE data[64];
    int index, redirected = 0;
    _snprintf(registry_path, sizeof(registry_path),
              "Software\\eXPerience2KTests\\LogonBackground-%lu", (unsigned long)GetCurrentProcessId());
    if (RegOpenCurrentUser(KEY_ALL_ACCESS, &real_user) != ERROR_SUCCESS ||
        RegCreateKeyExA(real_user, registry_path, 0, NULL, 0, KEY_ALL_ACCESS,
                        NULL, &test_root, NULL) != ERROR_SUCCESS) return 2;
    for (index = 0; index < 3; ++index) {
        if (RegCreateKeyExA(test_root, names[index], 0, NULL, 0, KEY_ALL_ACCESS,
                            NULL, &roots[index], NULL) != ERROR_SUCCESS) goto cleanup;
    }
    for (index = 0; index < 3; ++index) {
        if (RegOverridePredefKey(predefined[index], roots[index]) != ERROR_SUCCESS) goto cleanup;
        ++redirected;
    }
    g_instance = GetModuleHandleA(NULL);
    g_use_current_user_fallback = 1;
    g_cross_user = 0;
    if (!WideCharToMultiByte(CP_ACP, 0, directory, -1, g_install_root,
                             MAX_PATH, NULL, NULL)) goto cleanup;
    join_path(assets, sizeof(assets), g_install_root, "Assets");
    SHCreateDirectoryExA(NULL, assets, NULL);
    {
        LOGFONTW theme_font;
        const char original_font[] = "%OriginalFont%";
        int present;
        CHECK(theme_by_id("windows-95") == NULL, "unimplemented preset rejected");
        CHECK(theme_by_id("../windows-98-nt5") == NULL, "preset path traversal rejected");
        g_theme = theme_by_id("windows-98-nt5");
        initialize_w2k_logfont_w(&theme_font, FW_NORMAL);
        CHECK(!lstrcmpW(theme_font.lfFaceName, L"MS Sans Serif"), "98 interface font");
        CHECK(!strcmp(caption_color_value("ButtonFace", "212 208 200", CAPTION_PRESET_BLUE_GRADIENT),
                      "192 192 192"), "98 silver controls");
        CHECK(!strcmp(caption_color_value("ActiveTitle", "10 36 106", CAPTION_PRESET_BLUE_GRADIENT),
                      "0 0 128"), "98 navy gradient start");
        for (present = 0; present < 2; ++present) {
            delete_machine_value(EXPLORER_MACHINE_STATE_KEY, "Captured_ThemeTahoma");
            if (present)
                CHECK(write_machine_value(FONT_SUBSTITUTES_KEY, "Tahoma", REG_EXPAND_SZ,
                    (const BYTE *)original_font, sizeof(original_font)), "seed original font type");
            else delete_machine_value(FONT_SUBSTITUTES_KEY, "Tahoma");
            CHECK(apply_theme_font_substitution(1) && apply_theme_font_substitution(1),
                  "repeated 98 Apply retains font baseline");
            size = sizeof(data); type = 0;
            CHECK(read_machine_value(FONT_SUBSTITUTES_KEY, "Tahoma", &type, data, &size) &&
                  type == REG_SZ && !strcmp((char *)data, "MS Sans Serif"), "Tahoma substitution applied");
            CHECK(apply_theme_font_substitution(0), "2000 switch restores font substitution");
            size = sizeof(data); type = 0;
            if (present) {
                CHECK(read_machine_value(FONT_SUBSTITUTES_KEY, "Tahoma", &type, data, &size) &&
                      type == REG_EXPAND_SZ && size == sizeof(original_font) &&
                      !memcmp(data, original_font, sizeof(original_font)), "font type and bytes restored exactly");
            } else {
                CHECK(!read_machine_value(FONT_SUBSTITUTES_KEY, "Tahoma", &type, data, &size),
                      "originally absent Tahoma substitution removed");
            }
        }
        g_theme = theme_by_id("windows-2000");
        initialize_w2k_logfont_w(&theme_font, FW_NORMAL);
        CHECK(!lstrcmpW(theme_font.lfFaceName, L"Tahoma"), "2000 font unchanged");
    }
    {
        BYTE preferences[] = {0x9e, 0x3e, 0x07, 0x80, 0x55};
        set_menu_preference_bits(preferences, TRUE, FALSE);
        CHECK(preferences[0] == 0x9e && preferences[1] == 0x3c &&
              preferences[2] == 0x07 && preferences[3] == 0x80 && preferences[4] == 0x55,
              "Run As sliding updates only menu preference bits");
        set_menu_preference_bits(preferences, FALSE, TRUE);
        CHECK(preferences[0] == 0x9c && preferences[1] == 0x3e && preferences[4] == 0x55,
              "Run As animation and fade bits remain independent");
    }
    {
        HANDLE retained_state;
        join_path(path, sizeof(path), g_install_root, "state.tsv");
        retained_state = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, NULL);
        CHECK(retained_state != INVALID_HANDLE_VALUE, "create retained-backup fixture");
        if (retained_state != INVALID_HANDLE_VALUE) CloseHandle(retained_state);
        CHECK(!resource_conversion_detected(), "retained backups alone are not active conversion for a new user");
        CHECK(configure_resource_reloader(1) && resource_conversion_detected(),
              "new user detects the active installation reloader");
        write_machine_string("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
                             "eXPerience2K Resource Reloader", "unrelated-command.exe");
        CHECK(!resource_conversion_detected(), "unrelated Run value is not an active conversion");
        delete_machine_value("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
                             "eXPerience2K Resource Reloader");
        write_user_dword(CONFIG_KEY, "Configured", 1);
        write_user_dword(CONFIG_KEY, "ResourceConversionEnabled", 1);
        CHECK(resource_conversion_detected(), "existing transaction marker retains failed-restore recovery state");
        delete_user_value(CONFIG_KEY, "Configured");
        delete_user_value(CONFIG_KEY, "ResourceConversionEnabled");
        DeleteFileA(path);
    }
    {
        size_t preference;
        write_user_dword(CONFIG_KEY, "ExplorerExperimentEnabled", 1);
        write_machine_dword(EXPLORER_MACHINE_STATE_KEY, "Enabled", 1);
        write_user_dword("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", "WebView", 0);
        CHECK(explorer_enablement_markers_detected(), "Explorer enablement does not require every initial preference");
        for (preference = 0; preference < sizeof(g_explorer_dwords) / sizeof(g_explorer_dwords[0]); ++preference) {
            const EXPLORER_DWORD_VALUE *item = &g_explorer_dwords[preference];
            if (strcmp(item->marker, "ExplorerWebView"))
                write_user_dword(item->subkey, item->name, item->value + 1);
        }
        for (preference = 0; preference < sizeof(g_explorer_strings) / sizeof(g_explorer_strings[0]); ++preference) {
            const EXPLORER_STRING_VALUE *item = &g_explorer_strings[preference];
            write_user_string(item->subkey, item->name, "user preference");
        }
        CHECK(explorer_enablement_markers_detected(), "native folder and toolbar choices do not disable Explorer integration");
        write_user_dword("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", "WebView", 1);
        CHECK(!explorer_enablement_markers_detected(), "Common Tasks remains a structural layout conflict");
        write_user_dword("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced", "WebView", 0);
        write_machine_dword(EXPLORER_MACHINE_STATE_KEY, "Enabled", 0);
        CHECK(!explorer_enablement_markers_detected(), "disabled machine integration is not reported enabled");
        write_machine_dword(EXPLORER_MACHINE_STATE_KEY, "Enabled", 1);
        write_user_dword(CONFIG_KEY, "ExplorerExperimentEnabled", 0);
        CHECK(!explorer_enablement_markers_detected(), "disabled user integration is not reported enabled");
        delete_user_value(CONFIG_KEY, "ExplorerExperimentEnabled");
        delete_machine_value(EXPLORER_MACHINE_STATE_KEY, "Enabled");
    }
    g_features[FEATURE_CLASSIC_LOGON].checkbox = CreateWindowA("BUTTON", "",
        WS_POPUP | BS_AUTOCHECKBOX, 0, 0, 0, 0, NULL, NULL, g_instance, NULL);
    g_logon_background_combo = CreateWindowA("COMBOBOX", "",
        WS_POPUP | CBS_DROPDOWNLIST, 0, 0, 0, 0, NULL, NULL, g_instance, NULL);
    for (index = 0; index < 3; ++index) SendMessageA(g_logon_background_combo, CB_ADDSTRING, 0, (LPARAM)names[index]);
    Button_SetCheck(g_features[FEATURE_CLASSIC_LOGON].checkbox, BST_CHECKED);
    SendMessageA(g_logon_background_combo, CB_SETCURSEL, LOGON_BACKGROUND_BLUE, 0);

    write_user_string("Control Panel\\Desktop", "Wallpaper", "signed-in-desktop-untouched.bmp");
    write_default_user_value("Control Panel\\Desktop", "Wallpaper", REG_EXPAND_SZ,
        (const BYTE *)"%SystemRoot%\\original.bmp", sizeof("%SystemRoot%\\original.bmp"));
    write_default_user_value("Control Panel\\Desktop", "TileWallpaper", REG_SZ, (const BYTE *)"7", 2);
    write_default_user_value("Control Panel\\Desktop", "Pattern", REG_SZ, (const BYTE *)"original pattern", sizeof("original pattern"));
    write_default_user_value("Control Panel\\Colors", "Background", REG_SZ, (const BYTE *)"11 22 33", sizeof("11 22 33"));
    CHECK(capture_exact_machine_baseline(), "capture immutable logon baseline");
    CHECK(prepare_logon_background(), "existing 3.0 blue asset is ready");
    CHECK(apply_default_w2k_appearance(1, CAPTION_PRESET_SOLID_NAVY), "apply blue with solid caption");
    CHECK(default_equals("Control Panel\\Colors", "Background", "58 110 165", REG_SZ), "exact current blue RGB");
    CHECK(default_equals("Control Panel\\Desktop", "TileWallpaper", "1", REG_SZ), "blue bitmap tiles");

    /* Simulate upgrade: schema 1 and the existing first-Apply baseline remain. */
    CHECK(capture_exact_machine_baseline(), "3.0 baseline reused without recapture");
    SendMessageA(g_logon_background_combo, CB_SETCURSEL, LOGON_BACKGROUND_TEAL, 0);
    CHECK(prepare_logon_background(), "generate persistent teal BMP");
    CHECK(apply_default_w2k_appearance(1, CAPTION_PRESET_BLUE_GRADIENT), "apply teal with independent gradient caption");
    CHECK(default_equals("Control Panel\\Colors", "Background", "0 128 128", REG_SZ), "exact Windows 95 teal RGB");
    CHECK(default_equals("Control Panel\\Colors", "ActiveTitle", "10 36 106", REG_SZ), "caption preset remains independent");
    CHECK(detected_logon_background() == LOGON_BACKGROUND_TEAL, "reopen detects teal");

    SendMessageA(g_logon_background_combo, CB_SETCURSEL, LOGON_BACKGROUND_CUSTOM, 0);
    CHECK(!prepare_logon_background(), "missing custom image fails before settings change");
    CHECK(default_equals("Control Panel\\Colors", "Background", "0 128 128", REG_SZ), "failure preserves applied background");
    lstrcpynW(temporary, directory, MAX_PATH);
    lstrcatW(temporary, L"\\prepared.tmp");
    CHECK(e2k_convert_logon_image(source, temporary, 800, 600), "decode custom input to compatible BMP");
    lstrcpynW(g_pending_logon_image, temporary, MAX_PATH);
    CHECK(prepare_logon_background(), "copy custom image outside user profile");
    CHECK(g_pending_logon_image[0] == 0 && GetFileAttributesW(temporary) == INVALID_FILE_ATTRIBUTES,
          "temporary image cleaned after installation");
    CHECK(apply_default_w2k_appearance(1, CAPTION_PRESET_SOLID_NAVY), "apply custom image");
    logon_background_path(LOGON_BACKGROUND_CUSTOM, path, sizeof(path));
    CHECK(default_equals("Control Panel\\Desktop", "Wallpaper", path, REG_SZ), "secure desktop references managed BMP");
    CHECK(default_equals("Control Panel\\Desktop", "TileWallpaper", "0", REG_SZ), "custom image is not tiled");
    CHECK(default_equals("Control Panel\\Desktop", "WallpaperStyle", "0", REG_SZ), "custom image is centered without distortion");
    CHECK(detected_logon_background() == LOGON_BACKGROUND_CUSTOM, "reopen detects custom image");
    CHECK(read_user_dword(CONFIG_KEY, "LogonBackgroundPreset", &preset) && preset == LOGON_BACKGROUND_CUSTOM,
          "custom selection persists");
    CHECK(prepare_logon_background(), "reapply uses installed BMP without original source");
    CHECK(user_string_equals("Control Panel\\Desktop", "Wallpaper", "signed-in-desktop-untouched.bmp"),
          "signed-in desktop wallpaper unchanged");
    CHECK(apply_default_w2k_appearance(0, CAPTION_PRESET_SOLID_NAVY), "clear classic logon restores original settings");
    CHECK(default_equals("Control Panel\\Desktop", "Wallpaper", "%SystemRoot%\\original.bmp", REG_EXPAND_SZ),
          "restore exact original wallpaper data and REG_EXPAND_SZ type");
    CHECK(default_equals("Control Panel\\Desktop", "TileWallpaper", "7", REG_SZ), "restore original tiling rather than guessed default");
    CHECK(default_equals("Control Panel\\Desktop", "Pattern", "original pattern", REG_SZ), "restore original pattern");
    CHECK(default_equals("Control Panel\\Colors", "Background", "11 22 33", REG_SZ), "restore original background color");
    size = sizeof(data); type = 0;
    CHECK(!read_default_user_value("Control Panel\\Desktop", "WallpaperStyle", &type, data, &size),
          "restore original value absence");
    CHECK(capture_exact_machine_baseline() && apply_default_w2k_appearance(1, CAPTION_PRESET_SOLID_NAVY) &&
          apply_default_w2k_appearance(0, CAPTION_PRESET_SOLID_NAVY) &&
          default_equals("Control Panel\\Desktop", "Wallpaper", "%SystemRoot%\\original.bmp", REG_EXPAND_SZ),
          "repeat apply/revert retains immutable baseline");
    CHECK(strstr(g_log, g_install_root) == NULL && strstr(g_log, "original.bmp") == NULL,
          "diagnostics omit image and profile paths");
    {
        /* Cross-user path avoids changing the host's live colors/SPI metrics;
           every registry write remains redirected to the isolated test hive. */
        g_cross_user = 1;
        g_theme = theme_by_id("windows-98-nt5");
        CHECK(write_user_string("Control Panel\\Colors", "ButtonLight", "17 18 19"),
              "seed original desktop bevel color");
        CHECK(apply_classic_theme(1, CAPTION_PRESET_BLUE_GRADIENT) &&
              theme_palette_matches(CAPTION_PRESET_BLUE_GRADIENT), "apply complete 98 desktop palette");
        CHECK(user_string_equals("Control Panel\\Colors", "ButtonLight", "223 223 223") &&
              user_string_equals("Control Panel\\Colors", "ButtonDkShadow", "0 0 0") &&
              user_string_equals("Control Panel\\Colors", "HotTrackingColor", "0 0 255"),
              "98 bevel and hot-tracking colors reach Control Panel Colors");
        CHECK(write_user_string("Control Panel\\Colors", "ButtonLight", "192 192 192") &&
              !theme_palette_matches(CAPTION_PRESET_BLUE_GRADIENT), "old approximate 98 palette requires reapplication");
        CHECK(apply_classic_theme(1, CAPTION_PRESET_BLUE_GRADIENT) &&
              apply_classic_theme(0, CAPTION_PRESET_BLUE_GRADIENT) &&
              user_string_equals("Control Panel\\Colors", "ButtonLight", "17 18 19"),
              "repeated palette application retains original desktop bevel for Revert");
        SendMessageA(g_logon_background_combo, CB_SETCURSEL, LOGON_BACKGROUND_TEAL, 0);
        CHECK(prepare_logon_background() && apply_default_w2k_appearance(1, CAPTION_PRESET_SOLID_NAVY) &&
              default_equals("Control Panel\\Colors", "ButtonLight", "223 223 223", REG_SZ) &&
              default_equals("Control Panel\\Colors", "ButtonDkShadow", "0 0 0", REG_SZ),
              "secure logon desktop also receives 98 bevel colors");
        CHECK(apply_default_w2k_appearance(0, CAPTION_PRESET_SOLID_NAVY), "restore logon palette");
        g_theme = theme_by_id("windows-2000");
        CHECK(apply_classic_theme(1, CAPTION_PRESET_BLUE_GRADIENT) &&
              user_string_equals("Control Panel\\Colors", "ButtonLight", "212 208 200") &&
              user_string_equals("Control Panel\\Colors", "ButtonDkShadow", "64 64 64"),
              "switching to 2000 restores its independent bevel palette");
        CHECK(apply_classic_theme(0, CAPTION_PRESET_BLUE_GRADIENT), "restore desktop after 2000 switch");
        g_cross_user = 0;
    }
    DestroyWindow(g_logon_background_combo);
    DestroyWindow(g_features[FEATURE_CLASSIC_LOGON].checkbox);
    {
        WNDCLASSA test_class;
        RECT combo_rect, feature_rect, last_rect, caption_rect;
        ZeroMemory(&test_class, sizeof(test_class));
        test_class.lpfnWndProc = window_proc;
        test_class.hInstance = g_instance;
        test_class.lpszClassName = "eXPerience2KThemeUITest";
        CHECK(RegisterClassA(&test_class) != 0, "register isolated UI fixture");
        g_probe.supported = g_probe.resource_ready = g_probe.administrator = 1;
        g_window = CreateWindowA(test_class.lpszClassName, "Preset UI fixture", WS_OVERLAPPEDWINDOW,
            0, 0, 550, 720, NULL, NULL, g_instance, NULL);
        CHECK(g_window != NULL, "create real configuration controls without applying host settings");
        if (g_window) {
            SendMessageA(g_theme_combo, CB_SETCURSEL, 1, 0);
            SendMessageA(g_window, WM_COMMAND, MAKEWPARAM(IDC_THEME_PRESET, CBN_SELCHANGE), (LPARAM)g_theme_combo);
            CHECK(Button_GetCheck(g_features[FEATURE_WALLPAPERS].checkbox) == BST_UNCHECKED &&
                  Button_GetCheck(g_features[FEATURE_CLASSIC_EXPLORER].checkbox) == BST_UNCHECKED &&
                  selected_logon_background() == LOGON_BACKGROUND_TEAL,
                  "98 selector stages appropriate feature and background defaults");
            CHECK(g_theme == theme_by_id("windows-2000"), "browsing selector does not apply a preset");
            Button_SetCheck(g_features[FEATURE_RESOURCE_CONVERSION].checkbox, BST_CHECKED);
            update_theme_font_label();
            CHECK(IsWindowEnabled(g_low_color_checkbox), "low-colour option enabled for 98 resource conversion");
            CHECK(Button_GetCheck(g_taskbar_checkbox) == BST_UNCHECKED, "taskbar experiment defaults off");
            Button_SetCheck(g_features[FEATURE_CLASSIC_THEME].checkbox, BST_CHECKED);
            Button_SetCheck(g_features[FEATURE_CLASSIC_START_MENU].checkbox, BST_CHECKED);
            update_theme_font_label();
            CHECK(IsWindowEnabled(g_taskbar_checkbox), "taskbar modifier available with 98 Classic settings");
            g_theme = theme_by_id("windows-98-nt5");
            Button_SetCheck(g_low_color_checkbox, BST_CHECKED);
            CHECK(selected_resource_theme()->low_color_icons, "checkbox selects low-colour resource variant");
            CHECK(write_machine_string(EXPLORER_MACHINE_STATE_KEY, "ResourceThemePreset", selected_resource_theme()->id) &&
                  resource_theme_needs_update(), "old preset revision requires updated winver resources");
            CHECK(write_machine_dword(EXPLORER_MACHINE_STATE_KEY, "ResourceThemeRevision", THEME_RESOURCE_REVISION) &&
                  !resource_theme_needs_update(), "active low-colour resource preset is recognized");
            Button_SetCheck(g_low_color_checkbox, BST_UNCHECKED);
            CHECK(resource_theme_needs_update(), "unticking low-colour checkbox requires repatching normal icons");
            CHECK(write_user_dword(CONFIG_KEY, "LowColorIcons", 1) &&
                  write_user_dword(CONFIG_KEY, "ResourceConversionEnabled", 1), "seed saved low-colour selection");
            refresh_states(0);
            CHECK(Button_GetCheck(g_low_color_checkbox) == BST_CHECKED, "reopen retains active low-colour selection");
            g_theme = theme_by_id("windows-2000");
            CHECK(!selected_resource_theme()->low_color_icons, "2000 ignores 98-only icon modifier");
            GetWindowRect(g_theme_combo, &combo_rect);
            GetWindowRect(g_features[0].checkbox, &feature_rect);
            GetWindowRect(g_features[MAX_FEATURES - 1].checkbox, &last_rect);
            GetWindowRect(GetDlgItem(g_window, IDC_CAPTION_PRESET_GROUP), &caption_rect);
            CHECK(combo_rect.bottom <= feature_rect.top && last_rect.bottom <= caption_rect.top,
                  "preset, features and caption controls do not overlap");
            SendMessageA(g_theme_combo, CB_SETCURSEL, 0, 0);
            SendMessageA(g_window, WM_COMMAND, MAKEWPARAM(IDC_THEME_PRESET, CBN_SELCHANGE), (LPARAM)g_theme_combo);
            CHECK(Button_GetCheck(g_features[FEATURE_WALLPAPERS].checkbox) == BST_CHECKED &&
                  Button_GetCheck(g_features[FEATURE_CLASSIC_EXPLORER].checkbox) == BST_CHECKED &&
                  selected_logon_background() == LOGON_BACKGROUND_BLUE,
                  "2000 selector stages original optional features");
            CHECK(!IsWindowEnabled(g_low_color_checkbox), "98-only option disabled for 2000");
            CHECK(!IsWindowEnabled(g_taskbar_checkbox), "98 taskbar modifier disabled for 2000");
            DestroyWindow(g_window);
            g_window = NULL;
        }
        UnregisterClassA(test_class.lpszClassName, g_instance);
    }
    {
        const char *run_key = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
        CHECK(write_user_string(run_key, "eXPerience2K Taskbar98", "original") &&
              capture_original_user_value_checked("Taskbar98Run", run_key, "eXPerience2K Taskbar98") &&
              write_user_string(run_key, "eXPerience2K Taskbar98", "replacement") &&
              write_user_dword(CONFIG_KEY, "Taskbar98", 1), "seed isolated taskbar startup baseline");
        CHECK(configure_taskbar_geometry(0) &&
              user_string_equals(run_key, "eXPerience2K Taskbar98", "original"),
              "disabling taskbar restores pre-existing startup value");
        { DWORD enabled = 1;
          CHECK(read_user_dword(CONFIG_KEY, "Taskbar98", &enabled) && !enabled,
                "disabling taskbar signals controller to restore shell"); }
    }
cleanup:
    for (index = 0; index < redirected; ++index) RegOverridePredefKey(predefined[index], NULL);
    for (index = 0; index < 3; ++index) if (roots[index]) RegCloseKey(roots[index]);
    if (test_root) RegCloseKey(test_root);
    if (real_user) { SHDeleteKeyA(real_user, registry_path); RegCloseKey(real_user); }
    if (redirected != 3) return 2;
    return failures ? 1 : 0;
}

int wmain(int argc, WCHAR **argv)
{
    if (argc == 6 && !lstrcmpW(argv[1], L"convert"))
        return e2k_convert_logon_image(argv[2], argv[3], (UINT)_wtoi(argv[4]), (UINT)_wtoi(argv[5])) ? 0 : 1;
    if (argc == 4 && !lstrcmpW(argv[1], L"state")) return state_test(argv[2], argv[3]);
    return 2;
}
