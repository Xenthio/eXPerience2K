/* Exercises real core parsing/resolution without touching protected OS files. */
#define main e2k_core_entry_not_invoked
#include "../src/eXPerience2KCore.c"
#undef main

static int failures;
#define CHECK(c, label) do { if (!(c)) { ++failures; printf("FAIL: %s\n", label); } \
    else printf("PASS: %s\n", label); } while (0)

static int patched_icon_matches(const char *patched, const char *asset)
{
    HMODULE module = LoadLibraryExA(patched, NULL, LOAD_LIBRARY_AS_DATAFILE);
    HRSRC group_resource;
    const GROUP_ICON_ENTRY *entries;
    const BYTE *group;
    BYTE *ico = NULL;
    DWORD size = 0;
    unsigned index, count;
    int ok = 1;
    if (!module || !read_entire_file(asset, &ico, &size)) {
        if (module) FreeLibrary(module);
        return 0;
    }
    group_resource = FindResourceA(module, MAKEINTRESOURCEA(1), RT_GROUP_ICON);
    group = group_resource ? (const BYTE *)LockResource(LoadResource(module, group_resource)) : NULL;
    if (!group || size < sizeof(ICON_FILE_HEADER)) ok = 0;
    if (ok) {
        count = ((const ICON_FILE_HEADER *)ico)->count;
        ok = ((const ICON_FILE_HEADER *)group)->count == count;
        entries = (const GROUP_ICON_ENTRY *)(group + sizeof(ICON_FILE_HEADER));
        for (index = 0; ok && index < count; ++index) {
            const ICON_FILE_ENTRY *source = ((const ICON_FILE_ENTRY *)(ico + sizeof(ICON_FILE_HEADER))) + index;
            HRSRC image_resource = FindResourceA(module, MAKEINTRESOURCEA(entries[index].resource_id), RT_ICON);
            const BYTE *image_data = image_resource ? (const BYTE *)LockResource(LoadResource(module, image_resource)) : NULL;
            if (g_theme->low_color_icons && entries[index].bit_count > 4) ok = 0;
            ok = ok && image_data && SizeofResource(module, image_resource) == source->bytes_in_resource &&
                 source->image_offset + source->bytes_in_resource <= size &&
                 !memcmp(image_data, ico + source->image_offset, source->bytes_in_resource);
        }
    }
    HeapFree(GetProcessHeap(), 0, ico);
    FreeLibrary(module);
    return ok;
}

static int patched_bitmap_matches(const char *patched, const char *asset)
{
    HMODULE module = LoadLibraryExA(patched, NULL, LOAD_LIBRARY_AS_DATAFILE);
    HRSRC resource;
    const BYTE *data;
    BYTE *bmp = NULL;
    DWORD size = 0;
    int ok;
    if (!module) return 0;
    resource = FindResourceA(module, MAKEINTRESOURCEA(130), RT_BITMAP);
    data = resource ? (const BYTE *)LockResource(LoadResource(module, resource)) : NULL;
    ok = read_entire_file(asset, &bmp, &size) && size > 14 && data &&
        SizeofResource(module, resource) == size - 14 && !memcmp(data, bmp + 14, size - 14);
    if (bmp) HeapFree(GetProcessHeap(), 0, bmp);
    FreeLibrary(module);
    return ok;
}

