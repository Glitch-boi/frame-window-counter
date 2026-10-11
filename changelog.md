## v1.4.0
- **New audio engine.** Every sound used to get its own FMOD channel, capped at 6 overlapping copies per sound - the 7th cut the oldest, which is why fast macros sounded like each sound "overwrote" the previous one. Sounds are now added into **one** software-mixed stream: unlimited overlap, nothing is stolen, and each sound costs one lock-free queue push instead of ~10 FMOD calls.
  - Sounds are placed on a sample-accurate timeline from the game's frame numbers; inputs inside one tick are spread across that tick instead of stacking into a single click.
  - A look-ahead limiter keeps hundreds of overlapping sounds from clipping. New **Sound Volume** slider.
  - 12,000 sounds/second costs roughly 2-13% of one CPU core depending on how long the clip is (tested). There is **no** per-second limit; only a very high safety cap on simultaneous voices that normal use never reaches.
  - Falls back to the old engine automatically if the mixer stream can't start, or manually via **Use Legacy Audio Engine**. **Audio Buffer Size** trades delay against crackling.
  - Sounds are decoded at level start (no hitch on the first hit) and missing files are no longer re-checked on disk for every sound.
- **Faster HUD at high input rates.** Recounting the HUD after a death/checkpoint was O(actions x presets) (about 0.4 s for a 60 s, 12,000-input/s macro); it is now ~5 ms. Per-tick preset matching is cached, marker text is only built when a marker is really drawn, and unchanged counters are no longer re-rendered. Results are verified identical to the old code.
- **Fixed: Display Settings sliders were drawn in the wrong place** (shifted up and to the right, sticking out of the window). GD's `Slider` is a screen-sized layer, so scaling it moved it away from its row; sliders now live in a scaled holder and sit exactly on their rows.
- **New: Notifications switch** in Display Settings. Turns off every toast of the mod (auto-save and keybind messages). It replaces the old keybind-only "Keybind Notifications" setting.
- HUD changes while dragging a slider are applied once per frame instead of once per drag event.

## v1.3.0
- **New keybinds** (desktop, rebindable in settings): add a 1-frame window on the current tick (`I`), add one *and* open the editor at it (`U`), undo the last keybind frame (`Y`). Duplicate ticks are skipped; optional frame offset, notifications and auto-recalculate.
- The editor key (`O`) is now a normal rebindable Geode keybind setting (default unchanged).
- **Display Settings** popup (new **Display** button in the editor): sliders for precision-label size, counter-label size, marker size, HUD opacity and window size (all popups), plus a Show Markers toggle and Reset Defaults.
- **Base L\* %**: optional precision percentage for Base L\* only, weighted by each input's L\* contribution. Shown in the HUD and under every frame in the editor list.
- Precision Display popup is slightly taller to fit the new toggle.
- Fixed: the counter "pop" animation would have reset labels to a fixed size; it now follows the counter size setting.
