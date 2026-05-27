# wled-sprite-usermod

`Sprite` is a standalone WLED usermod that adds two 2D effects:

- `Sprite` renders a PNG sprite or a JSON-defined animation on a 2D segment.
  The asset is selected from the segment name, decoded once, then cached for
  playback.
- `Precipitation` renders procedural falling particles for rain, snow, sleet,
  hail, sparks, or any other color-driven shower.

## Supported targets

- ESP32 only
- 2D segments or matrix layouts only
- Packed `.spr` assets, PNG assets, or JSON animation definitions stored on the WLED filesystem

These effects do not render on 1D segments.

## Installation

Add this usermod to `custom_usermods` in your WLED build.

For most users, the simplest option is to reference the published repository directly:

```ini
custom_usermods =
  ${env:esp32dev.custom_usermods}
  https://github.com/teetow/wled-sprite-usermod.git#main
```

If you are working on the usermod locally, point `custom_usermods` at a local checkout instead:

```ini
[env:esp32dev]
extends = env:esp32dev
custom_usermods =
  ${env:esp32dev.custom_usermods}
  file:///absolute/path/to/wled-sprite-usermod
```

For day-to-day development in this repo, use the helper scripts in the `Local development` section below.

## Usage

1. Upload a packed `.spr` file, PNG file, or JSON animation definition to the WLED filesystem.
2. Set the segment name to the filename, for example `cloud.spr`.

Packed `.spr` files are the preferred runtime format. They contain the frame
table and RGB565+alpha pixel bytes the effect caches internally, so loading a
sprite only requires validating the header and reading bytes from the
filesystem. PNG and JSON loading remain supported as editable/debug fallbacks.

For animations, create a JSON file that lists the frames and their durations:

```json
[
  { "filename": "cloud-0.png", "duration": 120 },
  { "filename": "cloud-1.png", "duration": 120 },
  { "filename": "cloud-1.png", "duration": 120, "flip": true }
]
```

Then point the segment name at the JSON file instead, for example `cloud.json`.
Filenames in the JSON may be relative to the JSON file location, or absolute.
Missing `duration` defaults to `100` ms.
Set `flip`, `flipX`, or `flipped` to mirror that frame horizontally without
uploading a second PNG.

The segment name is treated as a path from the root of the filesystem.
A plain name like `cloud.png` is equivalent to `/cloud.png`.
If the segment name is empty, unsupported, or points to a missing file, the
effect falls back to a blank segment.

## Sprite controls

| WLED control | Label in UI           | Description                                                                                                                                                     |
| ------------ | --------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Speed        | `Move speed`    | Horizontal movement speed. `0` keeps sprites stationary. Values above `0` map to roughly 1 to 128 pixels per second. Moving sprites make one full off-screen-to-off-screen traversal, then respawn. |
| Intensity    | `Instances`     | Number of sprite instances. Low values keep one sprite. Higher values increase the moving spawn pool or stationary layout count up to the maximum that fits the segment and sprite size. |
| Custom 1     | `Spawn jitter`  | Randomizes moving sprite spawn timing and row. For animations, it also randomizes the starting frame. `0` removes extra spawn delay/frame offset; `255` uses the widest jitter window. |
| Check 1      | `Mask`                | Ignores the PNG RGB colors and uses sprite alpha as a stencil over the segment's primary color.                                                                 |
| Check 2      | `Gradient`            | Only applies when `Mask` is enabled. Fills masked pixels from the active palette instead of a flat primary color.                                               |
| Check 3      | `Horizontal Gradient` | Only applies when `Gradient` is enabled. Uses a horizontal palette axis instead of a vertical one.                                                              |

## Precipitation controls

| WLED control | Label in UI     | Description                                                                                                             |
| ------------ | --------------- | ----------------------------------------------------------------------------------------------------------------------- |
| Color 1      | `Particle`      | Particle head color. Use white for snow or hail, blue-white for rain, or any custom color.                              |
| Color 2      | `Trail start`   | Trail color nearest the particle head.                                                                                  |
| Color 3      | `Trail end`     | Trail color at the end of the fade.                                                                                     |
| Speed        | `Fall speed`    | Downward particle speed. `0` freezes the particles in place.                                                            |
| Intensity    | `Density`       | Active particle density. `0` disables particles; higher values increase the active particle count.                       |
| Custom 1     | `Bounciness`    | Splash droplet peak height. `0` disables splashes entirely (drops just despawn — good for snow). Higher values jump higher and read more like hail; capped so peak height stays within ~5 pixels regardless of slider position. |
| Custom 2     | `Shear`         | Horizontal travel / wind angle. The midpoint is vertical fall; lower and higher values push particles left or right.     |
| Custom 3     | `Trail length`  | Trail length in pixels. WLED stores this slider at reduced 0-31 resolution, so the maximum trail is roughly 31 pixels.   |

Suggested starting points:

