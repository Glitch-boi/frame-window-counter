# Frame Window Counter

A comprehensive frame window tracking, HUD customization, and $L^*$ (NaNDL Precision) calculation toolkit for **Geometry Dash** (Geode).

## Features

### Frame Window Tracking & Visuals
* **In-Game Markers:** Spawns animated markers on player positions during gameplay corresponding to specific frame actions and window tolerances.
* **Live HUD Statistics:** Customizable HUD overlay in the top-left corner tracking action counts across configured frame window ranges.
* **Custom Audio Effects:** Bind dedicated sound effects (`.ogg`, `.mp3`) to specific frame window presets.
* **Camera Compatibility:** Full world-to-screen matrix transformations ensuring markers properly track through rotations, zoom, and reversed gravity.

### Audio Engine (new in 1.4.0)
* **Unlimited overlap:** all sounds are mixed into a single stream instead of one FMOD channel per sound, so fast macros (thousands of inputs per second) are no longer cut off by "the next sound overwrites the previous one". There is no per-second limit.
* **Sample-accurate timing:** sounds are placed from the game's frame numbers, and inputs within one tick are spread across that tick.
* **Safe by design:** a look-ahead limiter prevents clipping, decoded sounds are cached at level start, and the old engine is used automatically if the mixer can't start (or manually via *Use Legacy Audio Engine*). *Audio Buffer Size* trades delay for stability.

### Keybinds & Quick Capture (Desktop)
* **Add 1-Frame Window:** One key press adds a 1-frame window for player 1 on the *current tick* of the level you are playing (duplicate ticks are skipped).
* **Add + Open Editor:** Same as above, then opens the Frame Window Editor scrolled to the new frame.
* **Undo Last Keybind Frame**, optional **frame offset** and optional **auto-recalculate L*** after each add.
* Every key (including the original `O`) is a normal Geode keybind setting, so you can rebind them in the mod's settings.

