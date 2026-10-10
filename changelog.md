## v1.3.0
- **New keybinds** (desktop, rebindable in settings): add a 1-frame window on the current tick (`I`), add one *and* open the editor at it (`U`), undo the last keybind frame (`Y`). Duplicate ticks are skipped; optional frame offset, notifications and auto-recalculate.
- The editor key (`O`) is now a normal rebindable Geode keybind setting (default unchanged).
- **Display Settings** popup (new **Display** button in the editor): sliders for precision-label size, counter-label size, marker size, HUD opacity and window size (all popups), plus a Show Markers toggle and Reset Defaults.
- **Base L\* %**: optional precision percentage for Base L\* only, weighted by each input's L\* contribution. Shown in the HUD and under every frame in the editor list.
- Precision Display popup is slightly taller to fit the new toggle.
- Fixed: the counter "pop" animation would have reset labels to a fixed size; it now follows the counter size setting.