| Look  | Particle | Trail start | Trail end | Fall speed | Density | Bounciness | Shear | Trail length |
| ----- | -------- | ----------- | --------- | ---------- | ------- | ---------- | ----- | ------------ |
| Rain  | Pale blue | Blue        | Black     | Medium     | Medium  | Low-medium | To taste | Medium-high |
| Snow  | White    | White       | Black     | Low        | Medium  | 0          | Gentle | Short        |
| Sleet | White    | Blue-white  | Blue      | Medium     | Medium  | Low        | To taste | Medium       |
| Hail  | White    | White       | Black     | Medium-high | Low    | High       | Low   | Short        |

Setting `Trail end` to solid black makes the trail fade to fully transparent
(alpha is what fades — the tail does not darken the background). For a tail
that actually fades toward a color, pick any non-black value.

## Starfield controls

`Starfield` paints procedural twinkling stars over the segment, recreating the
twinkle animation from the Blaster rasterizer. Star positions are deterministic
per segment, so the same panel always lights the same stars; each star pulses
on its own phase via `sin(t * rate + seed * 9) * 0.5 + 0.5`, then maps to
`0.18..1.0` alpha so even the dimmest twinkle stays faintly visible.

| WLED control | Label in UI       | Description                                                                                                  |
| ------------ | ----------------- | ------------------------------------------------------------------------------------------------------------ |
| Color 1      | (primary star)    | Star color. The starfield paints every twinkle in this color (or palette if `Gradient` is enabled).          |
| Speed        | `Twinkle speed`   | Twinkle pulse rate. `128` reproduces the rasterizer's 1.6 rad/s feel; lower slows the pulse, higher speeds it up. |
| Intensity    | `Density`         | Number of visible stars. `0` blanks the segment; full slider caps at roughly `pixels / 16` stars (~6% coverage). |
| Check 1      | `Gradient`        | Overrides the twinkles with a static vertical palette gradient — useful if you want to repurpose the seg as a sky backdrop. |

## DitheredGradient controls

`DitheredGradient` fills the matrix from the active WLED palette. It is useful as
a palette-respecting background layer, such as a sky gradient.

| WLED control | Label in UI | Description                                                                                   |
| ------------ | ----------- | --------------------------------------------------------------------------------------------- |
| Speed        | `Dither`    | Ordered dither strength. `0` renders a smooth palette gradient; higher values add more grain. |
| Intensity    | `Rotation`  | Gradient direction. The default `0` is vertical; higher values rotate the gradient axis.       |

## Limits

- Maximum frame size is 320 x 320 pixels.
- Total cache usage for one segment must fit within WLED's segment-data budget and is capped by this usermod at 12 KB.

Large sprites or long animations can fail to load if they exceed the cache budget.

## Troubleshooting

If `Sprite` renders a blank segment, check the segment name first, then
verify the asset files on the WLED filesystem.

The usermod publishes runtime info under `info.sprite_usermod` in WLED's JSON info output.
Useful fields include:

- `ready`: whether the effect registered successfully
- `fxid`: runtime effect id assigned by WLED
- `precip_fxid`: runtime effect id assigned to `Precipitation`
- `dithered_gradient_fxid`: runtime effect id assigned to `DitheredGradient`
- `starfield_fxid`: runtime effect id assigned to `Starfield`
- `active`: number of active segments using the effect
- `bytes`: current sprite cache memory in use
- `peak`: highest cache usage seen since boot
- `loads`: successful asset loads
- `clones`: loads avoided by cloning another active segment's cache
- `shared_hits`: loads avoided by restoring from the shared decoded cache
- `errors`: cumulative load failures
- `err`: most recent error code

Error codes:

| Code | Meaning                                          |
| ---- | ------------------------------------------------ |
| 0    | No error                                         |
| 1    | Segment name missing                             |
| 2    | Unsupported mode or non-2D segment               |
| 3    | Asset file missing                               |
| 4    | Invalid animation definition JSON                |
| 5    | Memory allocation failure                        |
| 6    | PNG open or decode failure                       |
| 7    | Asset exceeds sprite cache budget or size limits |
| 8    | Invalid packed `.spr` asset                      |

## Local development

The helper scripts create a temporary `platformio.sprite.ini`, link this repo
into `custom_usermods` through `symlink://...`, and build the derived
environment `esp32dev_sprite_usermod`.

They also validate that the target checkout is stock WLED (`github.com/wled/WLED`) to avoid compiling against older custom forks.

Windows:

```powershell
.\build-local.ps1 D:\path\to\WLED esp32dev
```

Linux or macOS:

```sh
./build-local.sh /absolute/path/to/WLED esp32dev
```

Build and deploy directly to a live WLED device:

```powershell
.\build-local.ps1 D:\path\to\WLED esp32dev -DeployHost wled-32.lan
```

Deploy a previously built firmware image without rebuilding:

```powershell
.\build-local.ps1 D:\path\to\WLED esp32dev -DeployHost wled-32.lan -SkipBuild
```

## Notes

- Effect name in WLED: `Sprite`
- Additional effect names in WLED: `Precipitation`, `DitheredGradient`, `Starfield`
- The current implementation is ESP32-only.
- This repository intentionally does not include demo sprite assets.
