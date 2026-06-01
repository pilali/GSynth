# GSynth — JUCE build (VST3 / AU / Standalone)

This directory wraps the **same DSP core** as the LV2/MOD plug-in
(`../src/gsynth_dsp.{c,h}`) in a JUCE `AudioProcessor`, producing desktop
plug-in formats. The engine is byte-for-byte the LV2 engine — only the host
glue differs.

## What's here

| File | Role |
|------|------|
| `CMakeLists.txt`    | Build definition; fetches JUCE automatically. |
| `PluginProcessor.h/.cpp` | `AudioProcessor` + `AudioProcessorValueTreeState`. Maps the 13 controls onto `GSynthParams` and calls `gsynth_dsp_process`. |

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

### macOS notes (AU + VST3)
- AU is macOS-only; build on a Mac (or CI with a macOS runner).
- For distribution outside your own machine you need codesigning + notarization
  (Apple Developer ID). Locally, unsigned builds run fine.
- Validate the AU with `auval -v aufx Gsyn Plli`.

### Linux notes
A local GUI build needs the usual JUCE system packages, e.g. on Debian/Ubuntu:
```sh
sudo apt install libasound2-dev libfreetype-dev libx11-dev libxext-dev \
                 libxinerama-dev libxrandr-dev libxcursor-dev libgl1-mesa-dev
```
(`libxinerama-dev` and `libgl1-mesa-dev` are the ones most often missing.)

## Status / next step

- ✅ Processor + parameters wired to the shared core; **generic** parameter UI.
- ⬜ Custom editor reproducing the MOD modgui fader layout (step 2).
