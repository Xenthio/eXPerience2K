# Windows 98 colour reference

`colors.tsv` is the full 29-colour **Windows Standard** appearance scheme
read from `WINDOWS/USER.DAT` in the developer-supplied **Windows 98 SE** VM
(`Windows 98.vmdk`). The disk was opened read-only with 7-Zip. The original
disk and hive were not modified; no private registry contents are included.

- Donor USER.DAT SHA-256:
  `41ca73429def5300d1c0329163dc69113f1fe99f12a97146a49843f7cecd8b33`
- Extracted 520-byte scheme SHA-256:
  `6ea0b7a3210127f8227feb1cbee224ca01a043b7f03521c2faa97c4da554b9e0`

The CREG REG_BINARY value named `Windows Standard` contains a version-4
appearance structure. Its 29 COLORREF entries begin at byte 404. The TSV
retains the raw DWORD, including any high-byte palette flags; `rgb` records
only the red, green and blue channels used by `Control Panel\Colors`.
The system-colour index/name mapping can also be checked against the
[ReactOS display panel source](https://github.com/reactos/reactos/blob/master/dll/cpl/desk/theme.c).

Reproduce without loading the hive into the host registry:

```powershell
python scripts/extract-win98-colors.py <copied-USER.DAT> reference-assets/windows-98/colors.tsv
```

The source's important bevel values are ButtonFace `192 192 192`,
ButtonLight `223 223 223`, ButtonHilight `255 255 255`, ButtonShadow
`128 128 128`, and ButtonDkShadow `0 0 0`. HotTrackingColor is `0 0 255`.

The NT 5.0 build 1877 base disk's saved user palette was also inspected:
its ButtonLight is `192 192 192` and HotTrackingColor is `0 0 128`.
Those NT values are deliberately not substituted for the Windows 98 ones.

The donor scheme stores solid active/inactive caption endpoints. The app's
independent **Blue Gradient** option deliberately overrides the active endpoint
with `16 132 208`; **Solid Navy** retains the donor's `0 0 128`. All other
98 colours come from the explicit table, not from recolouring the 2000 table.
