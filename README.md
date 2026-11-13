# Metal Gear Solid: Ported Ops

A native PC port of **Metal Gear Solid: Portable Ops** (PSP, USA disc `ULUS10202`).
The game's own code runs on a purpose-built PSP machine, and its graphics are redrawn on your graphics card
(OpenGL or Vulkan) at high resolution, at 60 fps, with optional remastered lighting.

**No game files are included.** You need your own copy of the game, as a disc image (`.iso`).
Everything made from the game's art (the launcher's FOXHOUND intro, the HD packs) is locked to the disc: it only opens
once the launcher has verified your own copy.

## What you get

- Sharp HD rendering up to 2880x1632, anti-aliasing, smooth or PSP-sharp textures
- 60 fps (the game keeps its own pace; in-between pictures are drawn by the graphics card)
- Remaster lighting: real-time character shadows, sun shading, ambient occlusion, colour grade
- HD texture packs (the PPSSPP kind) and HD movie replacements
- Keyboard and mouse controls made for PC, Xbox / PlayStation controllers
- Locker capture: hold Shift / R2 while dragging an enemy into an open locker to capture him, like at the truck
- Snapshots (F5) on top of the game's own saves, a performance log (F7) and one-key problem reports (F8)

## Before you start

You need Windows 10 or 11 (64-bit), [Python 3.10 or newer](https://www.python.org/downloads/) (tick
"Add to PATH" when installing), and a graphics card with OpenGL or Vulkan (any recent NVIDIA, AMD or Intel card).

## Setup

**Step 1: Gather all the files you need**
- **Your game's disc image (ISO):** `Metal Gear Solid - Portable Ops (USA).iso`, from your own copy of the game
- **pspdecrypt.exe:** download it from [pspdecrypt's releases page](https://github.com/John-K/pspdecrypt/releases)
- **The .pak files:** the `MGSPO-HD-*.pak` files from this project's [Releases](../../releases) page (our upscaled
  movies, cutscene drawings, menus and HUD)
- **The HD texture pack:** **TheGreatBulzome's** pack, for the environments, objects, weapons and characters. The
  download link is under [his video](https://www.youtube.com/watch?v=5MRC69kFOLM) ([rb.gy/jyajnt](https://rb.gy/jyajnt))

**Step 2:** Put `pspdecrypt.exe` in the **MGS Ported Ops** folder.

**Step 3:** Make a new folder inside the **MGS Ported Ops** folder (any name, for example `My Files`), and put the
ISO, the .pak files and the HD texture pack's folder in it.

**Step 4:** Start the launcher (double-click **`MGS Ported Ops.bat`**) and open **DISC CHECK**:
1. Choose **IMPORT FOLDER** and pick the folder from Step 3. The launcher finds your ISO, copies in the HD
   textures and installs the .pak files (this takes a few minutes).
2. Choose **BUILD** (one time only: it builds the game's program file from your ISO with pspdecrypt).
3. When everything shows a tick, the FOXHOUND intro plays. Go **BACK** and choose **NEW MISSION**.

Next time, **CONTINUE MISSION** picks up from your last F5 snapshot.

All settings are in the launcher's **SETTINGS** page (they are kept in `port_settings.txt`).

## Controls

| Keyboard / mouse | PSP | Controller |
|---|---|---|
| W A S D | analog stick | left stick |
| mouse | camera (D-pad) | right stick |
| Space | Cross | A / Cross |
| R | Circle | B / Circle |
| left mouse button | Square | X / Square |
| Shift | Triangle | Y / Triangle |
| right mouse button | L (auto aim) | LB / L1 |
| middle mouse button | R (first-person aim) | RB / R1 |
| Tab / C | START / SELECT | Start / Back |

F5 snapshot · F11 full screen · F6 lighting on/off · F4 60 fps on/off · F7 performance log · F8 problem report · Esc quit

## Making the packs (for maintainers)

`make_packs.bat` makes the locked packs from the files in the folder, with the key from your disc:
`pak\launcher_assets.pak` (from `launcher_assets\`, goes in the code) and `pak\MGSPO-HD-Movies-N.pak` /
`pak\MGSPO-HD-Textures-N.pak` (each under 1.9 GB: upload them to a GitHub release, not into the code).
Texture packs made by other people are left out (see `SKIP` in `make_packs.py`).

## Building the fast core

`psp_core.dll` is included. To build it yourself from `psp_core.c` / `psp_gpu.c` (+ `psp_gl.inc`,
`psp_vkgl.inc`): run `build_core.bat`.

## Credits and licences

- SDL2 (`SDL2.dll`): zlib licence, see `README-SDL.txt`
- Vulkan headers (`vk/`): Apache 2.0 / MIT, The Khronos Group
- HD texture pack (environments, objects, weapons, characters): **TheGreatBulzome** (HotdogRawdog) - not included,
  the launcher links to [his release](https://www.youtube.com/watch?v=5MRC69kFOLM)
- Metal Gear Solid: Portable Ops and the FOXHOUND logo are © Konami. This project is not affiliated with Konami.
