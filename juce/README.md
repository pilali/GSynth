# GSynth — JUCE build (VST3 / AU / Standalone)

This directory wraps the **same DSP core** as the LV2/MOD plug-in
(`../src/gsynth_dsp.{c,h}`) in a JUCE `AudioProcessor`, producing desktop
plug-in formats. The engine is byte-for-byte the LV2 engine — only the host
glue differs.

## What's here

| File | Role |
|------|------|
| `CMakeLists.txt`    | Build definition; fetches JUCE automatically. macOS builds are universal (arm64 + x86_64) by default. |
| `PluginProcessor.h/.cpp` | `AudioProcessor` + `AudioProcessorValueTreeState`. Maps the 13 controls onto `GSynthParams` and calls `gsynth_dsp_process`. |
| `PluginEditor.h/.cpp` | Custom editor + `LookAndFeel` reproducing the MOD modgui look (dark pedal, 11 vertical faders + 2 toggle switches in VOICE MIX / FILTER / MISC groups). |

The DSP is monophonic (guitar in): inputs are summed to mono, processed once,
and fanned out to all output channels.

Parameters mirror the LV2 ports exactly (same IDs/symbols, ranges, defaults,
and the two enums — *Filter Type* and *Pitch Track*), so automation and presets
line up with the LV2 build.

## Build

Requires CMake ≥ 3.22 and a C++17 toolchain. JUCE is fetched on first
configure (override the version with `-DGSYNTH_JUCE_TAG=<tag>`, or point at a
local checkout with `-DGSYNTH_JUCE_DIR=/path/to/JUCE` to build offline).

```sh
cd juce
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Artifacts land under `build/GSynth_artefacts/Release/`:

- **macOS** → `AU/GSynth.component`, `VST3/GSynth.vst3`, `Standalone/GSynth.app`
- **Windows** → `VST3/GSynth.vst3`, `Standalone/GSynth.exe`
- **Linux** → `VST3/GSynth.vst3`, `Standalone/GSynth`

By default (`GSYNTH_COPY_AFTER_BUILD=ON`) the build also installs them into the
per-user plug-in folders so hosts and `auval` find them right away:
`~/Library/Audio/Plug-Ins/Components` (AU) and `.../VST3` (VST3) on macOS,
`~/.vst3` on Linux, `%COMMONPROGRAMFILES%\VST3` on Windows. Pass
`-DGSYNTH_COPY_AFTER_BUILD=OFF` for CI/packaging builds.

### macOS notes (AU + VST3)
- AU is macOS-only; build on a Mac (or CI with a macOS runner).
- For distribution outside your own machine you need codesigning + notarization
  (Apple Developer ID). Locally, the ad-hoc signature JUCE applies is enough.
- Validate the AU with `auval -v aufx Gsyn Plli`. If it reports "didn't find
  the component" right after a build, refresh the AU cache:
  `killall -9 AudioComponentRegistrar` then re-run `auval`.

### Linux notes
A local GUI build needs the usual JUCE system packages, e.g. on Debian/Ubuntu:
```sh
sudo apt install libasound2-dev libfreetype-dev libx11-dev libxext-dev \
                 libxinerama-dev libxrandr-dev libxcursor-dev libgl1-mesa-dev
```
(`libxinerama-dev` and `libgl1-mesa-dev` are the ones most often missing.)

## Continuous integration

`.github/workflows/build.yml` builds on every push to the `JUCE` branch (and on
tags / manual dispatch): **macOS universal** AU/VST3/Standalone and **Windows**
VST3/Standalone, uploaded as downloadable artifacts. No local Windows machine
needed.

## Status

- ✅ Processor + parameters wired to the shared core.
- ✅ Custom editor mirroring the MOD modgui (faders + toggle switches + groups), natively drawn.
- ✅ macOS universal binaries + Windows builds via CI.
- ⬜ Code-signing / notarization for distribution (Apple Developer ID).
