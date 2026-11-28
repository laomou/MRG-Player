# MRG Player

MRG Player is a standalone SDL2 game launcher and multi-platform runtime foundation. Emulation is provided by libretro cores, so NES, Game Boy Advance, Super NES, Game Boy, and Mega Drive games all run through the cores installed on the machine, and the front end stays independent of any specific emulator: everything above the runtime interface is written once against the runtime registry.

The project remains independent from MRG-OS-clean. It ships no emulator implementation of its own: the libretro ABI is the only contract between the launcher and a console core.

## Features

- Native 1280×720 resizable, high-DPI launcher UI with fullscreen support
- English and Simplified Chinese UI
- Platform-neutral game model, game library index, runtime interface, and runtime registry
- Dynamic platform and file-type labels in the library and management UI
- Persistent game library that indexes ROMs where they lie, with click-to-import, drag-and-drop, duplicate detection, and Switch-style management
- Box art per game in the card's own `Imgs/<system>/` folder, the file the stock launcher shows, so art dropped in by hand and art the launcher fetches are the same picture; map a local title to a libretro-thumbnails name in `cover-names.json` to make lookups hit, and cards without art keep the plain navy card with a console badge while the selected game's art becomes the blurred background
- Console-agnostic runtime: video, audio, input, pacing, battery saves, and controller hot plugging are handled once for every core
- In-game pause menu with three persistent instant-state slots, console reset, and a confirmed power cycle (cold boot that keeps battery saves)
- Slot preview cards with instant-state thumbnails, save time, and the play time at capture
- 4× fast-forward while Tab or the controller's right shoulder button is held, with a configurable audio policy (silent, or audible without backlog)
- Half-speed slow motion and single-frame stepping from the pause menu
- Rewind: holding Backspace steps the machine back through a rolling window of the core's own snapshots; every pad button is a console input, so a handheld leaves the setting out
- Optional runahead (0-3 frames) that runs the core past the picture on screen to hide the core's own input lag
- Display options: square, 4:3, or 8:7 aspect ratio, integer scaling, and 0-16 px overscan crop, globally or per game
- One accent colour for the whole launcher — teal by default, five more in **Settings → Theme** — so the interface never borrows a game's palette
- Core options: every option a libretro core declares is collected and editable from the pause menu, saved per core
- In-game screenshots: **F12** writes a PNG of the current picture to the data root's `screenshots/` directory
- Audio options: master volume, a mute toggle with a keyboard hotkey, and the fast-forward audio policy
- Persistent game metadata: favorites, custom titles, total play time, and last-played time
- Configurable keyboard/controller bindings, editable per port, with turbo A/B autofire, plus an analog-stick dead zone
- libretro back end for NES, GBA, SNES, Game Boy, and Genesis: core discovery, `MRG_CORES_DIR` override, `.so.zip` unpacking, isolated `SDL_LoadObject` loading, and software framebuffer handling for 0RGB1555, RGB565, and XRGB8888 cores
- Core installer in **Settings → Cores**: downloads the current nightly build of the preferred core per platform and installs it into `cores/`
- Instant states come from the core's own serialization, wrapped in a versioned header that records the core name
- Import of `.nes`, `.gba`, `.sfc`/`.smc`/`.snes`, `.gb`/`.gbc`, and `.md`/`.gen` files, each validated for its own platform before the library lists it
- ROM folder scanning: on a build without a file dialog, the folders named by `MRG_ROM_DIRS` are imported on first start, and the Home import action rescans them; without that variable a desktop build reads the card layout, `Roms/<system>` beside `Imgs/<system>` under the data root and beside the executable, and still reads an older `roms/`
- Identical front-end features for every platform: pause menu, instant-state slots with thumbnails, fast-forward, slow motion, rewind, runahead, frame stepping, screenshots, turbo, display overrides, and mute

