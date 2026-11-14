# Air Defense Command for Nintendo 3DS

A native port of the Mac game: the same bases, weapons, radar model, enemy waves, air wing, front line,
carriers, difficulty modes and Creative mode. It runs on any 3DS and uses the New 3DS extras when present.

**Status: the code is finished and tested on a PC, but it has not been compiled for or run on a real 3DS yet.**
The game logic and both screens were tested with a software renderer (see `host/`), and every file passes
an ARM compile check against the real libctru/citro2d headers. The installable `.cia` still has to be built
with one of the two routes below.

## Get it onto the 3DS with FBI

### Route A: GitHub builds it, FBI downloads it (no installs on your computer)

1. Create a new **public** repository on github.com and upload the contents of this folder
   (including the hidden `.github` folder).
2. Open the repository's **Actions** tab. The "Build 3DS" job runs by itself and takes a few minutes.
3. When it finishes, open **Releases**. You'll find `AirDefense.cia` and `AirDefense-QR.png`.
4. On the 3DS: **FBI > Remote Install > Scan QR Code**, point it at the QR image on your screen, press A.

The download address is always
`https://github.com/<you>/<repo>/releases/download/latest/AirDefense.cia`, so the same QR code picks up
every later build. This needs FBI 2.6 or newer (older versions can't download over HTTPS).

### Route B: build on your Mac, send over Wi-Fi

1. Install devkitPro pacman from https://devkitpro.org/wiki/Getting_Started, then in Terminal:
   `sudo dkp-pacman -S 3ds-dev`
2. In this folder: `./build.sh`
   (it also downloads `makerom` and `bannertool`, which the `.cia` needs, into `tools/bin`)
3. On the 3DS: **FBI > Remote Install > Receive URLs over the network**. It shows the console's IP address.
4. On the Mac: `python3 tools/send_to_fbi.py 192.168.x.x` and press A on the 3DS.

Run `python3 tools/send_to_fbi.py` with no address to get a URL (and a QR code after `pip3 install qrcode`)
for **Scan QR Code** or **Manually enter URL** instead. `AirDefense.3dsx` is also built, for the Homebrew Launcher.

## Screens

**Top screen (stereoscopic 3D):** the battlefield. Push the 3D slider up: the ground sits behind the screen,
aircraft fly near the screen plane with shadows on the ground below them, climbing after take-off and
sinking on approach, and ballistic missiles arc out of the screen before they come down. The HUD shows
level, money, HQ health, hostiles and the latest messages. 3D strength is adjustable in MORE > OPTIONS.

**Touch screen:** everything you buy or manage. Every button is at least 28 pixels tall, lists scroll by
dragging, and nothing needs a double tap.

| Tab | What's there |
|---|---|
| MAP | Tactical map. WIDE shows the whole theatre (tap to look there); ZOOM follows the camera (drag to pan). Tap a unit, base or enemy target to manage it. |
| WEAPONS | The eight systems, two across. Tap one for its stats, then PLACE. |
| BASES | Build, rebuild and repair sites; base upgrades. |
| AIR WING | Squadron status and missions, the hangar (with the aircraft photos), air upgrades, strike targets. |
| MORE | Message log, statistics, options, controls, and the Creative tools in Creative mode. |

**Placing a unit:** WEAPONS > pick > PLACE. The map opens with a ghost. Tap or drag on the map to aim it,
nudge with the D-Pad, then CONFIRM (or A). B cancels.

## Controls

| Input | Action |
|---|---|
| Circle Pad | Move the camera |
| C-Stick (New 3DS) | Move the camera fast |
| L / R | Zoom out / in |
| ZL / ZR (New 3DS) | Previous / next tab |
| D-Pad | Nudge the unit being placed; otherwise move the camera |
| A | Confirm placement, or select what's under the crosshair |
| B | Cancel / deselect |
| X | 2x speed |
| Y | Back to HQ |
| SELECT | Switch the lower map between WIDE and ZOOM |
| START | Pause menu (on the title screen: exit) |

## New 3DS

- Runs the CPU at 804 MHz with the L2 cache on (set in `meta/app.rsf` and at start-up).
- C-Stick and ZL/ZR are used. An original 3DS/2DS still runs the game with the remaining controls.
- Each frame draws about 5,000 shapes with 3D on, well inside what the GPU handles at 60 fps.

## What's the same and what changed from the Mac version

Same: every number in the simulation (prices, ranges, hit chances, radar detection, enemy types and levels,
raids, Iskanders, Kh-101 salvos, ICBMs, SEAD, point defense, jets, auto missions, strike targets, carriers,
upgrades, the four difficulty modes, the 80% map and 80% game speed).

Changed for the handheld:
- Menus moved to the touch screen; the Mac's mouse and keyboard shortcuts became the controls above.
- Placing several units in a row (Shift-click on the Mac) is one PLACE per unit.
- Range rings show for the selected unit by default; MORE > OPTIONS can show all of them.
- At low zoom, aircraft are drawn as arrowheads so they stay readable on a 400x240 screen.
- The starting Vulcan sits slightly further from the HQ so its label doesn't overlap the HQ health bar.
- No sound (the Mac game has none either) and no save file: each battle is one sitting, as on the Mac.

## Files

- `src/sim.c`, `src/air.c`: the simulation (ported from the Swift `BattleScene*` and `GameState`)
- `src/catalog.c`: every weapon, aircraft, base and upgrade
- `src/view.c`: battlefield drawing for both screens, stereo depth, top-screen HUD
- `src/ui.c`: the touch-screen interface
- `src/main.c`: main loop, camera and buttons
- `src/plat_3ds.c`: citro2d/libctru backend
- `src/assets.c`: font and aircraft photos (made by `tools/gen_assets.py`)
- `meta/`: icon, banner, banner sound, and the `.cia` settings
- `host/`: PC test harness. `make -C host`, then feed `adc_host` a script of taps and button presses to get
  screenshots of both screens; `soak` plays the game headless for a long time under sanitizers.
