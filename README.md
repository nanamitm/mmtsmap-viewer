# mmtsmap-viewer

A small Qt6/C++ GUI for inspecting the `.mmtsmap` sidecar index that
`dantto4k` generates and the `mmts-dsfilter` DirectShow splitter consumes
(the index that accompanies decrypted Japanese 8K/4K MMT/TLV `.mmts`
recordings).

It parses all three on-disk variants and shows their contents:

| Magic        | Format            | Notes                              |
|--------------|-------------------|------------------------------------|
| `MMTSMAP3`   | binary v3         | tracks carry `audioMode`/`channels` |
| `MMTSMAP2`   | binary v2         | no `audioMode`/`channels`           |
| `MMTSMAP 1`  | text              | line-based                          |

The parser here is self-contained (only Qt, no dependency on dantto4k or the
DirectShow filter). The binary layout mirrors `dantto4k`'s
`MmtsMapWriter::writeBinary` and the reader in `MmtTlvSplitter::LoadSidecarMap`.

## What it shows

- **Summary**: format/version, source size, duration, first/last video PTS,
  and table counts.
- **Timeline**: time → byte-offset plot of RAP points (green), seek points
  (blue line) and MPT changes (orange) — the seek curve and index density at a
  glance. Hover for the time/offset under the cursor.
- **Audio Timeline**: one horizontal lane per distinct audio track, with bars
  over the program-time intervals where that track is present in the MPT. Makes
  audio-track transitions (a language/track added or removed, channel-mode
  change) visible at a glance. Lane labels include sampling rate and **channel
  count** (`5.1ch`, `22.2ch`, …) — the channel data is only present in
  `MMTSMAP3`; older v2/text maps show no channel count.
- **Tables**: Tracks, MPT Changes (audio/subtitle split with a per-row
  `+`/`−` change summary), RAP Points, Seek Points.

All times shown are relative to the first video PTS (a 0-based program
timeline), not the raw absolute PTS stored in the file.

## Samples

`samples/synthetic_v3.mmtsmap` is a tiny hand-built `MMTSMAP3` file (5.1ch /
2.0ch / 22.2ch audio with two audio-track transitions) for exercising the v3
channel column without a real recording.

## Build

```powershell
cd F:\VTemp\codex\mmtsmap-viewer
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:\Qt\6.11.1\msvc2022_64
cmake --build build-msvc --config Release
# Run: build-msvc\Release\mmtsmap-viewer.exe [path\to\recording.mmtsmap]
```

## Releases

Pushing a `v*` tag (e.g. `v0.1.0`) triggers the
[Release (Windows)](.github/workflows/release-windows.yml) workflow, which
builds an x64 binary, bundles the Qt runtime with `windeployqt`, and attaches a
ready-to-run `.zip` to the GitHub Release. The same workflow can also be run
manually from the Actions tab (`workflow_dispatch`).

## Use

`File > Open` (Ctrl+O), drag & drop a `.mmtsmap` onto the window, or pass a path
on the command line.