int main(int argc, char **argv)
{
    STATE_RECORD record;
    char old[] = "T1\tnative\tshell32.dll.txt\tC:\\test.dll\tC:\\backup.dll\t12345678\t12345679\t10\t20";
    char current[] = "T1\tnative\tshell32.dll.txt\tC:\\test.dll\tC:\\backup.dll\t12345678\t12345679\t10\t20\twindows-98-nt5";
    char low_state[] = "T1\tnative\tshell32.dll.txt\tC:\\test.dll\tC:\\backup.dll\t12345678\t12345679\t10\t20\twindows-98-nt5-low-color\t12345677";
    char invalid[] = "T1\tnative\tshell32.dll.txt\tC:\\test.dll\tC:\\backup.dll\t12345678\t12345679\t10\t20\t../evil";
    char path[MAX_PATH_TEXT], line[16384];
    FILE *file;
    if (argc != 3) return 2;
    CHECK(parse_state_record(old, &record) && !strcmp(record.theme_id, "windows-2000"),
          "legacy state defaults to Windows 2000");
    CHECK(parse_state_record(current, &record) && !strcmp(record.theme_id, "windows-98-nt5"),
          "98 preset retained in state");
    file = tmpfile();
    if (!file) return 2;
    write_state_record(file, &record);
    rewind(file);
    CHECK(fgets(line, sizeof(line), file) && parse_state_record(line, &record) &&
          !strcmp(record.theme_id, "windows-98-nt5") && record.original_crc == 0x12345678,
          "state roundtrip preserves preset and original CRC");
    fclose(file);
    record.previous_crc = 0x87654321;
    CHECK(!is_new_system_image(&record, record.previous_crc), "pending previous preset cannot replace original backup");
    CHECK(!is_new_system_image(&record, record.original_crc), "restored original is not an OS update");
    CHECK(!is_new_system_image(&record, record.patched_crc), "current preset is not an OS update");
    CHECK(is_new_system_image(&record, 0x87654322), "genuine OS update retains upstream rebase behavior");
    CHECK(!parse_state_record(invalid, &record), "unknown state preset rejected");
    CHECK(parse_state_record(low_state, &record) && theme_by_id(record.theme_id)->low_color_icons &&
          record.previous_crc == 0x12345677, "low-colour state survives reload with previous CRC");
    {
        char fixture[MAX_PATH_TEXT], name[64], rgb[64], raw[16];
        unsigned colors = 0;
        join_path(fixture, sizeof(fixture), argv[1], "..\\reference-assets\\windows-98\\colors.tsv");
        file = fopen(fixture, "rb");
        if (!file) return 2;
        if (!fgets(line, sizeof(line), file)) return 2;
        while (fgets(line, sizeof(line), file)) {
            if (sscanf(line, "%63[^\t]\t%63[^\t]\t%15s", name, rgb, raw) != 3) return 2;
            CHECK(!strcmp(theme_color(&g_theme_presets[1], name, "missing"), rgb), name);
            CHECK(!strcmp(theme_color(&g_theme_presets[0], name, "unchanged"), "unchanged"),
                  "2000 retains original color");
            ++colors;
        }
        fclose(file);
        CHECK(colors == 29, "complete 29-color Windows 98 VM fixture");
    }
    g_theme = theme_by_id("windows-98-nt5");
    lstrcpyA(g_branding_id, "xp-x86");
    CHECK(resolve_asset_path(path, sizeof(path), argv[1], "Resources\\eXPerience2K\\shell32\\3.ico") &&
          strstr(path, "Themes\\windows-98-nt5\\") && regular_file_exists(path), "98 overlay resolves");
    CHECK(resolve_asset_path(path, sizeof(path), argv[1], "Resources\\eXPerience2K\\msgina\\101.bmp") &&
          strstr(path, "Themes\\windows-98-nt5\\"), "preset precedes OS branding");
    CHECK(resolve_asset_path(path, sizeof(path), argv[1], "Resources\\eXPerience2K\\calc\\SC.ico") &&
          !strstr(path, "Themes\\"), "unmapped assets inherit base payload");
    g_theme = theme_by_id("windows-98-nt5-low-color");
    CHECK(resolve_asset_path(path, sizeof(path), argv[1], "Resources\\eXPerience2K\\shell32\\3.ico") &&
          strstr(path, "LowColor\\") && regular_file_exists(path), "low-colour variant resolves original 4bpp frames");
    CHECK(resolve_asset_path(path, sizeof(path), argv[1], "Resources\\eXPerience2K\\msgina\\101.bmp") &&
          strstr(path, "Themes\\windows-98-nt5\\") && !strstr(path, "LowColor\\"),
          "low-colour checkbox retains regular 98 branding");
    g_theme = theme_by_id("windows-2000");
    CHECK(resolve_asset_path(path, sizeof(path), argv[1], "Resources\\eXPerience2K\\shell32\\3.ico") &&
          !strstr(path, "Themes\\"), "switch to 2000 stops resolving legacy assets");
    {
        char script[MAX_PATH_TEXT], output[MAX_PATH_TEXT];
        int i;
        const char *sequence[] = {"windows-2000", "windows-98-nt5", "windows-98-nt5-low-color",
                                  "windows-98-nt5", "windows-2000"};
        join_path(script, sizeof(script), argv[2], "theme-fixture.txt");
        file = fopen(script, "wb");
        if (!file) return 2;
        fputs("[COMMANDS]\n-addoverwrite Resources\\eXPerience2K\\shell32\\3.ico, ICONGROUP, 1,\n"
              "-addoverwrite Resources\\eXPerience2K\\shell32\\130.bmp, BITMAP, 130,\n", file);
        fclose(file);
        for (i = 0; i < 5; ++i) {
            g_theme = theme_by_id(sequence[i]);
            _snprintf(output, sizeof(output), "%s\\theme-fixture-%d.exe", argv[2], i);
            CHECK(patch_file(argv[0], script, argv[1], output), "patch isolated PE fixture");
            resolve_asset_path(path, sizeof(path), argv[1], "Resources\\eXPerience2K\\shell32\\3.ico");
            CHECK(patched_icon_matches(output, path), "all patched icon images match selected preset bytes");
            resolve_asset_path(path, sizeof(path), argv[1], "Resources\\eXPerience2K\\shell32\\130.bmp");
            CHECK(patched_bitmap_matches(output, path), "winver bitmap matches selected preset through theme switches");
        }
    }
    return failures ? 1 : 0;
}
