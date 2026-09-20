# Theme presets (development)

The configuration app now offers **Windows 2000** and an experimental
**Windows 98 / NT 5.0 Beta** preset. These are appearance presets for the same
supported English XP Professional x86 SP3 / x64 SP2 systems. They do not add
support for running the patcher on Windows 98 or NT 5.0.

Windows 2000 remains the default, including for installations without a saved
preset. Its original resource, sound, wallpaper and Explorer assets are unchanged.
Windows 95 is intentionally not exposed until it has its own reviewed assets.

## Using the new preset

1. Install the development build on a disposable supported XP snapshot.
2. Select **Windows 98 / NT 5.0 Beta (experimental)** in **Theme preset**.
3. Review the independent feature checkboxes, then Apply as administrator.
4. Restart before assessing or switching the protected resources again.
5. Select Windows 2000 and Apply to return to that preset, or use Revert to
   restore the saved pre-Apply setup. Uninstall uses the same restoration path.

Changing the selector does not write settings until Apply. Selecting the 98
preset chooses teal for the logon background and clears the optional 2000-only
wallpaper installation and experimental Explorer pane. Selecting 2000 chooses
blue and selects those two features. The user can change these selections
before Apply. Caption choices remain independent. Reopening does not reassert
these defaults.

### Low-colour icons

With the 98 preset and visual resource conversion selected, tick
**Use low-colour (16-colour) 98 icons**, Apply as administrator, and restart.
Untick it and Apply/restart to restore the normal multi-depth icon groups.
This option defaults off. Its preference is retained, but is disabled and has
no effect with Windows 2000 or with resource conversion off. It does not
change the display colour depth, wallpaper, bitmap artwork or palette.

The payload's `LowColor` overlay selects existing 1-bit/4-bit frames directly
from the original ICOs, preserving their pixel and transparency-mask bytes.
It covers all 77 icons supplied by the 98/NT preset and another 408 inherited
base icons. Icons with no original low-colour frame retain their normal art;
unmanaged third-party application icons are not affected. Nothing is quantized.
Run `scripts/build-low-color-icons.py` after reimporting source artwork.
`LowColor/assets.tsv` records the source and output hashes, and build verification
checks each retained image against the original bytes.