Every platform needs a libretro core installed on the machine — see [libretro cores](#libretro-cores). The project bundles no cores, ROMs, or BIOS files, and reports a clear error instead of guessing when no suitable core is found. Use only game images that you are legally entitled to use.

## Architecture

```text
src/
├── app/                     # SDL lifecycle, navigation, and launch options
├── config/                  # Settings, input bindings, data roots, and migration
├── game/                    # Platform descriptors, detection, and validation
├── i18n/                    # English and Simplified Chinese catalog
├── library/                 # Game library: metadata and ROM paths
├── model/                   # Launcher card style metadata
├── platform/                # Optional native file picker
├── runtime/                 # IGameRuntime, RuntimeRegistry, video geometry
│   └── libretro/            # Core discovery, zip unpacking, core loader, adapter
└── ui/                      # Drawing primitives and screens
```

Device-side assets live beside the code, one directory per target:

```text
platforms/trimui/           # everything that ships to a TrimUI handheld
├── pak/                    # the pak skeleton: config.json, launch.sh, icon.png, cores/
└── emus/                   # optional Emus/<SYS> tiles for the stock desktop
```

One pak serves the whole TrimUI line: the Brick Pro and the Smart Pro only differ in panel size, which the
canvas already takes from the build (`MRG_CANVAS_WIDTH` / `MRG_CANVAS_HEIGHT`) rather than from the pak.

`IGameRuntime` defines the video/audio/input lifecycle used by the application. `RuntimeRegistry` maps `GameEntry::platform` to `LibretroRuntime`, which opens a matching core through `SDL_LoadObject` and adapts it to that interface. `third_party/libretro/libretro.h` is the vendored upstream header, so the ABI matches the cores it loads.

The layout has two halves, and only one of them is yours to edit:

```text
<card>/                       # the open half: the stock launcher reads it too
├── Roms/FC/, Roms/SFC/, ...   # the games, one folder per system
└── Imgs/FC/<title>.png       # box art beside them, the file the stock launcher shows

<data root>/                  # everything the launcher manages
├── settings.ini
├── cores/                    # libretro cores (.so/.dylib/.dll, or their .zip)
│   └── unpacked/             # cores extracted from a .zip
├── system/                   # core system and BIOS files
├── library/
│   ├── metadata.tsv          # path, favorites, titles, play time, display overrides
│   └── cover-names.json      # local title to libretro-thumbnails name, for lookups
├── saves/FC/                 # one .srm battery save per game
├── states/FC/                # three instant-state slots per game
├── core-options/             # one `<core>.tsv` of chosen values per core
├── screenshots/              # F12 captures
└── rom-cache/                # a one-run ASCII copy of a non-ASCII ROM name
```

Every platform folder uses the card's name -- `FC`, `SFC`, `MD`, `GB`, `GBA` -- so one system has
one name everywhere a player can see it. A desktop build is the same two halves with the data root
standing in for the card, so a folder copied off a device opens as it left it and one cover file
serves both. A tree named `roms/` next to the executable or the data root is still scanned for
older setups.

[docs/paths.md](docs/paths.md) spells out which folder is whose, who writes each one, and what
deleting it costs.

Library metadata is keyed by platform and stable content ID, and each row also stores the path of the ROM. The library is an index, not a copy: a game is read from the file the player put on the card, and removing it from the library never deletes that file. A game that is moved or renamed is picked up again by the next scan, which matches the same bytes to the same entry, so favorite, custom-title, play-history, and per-game display data survive removing and later reimporting the same game. `metadata.tsv` is written in a versioned format that also holds each game's optional display override; files written by older builds still load and are rewritten in the current format on the next save.

## Build and run

```bash
git submodule update --init --recursive
cmake -S . -B build
cmake --build build -j
./build/mrg-player
```

An unset `CMAKE_BUILD_TYPE` defaults to `Release` (`-O3 -DNDEBUG`), because the emulation cores are far slower unoptimised and audio has to keep real time. Pass `-DCMAKE_BUILD_TYPE=Debug` when debugging; nothing in the code depends on `NDEBUG`.

SDL2 and SDL2_ttf are pinned Git submodules. The bundled `DroidSansFallbackFull.ttf` CJK font is Apache-2.0 licensed; its license is beside the font. A native Linux/X11 build typically needs `libx11-dev` and `libxext-dev`. On Linux, the optional native picker uses `zenity`; drag-and-drop remains available without it.

Windows builds with MSVC or MinGW through the same CMake invocation; install zlib (for example with vcpkg) so `.zip` cores can be unpacked, otherwise the installer reports that it cannot unpack the archive. SDL2 has neither an HTTP client nor a file dialog, so the launcher uses the Win32 common dialog for imports and WinHTTP for core downloads and needs no curl on the machine. Cores are loaded with `SDL_LoadObject`, which wraps `LoadLibrary` there, and `cores/` beside `mrg-player.exe` is searched as a portable layout. Windows, macOS (x86-64 and arm64), and Linux (x86-64 and aarch64) each resolve their own buildbot directory.

### Releases

Pushing a tag that starts with `v` (for example `git tag v1.0.1 && git push origin v1.0.1`) makes `.github/workflows/release.yml` build and publish that tag:

- `mrg-player-<version>-linux-x86_64.tar.gz`, unpacked into `bin/` and `share/` — the binary, the fonts, and nothing else
- `mrg-player-<version>-windows-x64.zip`, the same layout with `mrg-player.exe` in `bin/`
- `mrg-player-<version>-macos-arm64.zip`, containing `mrg-player.app`
- `SHA256SUMS.txt` covering all three

The tag without its leading `v` is passed in as `MRG_VERSION_STRING`, so the About page of a packaged build reports the version it was cut from; a plain checkout keeps the project version from `CMakeLists.txt`. zlib, `libstdc++`, and the C library are the only shared libraries an archive needs.

## Game library

Click **+ Game Library** to choose a game file, or drag one onto the window. The extension selects the platform, then that platform's validation runs before the game is listed: NES images must carry an iNES or NES 2.0 header, other platforms are checked for the 64 MiB size limit and validated by the core when they start. The file itself stays where it is -- nothing is copied into the data root, so a card holds one copy of each game. Content hashes provide stable game identities and duplicate detection. A dropped game starts after import; a game selected through the picker is added without auto-starting.

On Linux the root is normally `~/.local/share/MRG/MRGPlayer/`, obtained from `SDL_GetPrefPath("MRG", "MRGPlayer")`, and a device build points `MRG_DATA_DIR` at the card instead. It holds what the launcher manages: `library/`, `saves/`, `states/`, `system/`, `core-options/`, `cores/` and `rom-cache/`. The games and their art sit outside it, in `Roms/<system>/` and `Imgs/<system>/` on the card -- `MRG_CARD_ROOT` names it, and on a desktop the data root stands in for the card.

### Folders on a card

A device build has no file dialog, so the folders named by `MRG_ROM_DIRS` are the way in. The
launcher reads the layout a stock card already uses and lets the folder name the platform:
`Roms/FC/`, `Roms/SFC/`, `Roms/MD/`, `Roms/GB/`, and `Roms/GBA/` (also accepted: `NES`,
`SNES`, `GENESIS`, `MEGADRIVE`, `GBC`, and a trailing qualifier such as `FC (FCEUmm)`). So a
`.bin` under `Roms/MD/` is a Mega Drive game, a `.md` under `Roms/SFC/` is a SNES game, and a
`.nes` outside any of those folders still counts on its extension alone. The files a ROM set ships
beside its games — `README.md`, `license.txt`, `CHANGELOG.md` and the like — are never imported,
even when the extension says they could be: `.md` is both a Mega Drive ROM and a Markdown document.
The card badges on the shelf wear the same short names, `FC`, `SFC`, `MD`, `GB` and `GBA`, so a
game looks the same whether it came from `Roms/FC` or from a desktop tile that launched it.

A game dropped straight into the top of `Roms/` is filed into its system folder the first time a
scan sees it, so `Roms/F1赛车.nes` becomes `Roms/FC/F1赛车.nes` and the card reads the same on
every machine. Only the top of the tree is tidied: a folder you made yourself (`Roms/My Stuff/`),
a game already inside a system folder, documentation such as `Roms/README.md`, and a name that
already exists in the target folder are all left exactly where they are.

Select a populated card and press **M** or keyboard **X**, controller **X**, or right-click to open management. The menu can start the game, toggle its favorite status, display metadata, remove the game from the library, or delete its battery save. Removing a game only forgets the library entry: the ROM stays on the card and the save, favorite, title, and play time are kept, so adding the same file again picks up where it left off. Deleting save data retains the game. Destructive confirmations default to **Cancel**. Favorite games sort before regular games, and play time excludes time spent in the pause menu.

### Box art

Box art lives in one place on the card: `Imgs/<system>/<title>.png` (or `.jpg`) beside the `Roms/`
tree, with the system folder named the way the card names it, so `Imgs/FC/F1赛车.png` covers
`Roms/FC/F1赛车.nes`. That is exactly the file the stock launcher shows, so art dropped in by hand,
shipped with an image pack, or fetched here is the same picture instead of two that can disagree.

Most people have no art on disk, so the launcher can fetch it. It reads `library/cover-names.json`,
which is created with three examples and never overwritten:

```json
{
  "F1赛车": "F-1 Race (Japan)",
  "超级马里奥": "Super Mario Bros. (World)",
  "俄罗斯方块": "Tetris (USA)"
}
```

The value is the No-Intro name used by [libretro-thumbnails](https://github.com/libretro-thumbnails),
without `.png`. Matching ignores case and surrounding spaces. When a title is not in the file the
launcher tries the title itself and then the title with `(World)`, `(USA)`, `(Europe)`, and `(Japan)`
appended, so an English-named ROM often works with no mapping at all. A mapping value may also be a
full `https://` URL, which is fetched as-is.

Downloads run one at a time in the background, only from the platform's thumbnail repository, and
stop at 8 MiB. A failure is silent: the card keeps its gradient and the next launch tries again. A
success is written to `Imgs/<system>/<title>.png` beside the ROMs, so the stock launcher shows it too
and later launches never touch the network. The selected game's art is also blurred into the
launcher background; without it the background uses the selected console's palette.

### Starting a ROM from the command line

A stock desktop starts an emulator by handing it the ROM the player picked, so `mrg-player`
accepts that same shape:

```bash
./mrg-player /mnt/SDCARD/Roms/FC/game.nes        # what an Emus launch.sh passes
./mrg-player --rom=/mnt/SDCARD/Roms/FC/game.nes
./mrg-player --rom /mnt/SDCARD/Roms/FC/game.nes
```

A ROM given this way is the whole session: the library never opens, the game starts immediately,
and leaving it (pause menu → **Return Home**) ends the process, which is how the desktop gets its
screen back. The platform comes from the path exactly as it does for an import, so `Roms/FC/` still
means NES, and the game keeps the content-hash identity an import would assign — a ROM that is also
in the library shares one save file, one cover and one play-time record either way. A missing file
or a platform without an installed core prints the reason on stderr and exits with status 1.

### Launcher tiles instead of RetroArch

The stock desktop does not scan `Roms/`: it lists whatever `Emus/<SYS>/config.json` declares, one
tile per package. `platforms/trimui/emus/` holds a package for each system this build has a core for, so a card
can show games directly:

```bash
cp -r platforms/trimui/emus/* /mnt/SDCARD/Emus/
```

A package is three files. `config.json` carries the label, the ROM folder, the extension list and the
theme colour the desktop uses, all copied from the stock packages. `icon.png` is the tile art.
`launch.sh` is what replaces RetroArch:

```sh
SDCARD=${SDCARD_PATH:-/mnt/SDCARD}
exec "$SDCARD/Apps/MRG.pak/launch.sh" "$*"
```

The desktop hands the ROM over as a bare path, which is the shape described above, so the tile starts
that one game and the player exits when the game is left — the desktop gets its screen back without a
frontend in between.

| Package | Tile lists | Starts |
|---|---|---|
| `FC` | `Roms/FC` (`.nes`, `.fds`, `.zip`) | FCEUmm |
| `SFC` | `Roms/SFC` (`.sfc`, `.smc`, `.zip`) | Snes9x |
| `MD` | `Roms/MD` (`.md`, `.gen`, `.bin`, `.zip`) | Genesis Plus GX |
| `GB` | `Roms/GB` (`.gb`, `.gbc`, `.zip`) | mGBA |
| `GBA` | `Roms/GBA` (`.gba`, `.zip`) | mGBA |

Only these five are packaged on purpose: a system without an installed core would list its ROMs and
then refuse to start them. One caveat while replacing a stock install: if the card still holds the
stock `Emus/<SYS>` package, both tiles appear, so delete the one you replaced.

## Controls

### Launcher

| Action | Keyboard | Controller |
|---|---|---|
| Move between the pills and the shelf | Up / Down | D-pad up / down |
| Browse carousel | Left / Right | D-pad / left stick |
| Page the shelf by a screenful | Page Up / Page Down | L / R |
| Import game | Click **+ Game Library** or drag a file; without a file dialog the ROM folders are rescanned | Focus the library pill, A |
| Start selected game | Enter / Space or click | A |
| Open management | M / X / right-click | X |
| Remove confirmation | Delete | Management menu |
| Open settings | Click **? Settings** | Focus the settings pill, A |
| Quit the launcher | Esc, then confirm | B on a handheld; B, then confirm on a desktop |
| Fullscreen | F11 | — |

### Key hints

The strip along the bottom of a screen is a button row that never takes focus. The direction keys stay on the content — cards, rows, the category rail — and the strip is always drawn idle, with no focus ring anywhere on it. The pointer can still click either entry and runs it on the spot, which is the only way to press one. Each entry is one key plus one short word; the key is drawn as a filled badge, the word is plain text with no plate or outline around it, so the bottom of the screen never grows a second row of boxes. The word drops the noun the screen already shows, so the shelf reads `S OPTIONS` next to `ENTER CONFIRM` on the keyboard and `X OPTIONS` on a pad (the selected card is the game) and the core page reads `ENTER INSTALL` / `ENTER DETAILS` / `ENTER REFRESH` (the focused row is the core).

Two entries is the rule: the left one opens the drawer or backs out, the right one acts on what is focused. A shelf with nothing selected has no strip at all, since there is nothing to act on.

The keys in the strip follow the bindings. Outside a game the keys bound to the pad's buttons mean what the buttons mean: **X** confirms like A, **Z** backs out like B, **S** opens the options drawer like X, **A** does the same as Y, and the d-pad keys stay arrows. So the same four keys do the same four things whether the player is holding a pad or a keyboard, and the strip always names the key it is actually listening to.

Dialogs are the one place with no key hints: they are just two buttons, cancel on the left and the confirming verb on the right, both focusable and clickable. Caption text never takes focus anywhere — it is the label of the control above it.

### Controls

| Console action | Player 1 keyboard | Controller |
|---|---|---|
| D-pad | Arrow keys | D-pad / left stick |
| A | X | A |
| B | Z | X |
| X | S | Y |
| Y | A | B |
| Select | Right Shift | Back |
| Start | Enter | Start |
| Turbo A | C | Left stick button |
| Turbo B | V | Right stick button |
| Open pause menu | Escape | Guide |
| Fast-forward (hold) | Tab | Right shoulder |
| Slow motion (hold) | Left Shift | Left shoulder |
| Rewind (hold) | Backspace | — |
| Step one frame (paused) | . (period) | Right shoulder |
| Mute / unmute | M | — |
| Screenshot | F12 | F12 (keyboard only) |

**The keyboard is a one-player device.** Player 1 keeps the whole keyboard; player 2 is a second pad. Two people on one keyboard would have to share the keys the first player is already holding, so Settings → Controller shows the key column for player 1 and only the button column for player 2. To play player 1 on the keyboard against player 2 on a pad, open Settings → Controller, switch **Editing bindings for** to **Player 2**, and cycle the pad onto that port. Two pads on ports 1 and 2 work with no setup at all. Both translation layers — the menu one that turns the face keys into Enter/Escape/X, and the runtime one that hands the keys to the core — are written down in [docs/input.md](docs/input.md), together with the reasons this area kept regressing and the way to verify a key press end to end.

On a TrimUI Brick the panel is read directly, so its buttons are the controller in the table. The two function keys are shortcuts of their own: **F1** saves the current slot and **F2** loads it while a game is running, and both are ignored in the menus because a slot only means something in a game. The firmware also watches these keys, so leave them unbound in the stock **Fnkey setting** app; binding a function there fires both actions at once.

The case lighting follows the accent colour: the **THEME** page has an **LED LIGHTING** switch, the lights turn the accent colour while MRG Player runs, and the stock lighting is put back when it exits. This only applies where the Brick exposes the system LEDs, so desktop builds do not show the switch.

Every platform uses the same controller mapping and the same player 1 keyboard. The eight console actions are forwarded to the core's joypad, turbo is applied in front of the core, and the pause menu, fast-forward, slow motion, rewind, frame stepping, and screenshots behave identically.

Holding **Turbo A** or **Turbo B** repeats that button: the console sees it pressed for two frames and released for two, which is fifteen presses per second at the NTSC field rate. Turbo is a front-end feature and never reaches the emulator as a button of its own, so it works for any game that rewards rapid tapping, and a normal A or B press still wins while the turbo key is held.

The pause menu provides **Continue**, **Save State**, **Load State**, **Reset Console**, **Power Cycle**, **Display**, **Core Options**, **Controls**, and **Return Home**. Use Left/Right, the D-pad, or the left stick to choose one of three slots and navigate actions. Enter/A activates the selected action; Escape/B resumes. Press **.** (period) or the right shoulder button to advance exactly one frame while paused. The focused slot shows a preview card with its thumbnail, save time, and the play time at capture, so a slot can be recognised before it is loaded. State files are stored under `states/<platform>/` in the MRG Player data root, with a `.meta` description and a `.bmp` thumbnail beside each one.

Holding **Left Shift** or the left shoulder button halves the frame rate to 0.5× for precise play. Slow motion and fast-forward are mutually exclusive: the last input wins, and both stop as soon as the button or key is released.

Reset is the front-panel button: the CPU restarts from the reset vector while work RAM, mapper registers, and battery RAM keep their contents. Power Cycle builds the console from scratch — work RAM and mapper state are lost, and the cartridge battery save is reloaded — so it asks for confirmation and defaults that dialog to **Cancel**. Fast-forward runs the machine four times faster while held, applies the configured fast-forward audio policy (silent by default), and does not count toward play time.

Pressing **F12** on the game or pause screen saves a PNG of exactly what is on screen to `screenshots/` in the MRG Player data root, named after the moment it was taken, and confirms it with a short banner. The frame is read back before it is presented, so the file never lags a frame behind. Compression needs zlib; a build without it falls back to writing a BMP and the banner reports the failure.

Pressing **M** mutes or unmutes the output from the game or pause screen and shows the new state as a short banner; the same toggle lives in **Settings → Audio**, and the saved volume is untouched while muted.

Instant states are the core's own serialization, so they restore the whole machine in one step and never depend on how long the session has been running. Each state file is wrapped in a versioned header that records the core's name and the game's content id, so a state written by a different core or a different game is refused instead of being loaded into an incompatible machine. Queued host audio is discarded on load, and the capture records the sample rate it was taken at so states stay loadable after the audio device changes.

If a snapshot cannot be restored, the runtime reloads the core and reports that the state is incompatible or damaged. State files are stored per game and slot under `states/<platform>/` in the MRG Player data root.

### In-game control hints

Starting a game briefly shows just the pause entry: **Esc · Pause · View controls** on the keyboard, **MENU** on the Brick built-in controls, or **GUIDE** on an SDL controller. It fades out after three seconds, never resizes the game picture, and does not reappear when resuming from pause.

Open **Pause → Controls** with normal navigation and confirm. The drawer shows the current **Player 1** bindings for the selected prompt device, not a hard-coded keyboard cheat sheet: NES/GB show direction, A/B, START and SELECT; GBA adds L/R; wider pads also list X/Y. Brick uses its physical A/B and L1/R1 mapping. Genesis labels explicitly refer to RetroPad, since the core maps those to console buttons. Long custom direction mappings and wider pads paginate with directions; confirm or back returns to the pause card. The single Back button uses the current accent and shared white focus ring. Specific in-game actions (accelerate, jump, etc.) are left to the game.

### Latency options

**Settings → General** carries the two playback options that trade memory and CPU for feel. **Rewind** (on by default) keeps a rolling window of the core's own snapshots while a game runs: one snapshot every other frame, capped at roughly ten seconds or 32 MiB, whichever comes first. Hold **Backspace** to step back through it; the first press skips the newest snapshot because it describes the frame already on screen, and each further press moves two frames back. Rewinding pauses the frame clock, drops its audio instead of queuing it, and stops when the window runs out. The window starts empty and is discarded on console reset, power cycle, state load, or when runahead is turned on. Every pad button is a console input, so there is nothing left to hold on a handheld: the line is hidden while the prompts come from a pad and comes back once a keyboard takes over.

**Runahead** (off by default, 0-3 frames) hides the core's own input lag by running it past the picture that is shown. Each frame runs the core once with audio and video muted, remembers that machine, runs the remaining hidden frames, then produces the frame that is displayed and finally rolls the machine back to the remembered point, so the next input lands earlier in the core than the frame it produced. The cost is one serialization per frame plus the extra emulation, and some cores do not leave their audio clean after a load, so an audible click can appear; that is why the setting is off by default. Both settings are stored in `settings.ini` and applied to a running game immediately.

### Display options

Open **Settings → Display** to control how the picture fills the window. **Aspect ratio** cycles through square pixels, the 4:3 screen shape of a CRT, and the 8:7 pixel grid of the NES and Game Boy; **Integer scaling** snaps the picture to whole multiples of the cropped source and falls back to fractional scaling when the window is smaller than one source pixel; **Overscan crop** removes 0-16 source pixels from every edge to imitate the hidden border of a television. Rendering stays nearest-neighbour and centred, with black letterboxing, and every change applies immediately and is written to `settings.ini`.

### Theme

**Settings → Theme** holds the single accent colour the whole interface shares: the header mark, the pills, the footer
key chips, the selected card's badge, the wash and bar that mark the current row, the pause menu, and the confirmation
dialogs. Six hues are offered — teal (the default, and the colour of the wordmark), blue, purple, lime, orange, and
yellow — and Left/Right or Enter cycles through them. The change applies to the screen that is open and is written to
`accent=` in `settings.ini`, so it survives a restart.

Only the accent moves. The background gradient, the card plates, the focus ring (a near-white line, because focus is a
system state rather than a brand colour), and the destructive red of the delete confirmation stay fixed. Cards are no
longer tinted per console: identity belongs to the box art and the small platform badge, which is why a library with
five consoles in it still reads as one launcher.

### Core options

Cores describe their own settings through the libretro environment callbacks, and MRG Player exposes them rather than hiding
them behind compiled-in defaults. **Core Options** in the pause menu lists everything the running core declared — through
both the original `SET_VARIABLES` form and the categorised `SET_CORE_OPTIONS_V2` form — with the core's own labels and
values. Move with Up/Down, change the focused value with Left/Right or Enter, and press **R** (or the left shoulder button)
to restore the declared defaults.

The panel is translated: the option names and the common value words of the cores the installer offers — Mesen, mGBA,
Snes9x, Gambatte, and Genesis Plus GX — are shown in Chinese when the launcher is in Chinese, while numbers, palette
names, and hardware jargon stay as the core wrote them. Anything a core adds later that the translation table has not
seen yet falls back to the core's own English label, so a core update never leaves a blank row.

Changes reach the core immediately through `GET_VARIABLE` and `GET_VARIABLE_UPDATE`, so an option such as a video filter
or an overclock can be judged on the running picture while the panel is open. Selections are written to
`core-options/<core>.tsv` in the data root, one file per core, and are reloaded the next time that core starts; deleting
the file is the same as restoring the defaults. A core that declares nothing shows an empty panel instead of inventing
settings, and keys the core never asked about are reported as undefined.

The same three options can be overridden for a single game from **Display** in the pause menu, which is useful when only one title looks wrong at the global ratio. Every change previews on the running picture while the panel is open; **Use global defaults** decides whether the values are stored with that game. Turning it off writes the current values into the library metadata and keeps them for that game only; turning it back on discards the override and follows `settings.ini` again. Leaving the panel without an override restores the picture that was there before.

### Audio options

**Settings → Audio** holds the master volume (steps of 10%), the **Mute** toggle, and the **Fast-forward audio** policy. Muting silences the output without changing the stored volume, so unmuting restores the configured level. With fast-forward audio set to **Silent** the samples generated while fast-forwarding are dropped, which keeps latency at zero; set to **Keep**, one sample buffer out of every four is queued so the game stays audible while the device keeps draining at real time — the result is choppy but recognisable, and no audio backlog builds up. All three settings are saved in `settings.ini` and applied immediately.

### Input configuration

Open **Settings → Controller** to choose the UI prompt mode (**Auto**, **Keyboard & Mouse**, or **Controller**), controller label style (**Auto**, Xbox, Nintendo Switch, PlayStation, or Generic), and the active controller when multiple devices are connected. These choices change presentation only: keyboard, mouse, and controllers remain usable together. Auto mode follows the most recent meaningful input and ignores analog movement inside the configured dead zone; it falls back to keyboard prompts when no controller is available. The preferred controller is restored by SDL controller GUID after hotplug or restart.

The knobs on this screen follow the console convention: the category rail and the page next to it are two halves of one screen, so enter the screen on the rail, move the focus between the rail and the page with Left and Right, and press Enter on a category to step into its page. Back/Escape returns to the rail and, from there, to the home screen. The same page configures all eight console actions (A, B, Select, Start, and D-pad) plus the two turbo actions, laid out in two columns. Select an action and press the desired keyboard key or controller button. Press **R** on the controller settings page to restore defaults. The analog-stick dead zone is adjustable from 4000 to 30000. Prompt preferences, the preferred controller, bindings, and dead zone are saved in `settings.ini`. Home, settings, game-management, and pause hints use the selected keyboard/controller presentation, with the button first and what it does written beside it. On the home screen the direction keys stay on the content: left and right walk the shelf, up moves to the library and settings pills, and down comes back. The strip along the bottom never takes focus, so Enter or A always starts the selected card and S (or X on a pad) opens its drawer. Every clickable control on the settings screen draws the same focus ring, which only adds a border around the row: the plate underneath keeps its colour as the selection moves. The cores page re-reads the installed cores whenever the settings screen is opened, so it lists them without a click.

The shelf always shows seven and a half cards: seven whole ones and a half card at the right edge, so a library that continues past the screen says so on its own. Once there are more than seven games, the row slides as the selection moves so the highlighted card stays in view and the half card is always the next one; the row stops at the last game instead of wrapping, and settles into place when the screen is opened rather than sliding in from the first game.

## libretro cores

Every platform runs through a libretro core that must already be on the machine; MRG Player bundles none. Copy the core's shared library into `cores/` in the data root and it is picked up on the next launch:

| Platform | Extensions | Cores, in the order they are tried |
|---|---|---|
| NES (`FC`) | `.nes` | `mesen_libretro`, `nestopia_libretro`, `fceumm_libretro`, `quicknes_libretro` |
| Game Boy Advance (`GBA`) | `.gba` | `mgba_libretro`, `gpsp_libretro`, `vbam_libretro`, `vba_next_libretro` |
| Super NES (`SFC`) | `.sfc`, `.smc` | `snes9x_libretro`, `bsnes_libretro`, `mesen-s_libretro` |
| Game Boy / Color (`GB`) | `.gb`, `.gbc` | `gambatte_libretro`, `sameboy_libretro`, `gearboy_libretro` |
| Mega Drive / Genesis (`MD`) | `.md`, `.gen` | `genesis_plus_gx_libretro`, `picodrive_libretro` |

**Settings → Cores** installs the preferred core for each platform. Each row names the core, shows how much space the installed library takes, and reports whether it is there. Enter on a row downloads that core's archive from the libretro buildbot and unpacks it into `cores/` under the data root; on a row that is already installed the same key re-downloads the current build, which is how a core is updated. Delete removes the core, its archive, and any copy unpacked from one, and asks for a second press first so a stray key never costs a download. R re-reads the disk. Every outcome is reported as a banner on the page. The download runs on a background thread, so the launcher keeps drawing. On Linux and macOS it uses `curl` with `wget` as a fallback; on Windows it uses WinHTTP, so a stock machine needs no extra tools. The buildbot only publishes rolling `latest/` directories, so an install fetches whatever that directory holds today; the SHA-256 of each archive is printed to stderr for reference rather than enforced. Set `MRG_CORES_BASE_URL` to a mirror that ends in `/` to fetch from somewhere else. If a buildbot directory is unreachable, the row's install fails with a message that says which step failed.

A downloaded archive needs no manual step: the libretro buildbot publishes every core as a one-file zip, and a <core>.so.zip (`.dll.zip` on Windows, `.dylib.zip` on macOS) sitting where the library itself would be is unpacked on first use. The extracted copy lands in `cores/unpacked/` under the data root, which keeps read-only system directories untouched, and it is reused on later launches while its bytes still match the archive. An archive that is not a zip, or that does not contain the expected library, is reported on stderr and skipped, so a stray file never hides a working core.

Cores open the ROM through narrow file APIs, so a game whose file name is not plain ASCII (`F1赛车.nes`) is copied once into `rom-cache/` under the data root and the core is handed that ASCII path instead. The copy is removed when the game stops, and the error message a failed load reports still names the file the player picked.

Core search order: `$MRG_CORES_DIR` when it is set (and then only that directory), then `<data root>/cores/`. On Windows the directory next to `mrg-player.exe` is searched as well, so a portable copy keeps its cores beside the binary. On Linux and macOS the search continues with `/usr/lib/libretro/`, `/usr/lib64/libretro/`, `/usr/lib/x86_64-linux-gnu/libretro/`, `/usr/local/lib/libretro/`, and `~/.local/lib/libretro/`. Setting `MRG_CORES_DIR` restricts the search to one directory, which is also how the test suite injects its stub core.

Everything above the runtime interface treats every platform the same: one pause menu and three instant-state slots with thumbnails, fast-forward, slow motion, single-frame stepping, screenshots, turbo, per-game display overrides, and mute. A state file is the core's own serialization wrapped in a versioned header that records the core's name, so a state written by a different core is refused instead of being loaded into an incompatible machine. Battery saves live in `saves/<platform>/` as `.srm` files, are written when the game closes, and are what the management menu's delete action removes. Cores that need system or BIOS files look for them in `system/` under the data root.

## Known limitations

- Every platform requires a libretro core you install yourself. Without a matching core, launching the game reports that no libretro core is installed for the platform instead of running it.
- Game compatibility, expansion audio, FDS support, and mapper coverage are the core's, not the launcher's; pick a core that covers the titles you play.
- Only software-rendered cores are supported, and a core that insists on an OpenGL or Vulkan context is refused.
- Instant states are the core's own blob, so their cross-build compatibility is the core's, not ours; the launcher still rejects a state written by a different core.
- Box art is looked up by exact No-Intro name. A title the thumbnails project does not ship needs an entry in `library/cover-names.json`, or `Imgs/<system>/<title>.png` dropped in by hand.

## Tests

For a headless build:

```bash
cmake -S . -B build-headless \
  -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF \
  -DSDL_DIRECTFB=OFF -DSDL_RPI=OFF -DBUILD_TESTING=ON \
  -DMRG_ENABLE_SMOKE_TESTS=ON
cmake --build build-headless -j
ctest --test-dir build-headless --output-on-failure

SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./build-headless/mrg-player --smoke-test --language=en
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./build-headless/mrg-player --smoke-test-settings --language=zh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./build-headless/mrg-player --smoke-test-management /path/to/legal-test.nes --language=zh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./build-headless/mrg-player --smoke-test-pause /path/to/legal-test.nes --language=zh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./build-headless/mrg-player --smoke-test-rom /path/to/legal-test.nes
```

Every platform needs a core, including the NES, so the game-path smoke tests are run with `MRG_CORES_DIR=$PWD/build-headless/cores`, where the stub cores are built.

The UI regression tests exercise control-help bindings, keyboard/controller navigation, shared hit geometry, the three-second hint timer, and both languages with all six accents on both native canvas sizes. Set `MRG_UI_SNAPSHOT_DIR=/tmp/mrg-ui` when running `ctest` to save control-help and entry-hint BMPs for the configured canvas.

The `--smoke-test-*` flags are a developer tool and are compiled out by default: a build without `-DMRG_ENABLE_SMOKE_TESTS=ON` reports them as unknown options, so a released binary carries neither the flags nor the frame-capture code they drive. CI configures with the option on.

Smoke tests render 1280×720 BMP files in the working directory, which git ignores; the settings smoke test captures the general, display, audio, controller, cores, theme, and about pages. The pause smoke test saves and reloads a slot, exercises fast-forward with both audio policies, the mute hotkey, slow motion, single-frame stepping and the slot metadata card, captures the pause card, the per-game display panel, the core options panel, and the controls drawer, then resets and power cycles the console. The unit test covers validation, import, platform metadata, platform directory layout, duplicate detection, discovery, runtime registration, save routing, independent game/save removal, library metadata persistence, favorite ordering, play-history accumulation, per-game display overrides, the older-metadata upgrade path, and configurable input persistence, adaptive prompt selection, controller-layout labels, and no-controller fallback. `input-bindings-test` checks the default binding table, the eight-versus-ten action split, and the turbo duty cycle; `core-option-text-test` checks the Chinese core-option labels and the fallbacks; `cover-names-test` checks the box-art mapping parser, the console repositories, and the URL candidates; `data-layout-test` checks that a data root written by a previous build is moved into the current folder names, that a newer file is never overwritten by an older one, and that a downloaded cover can never land inside the pak; `png-writer-test` decodes the file the screenshot writer produces. `libretro-runtime-test` builds stub cores under the GBA and NES names, points `MRG_CORES_DIR` at them, and drives the whole back end: platform detection, core discovery, NES routing through libretro, `dlopen` and symbol resolution, frames, instant-state round trips and their determinism, battery saves, a power cycle, collecting the core's declared options, answering `GET_VARIABLE`, flagging updates back to the core, persisting and resetting those values, rewinding through remembered snapshots, showing the frame runahead produces, unpacking the same core from stored and deflated zips, removing an installed core, and the error raised when no core is installed. Setting `MRG_TEST_CORE_INSTALL=1` additionally downloads the current NES core and checks the installer end to end, which is why it is opt-in: the suite itself never touches the network. `sha256-test` checks the hash helper against the FIPS 180-4 vectors.