### Display Settings (new "Display" button in the editor)
* **Precision Size / Counter Size / Marker Size:** sliders (50% - 250%) for the bottom-left L* labels, the top-left counter HUD and the in-game circle markers.
* **HUD Opacity** slider and a **Show Markers** toggle (keep the counters, hide the circles).
* **Sound Volume** slider (0% - 100%, on top of the game's SFX volume) and a **Notifications** switch that turns off every toast the mod shows (auto-save, keybind messages).
* **Window Size** slider (60% - 140%) that scales *all* of the mod's popups. Enlarging is capped so a window can never be pushed off-screen, and at 100% nothing changes.
* Previews live: HUD changes are visible behind the popup while you drag; other open mod windows resize as you drag.

### Advanced Precision Calculator ($L^*$)
* **Multi-Dimensional Metrics:** Solves for theoretical level precision across 8 dimension combinations:
  * **Base** ($L^*$)
  * **Nerve** ($K_T$) — Time-dependent decay
  * **Fatigue** ($K_U$) — Input count-dependent decay
  * **CPS** ($K_C$) — High click-frequency penalty
  * **Composites:** N+F, N+C, F+C, and All (N+F+C)
* **Multi-Threaded Asynchronous Solver:** Fast root-finding algorithm using an error function (erfc) LUT lookup table.
* **Configurable Calculation Parameters:**
  * **Level TPS:** Target calculation rate (synced with macro FPS).
  * **Respawn Time (s):** Accounts for penalty time from death back to start.
  * **Target Solve Time (s):** Expected completion time benchmark (default: 86,400s / 24 hours).
  * **Model Constants:** Fine-tune $K_T$, $K_U$, and $K_C$ up to 16 decimal places with one-click default restore.
* **Dirty State Indication:** Automatically highlights $L^*$ values in red when frames or settings are modified without recalculating.
* **Base $L^*$ Precision Percentage:** an optional `Base L* %` display showing how much of the *final* base $L^*$ has been reached at the current input, i.e. `L*(this input) / L*(final) x 100`. It is weighted by each input's $L^*$ contribution, not by time or input count: a level with 500 $L^*$ whose first 5% of inputs already carry 450 $L^*$ reads 90% there. Shown in the bottom-left HUD and under every row of the frame list.

### Action Editor & Macro Tooling
* **Timeline Tracking:** Automatically highlights and scrolls to the active frame in real-time as the level plays.
* **Dual Player (1P/2P) Control:** Toggle individual action player assignments on the fly.
* **Import & Export:**
  * Import replay/macro files (`.gdr`, `.gdr2`, `.slc`, `.json`, `.fwc`, `.cml`).
  * Export custom binary `.fwc` and NaNDL `.json` project files.
* **Inputs per Frame (I/F) Support:** Native handling and auto-unpacking of high-frequency micro-inputs into sub-frames, seamlessly integrating with fatigue and CPS fatigue models.
* **Batch Frame Merging:** Built-in "Merge" utility to automatically aggregate duplicate same-frame inputs into a single action with accumulated I/F count.
* **Automated Safety Backups:** Background autosaves generated periodically to the Geode config directory.

---

## Project Structure

```text
src/
├── Audio/                          # Audio management & playback engine
│   ├── SoundManager.cpp
│   └── SoundManager.hpp
├── Data/                           # Global state, cache & type definitions
│   ├── State.cpp
│   ├── State.hpp
│   └── Types.hpp
├── Hook/                           # Geode game hooks & lifecycle modifications
│   ├── CCDirectorHook.cpp          # Scene loop, autosave & editor live-tracking
│   ├── KeybindHooks.cpp            # Rebindable keybinds: open editor, add frame, add + open, undo
│   ├── EndLevelLayerHook.cpp       # Level completion & end screen handling
│   ├── PauseLayerHook.cpp          # Pause menu UI integration
│   └── PlayLayerHook.cpp           # HUD rendering, marker spawning & game ticks
├── IO/                             # File importing/exporting logic (.fwc, .gdr, .slc, .cml)
│   ├── CmlParser.cpp / .hpp        # xdBot compressed macro parser (v1-v7)
│   ├── FileIO.cpp
│   └── FileIO.hpp
├── Math/                           # L* mathematical models & multi-threaded solver
│   ├── Calculator.cpp
│   ├── Calculator.hpp
│   ├── DisplayMath.hpp             # Pure helpers (Base L* %, slider mapping, popup fit) - unit tested, no Geode deps
│   └── HudTally.hpp                # Fast preset matching / recounting - verified identical to the naive loops
├── UI/                             # Custom Geode popups and editors
│   ├── AddFramePopup.cpp / .hpp
│   ├── DisplaySettingsPopup.cpp / .hpp
│   ├── FrameActionPopup.cpp / .hpp
│   ├── LabelPresetPopup.cpp / .hpp
│   ├── LStarCalcSettingsPopup.cpp / .hpp
│   ├── MobileAudioPickerPopup.cpp / .hpp
│   ├── MobileExportPopup.cpp / .hpp
│   ├── MobileImportPopup.cpp / .hpp
│   ├── PopupScale.cpp / .hpp       # Applies the "Window Size" setting to every popup
│   ├── PrecisionSettingsPopup.cpp / .hpp
│   └── WindowPresetPopup.cpp / .hpp
└── Common.hpp                      # Global constant definitions & math helpers
```

---

## Hotkeys & Controls

All keybinds are desktop-only and can be changed in the mod's settings (Geode keybind settings).

| Default | Setting | Action |
| :--- | :--- | :--- |
| **`O`** | Open / Close Editor | Toggle Frame Action Editor & close active submenus (anywhere in the game) |
| **`I`** | Add 1-Frame Window | Add a 1-frame, 1P window on the current tick of the level |
| **`U`** | Add 1-Frame Window + Open Editor | Same as `I`, then open the editor scrolled to the new frame |
| **`Y`** | Undo Last Keybind Frame | Remove the most recent frame added by `I` / `U` |
| **Time Icon** | Pause Menu (Right side) | Open Frame Action Editor |

Related settings: **Keybind Frame Offset** (added to the current tick, default 0) and **Auto-Recalculate L\* After Keybind** (default off). Keybind messages follow the **Notifications** switch in Display Settings. Keys are ignored while a text box is focused.

---

## How to Use

### 1. Frame Window Presets & Labels
1. Open the **Windows** or **Labels** popup from the bottom bar of the main editor.
2. Configure **Min/Max Frame Windows**, custom HUD label text, colors, and audio file triggers.
3. Enable **Show in HUD** to display a live tally during gameplay.

### 2. Importing & Editing Frames
1. Click **Import** in the main editor to load a macro or replay file (`.gdr`, `.gdr2`, `.slc`, `.json`, `.fwc`, `.cml`).
2. Edit frame numbers, frame window tolerances, or switch between **1P** and **2P** modes.
3. Use **Activate All** / **Inactivate All** to bulk toggle drawing and HUD inclusion.

### 3. Display Settings
1. Click **Display** in the bottom bar of the main editor.
2. Drag the sliders for **Precision Size**, **Counter Size**, **Marker Size**, **HUD Opacity** and **Window Size**. Values snap to 5% steps and are saved automatically.
3. **Show Markers** hides/shows the in-game circles without touching the counters. **Reset Defaults** restores everything.

### 4. Calculating Precision ($L^*$)
1. Click the **Options** gear icon in the top-right of the Frame Editor to open the **Precision Display** popup.
2. Toggle your desired metrics (Base, Nerve, Fatigue, CPS, etc.).
3. *(Optional)* Click the top-right gear in the Precision Display to open **L* Calc Settings** to configure TPS, respawn time, target solve time, or model constants.
4. Toggle **Base L\* %** to also show the Base $L^*$ percentage (bottom-left HUD and next to each frame in the list).
5. Click **Start Calculation**. Once finished, cycle through metrics on the top status button or view live values in the bottom-left corner during level runs.

---

## Building from Source

### Prerequisites
* [Geode CLI](https://github.com/geode-sdk/cli) installed and configured.
* **Clang** compiler with C++20 support (LLVM / Clang-cl).
* CMake 3.27+.

### Build
   ```bash
   geode build
   ```

### Running the tests (no game needed)
The logic that does not depend on Geode/FMOD is header-only and covered by tests:
   ```bash
   g++ -std=c++20 -Wall -Wextra -o displaymath_test tests/displaymath_test.cpp && ./displaymath_test      # Base L* %, sliders, popup fit
   g++ -std=c++20 -O2 -Wall -Wextra -o hudtally_test tests/hudtally_test.cpp && ./hudtally_test            # HUD counting == original loops
   g++ -std=c++20 -O2 -Wall -Wextra -pthread -o audiomixer_test tests/audiomixer_test.cpp && ./audiomixer_test   # mixer, 12k sounds/s, timing, threads
   ```
`audiomixer_test` also builds with `-fsanitize=thread` and `-fsanitize=address,undefined`.