The historical registry setting is the REG_SZ value `Shell Icon BPP` under
`HKCU\Control Panel\Desktop\WindowMetrics`; 4 bits means 16 colours.
However, [OpenJDK's Windows implementation](https://github.com/openjdk/jdk6/blob/master/jdk/src/windows/native/sun/windows/awt_DesktopProperties.cpp#L536)
explicitly describes it as honoured only before XP. This implementation does
not rely on that legacy switch or write a potentially ineffective setting.
It supplies low-colour icon groups directly to XP.

The checkbox selects the resource-only variant ID
`windows-98-nt5-low-color`. It is persisted in each resource state row and the
machine's `ResourceThemePreset` marker, so the reboot reloader uses the same
icon depth. The appearance preset stays `windows-98-nt5`; the modifier is not
a third entry in the theme selector. The saved user preference is `LowColorIcons`.

The 98 preset supplies silver controls, a teal desktop background, navy-to-blue
caption colors and MS Sans Serif metrics. Its complete control palette comes
from the Windows 98 SE VM's Windows Standard scheme, including the distinct
`ButtonLight` (`223 223 223`) and black `ButtonDkShadow`. See the
[colour reference](../reference-assets/windows-98/COLORS.md). It is written to
`Control Panel\Colors` for both the interactive user and the secure logon
desktop. Apply also repairs the earlier approximate 98 palette without
recapturing the original colour baseline. When its Classic colors/fonts option
is enabled, it also sets native HKLM
`SOFTWARE\Microsoft\Windows NT\CurrentVersion\FontSubstitutes\Tahoma`
to `MS Sans Serif`. This is machine-wide and requires administrator rights and
a restart for all applications to see it. Switching to 2000, clearing Classic
colors/fonts, Revert and uninstall restore the original substitution, including
its type, bytes, or absence. The extra baseline is captured independently of
the existing baseline schema, so upgrades do not recapture other settings.

## Asset coverage and provenance

`payload/Themes/windows-98-nt5/assets.tsv` records 131 overlay files, their
donor resource IDs and SHA-256 hashes. Sources are Windows 98 and NT 5.0
Workstation build 1877, read from developer-supplied VM base disks. No VM was
booted or modified to extract them. Complete Windows binaries and disk images
are not part of the payload.

The overlay contains shell icons, compatible common-control toolbar strips,
NT Workstation Start-menu banners, NT 5.0 logon headers, System Properties
artwork/text, shell animations where available, and sounds. System Properties
text explicitly describes the beta *style* while identifying Windows XP.
The XP logon header slots use the authentic 413x72 NT Workstation header, not
the unrelated 484x280 NT startup panels. Its placement in XP's taller header
slots still needs visual testing. Toolbar images with incompatible dimensions
are deliberately omitted. Icon exports with sequential filenames were not
assumed to have matching resource IDs.

The four XP ShellAbout/winver header slots (shell32 bitmaps 130, 131, 146 and
147) explicitly use build 1877's original msgina bitmap 101: the 413x72
Windows NT Workstation 5.0 header. The bitmap bytes are preserved; no artwork
is rescaled. XP's actual version/build reporting remains intact. Resource
revision 2 forces an existing 98 installation to repatch these slots on Apply.

### Experimental 98 taskbar geometry

With the 98 preset, Classic colors/fonts and Classic Start menu selected, tick
**98 taskbar insets and wider Start (experimental)** and Apply. This independent
modifier defaults off. It adds three pixels of native Start-button width at
96 DPI (scaled at other DPIs), reserves that space in the toolbar layout, and
adds an etched divider immediately after Start and enables native toolbar
borders and notification-area insets. The running-window band stays flat. The native Start button still handles pressing and input. Its freshly painted
interior is shifted two pixels right at 96 DPI while preserving the frame and
native pressed-state offset. Native painting is buffered so only the shifted
image reaches the screen; pressed-state changes suppress native direct drawing
and synchronously repaint the completed buffer. The divider has a two-pixel gap after Start and no extra pixel before the toolbar.
It paints straight shadow/highlight columns without end caps and is restored if Explorer hides or destroys it during startup.
This is a prototype, not a verified pixel-exact recreation of every 98 taskbar.

An architecture-matched controller and DLL use a thread-scoped hook to apply
subclasses inside Explorer. There is no on-disk Explorer code patch. The
controller follows Explorer restarts and starts through the selected user's
Run key. Under cross-account Run As, it starts at that user's next sign-in.
Luna and vertical taskbars are left native; returning to a horizontal Classic
taskbar resumes the experiment. Unticking, disabling either prerequisite,
switching to 2000, Revert or uninstall signals restoration and restores the
original Run value. Original edge flags are retained by band ID.

The injected DLL stays pinned until Explorer exits, so callbacks cannot point
to unloaded code if the controller crashes. An Explorer-thread timer detects
that crash and restores geometry. Consequently, upgrading/removing a loaded
DLL can require a restart. The helper refuses OS versions outside NT 5.1/5.2
workstation. Its actual appearance, shell restart behavior and hook loading
still require XP x86/x64 VM verification before release. Special taskbars,
third-party shell replacements and right-to-left layouts are not validated.

Unmapped resources inherit Windows 2000. This first preset is not a complete
NT 5.0 reproduction: XP-only dialogs, some application icons, example images
and remaining branding still use the base conversion. The separately selectable
Explorer pane and wallpaper collection are still explicitly Windows 2000.
NT conference sound mappings use `ding.wav` for the blip and `ringin.wav` for
incoming calls; they are curated mappings, not a claim of an exact NT event
scheme. See the existing third-party notices before redistribution.

To reproduce assets, extract the desired WINDOWS and WINNT files read-only,
install `pefile` in an authoring environment, and run:

```powershell
python scripts/import-legacy-theme.py --win98 <WINDOWS-directory> --nt5 <WINNT-directory>
```

Only the importer needs Python/pefile. Building and running the patcher need
neither. Import mappings are explicit in the script; the generated manifest
documents each result.

## Implementation and extension

`src/eXPerience2KTheme.h` is the shared preset catalog. Stable string IDs, not
combo-box indices, are saved. To add another curated preset, add a catalog
entry and an overlay directory with a provenance manifest, extend the palette
data and add corresponding verification. Theme IDs are distinct from the XP
compatibility profiles and are validated against the catalog.

Assets resolve in this order:

1. Optional `Themes/windows-98-nt5/LowColor/<original asset path>`
2. `Themes/<appearance-preset-id>/<original asset path>`
3. Existing architecture-specific branding override
4. Original Windows 2000 payload

Both the native patch path and Resource Hacker fallback use this resolver.
Sound mappings use the corresponding overlay paths. Existing resource scripts,
targets and operations remain shared, without duplicating the base payload.

Each `state.tsv` row now appends `theme_id` and `previous_crc32`. Old nine-column
rows load as Windows 2000; ten-column rows without a previous CRC are accepted.
Unknown presets and malformed rows fail closed. The reloader uses each row's
preset, independently of the signed-in user's selection. The last successfully
applied resource preset is also tracked machine-wide, while appearance choices
are saved for the interactive user.

Preset installation rebuilds resources from the saved unpatched image instead
of stacking themes on a previously themed image. The recorded pre-switch CRC
prevents a pending previous preset from being mistaken for an OS update and
copied into the backup. Genuine OS updates retain the upstream backup-refresh
behavior. Failed/skipped targets retain their recovery records, and any staging
failure makes the transaction report failure. This is not an atomic transaction:
some files can already be staged if a later operation fails. Retain recovery
data and inspect the log before retrying or reverting.

Older releases do not understand the appended state columns. Revert with this
build before downgrading; do not install the old patch engine over active preset
state. The first development artifact retains the upstream version filename
and is not a published release.

## Validation

Run the normal build and release verification, plus:

```powershell
.\scripts\test-theme-presets.ps1 -GccX86Path <x86-gcc.exe>
.\scripts\test-logon-background.ps1 -GccX86Path <x86-gcc.exe>
.\scripts\test-taskbar.ps1 -GccX86Path <x86-gcc.exe> -GccX64Path <x64-gcc.exe>
```

These cover legacy/current state parsing, unknown-preset rejection, overlay
precedence, fallback to 2000, original versus pending/update CRC classification,
and actual resource patching of disposable PE files with icon-byte verification.
The winver DIB is also checked byte-for-byte across preset switches. The
taskbar suite uses isolated native controls in each architecture to test width,
non-overlap, repeated layout, added bands, edge restoration, vertical suspension
and controller-death cleanup. It does not attach to the developer's taskbar.
The isolated registry suite covers MS Sans Serif metrics and repeated
apply/restore with an absent or REG_EXPAND_SZ Tahoma substitution. Registry roots
are redirected to temporary test keys; the host is not themed. Build verification
also checks that the installer itself is x86, even with a 64-bit NSIS default.

Still required before an upstream release: XP x86 and x64 visual checks, Apply,
reboot/reloader persistence, reopen, 2000-to-98-to-2000, feature clearing, Revert,
uninstall, interrupted staging and genuine OS-update scenarios. Unit/integration
checks and PE version fields do not establish working XP runtime compatibility.
