# wled-sprite-usermod

`Sprite` is a standalone WLED usermod that renders a PNG sprite or a
JSON-defined animation on a 2D segment. The asset is selected from the segment
name, decoded once, then cached for playback.

## Supported targets

- ESP32 only
- 2D segments or matrix layouts only
- PNG assets or JSON animation definitions stored on the WLED filesystem

This effect does not render on 1D segments.

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

1. Upload one or more PNG files to the WLED filesystem.
2. Set the segment name to the filename, for example `cloud.png`.

For animations, create a JSON file that lists the frames and their durations:

```json
[
  { "filename": "cloud-0.png", "duration": 120 },
  { "filename": "cloud-1.png", "duration": 120 }
]
```

Then point the segment name at the JSON file instead, for example `cloud.json`.
Filenames in the JSON may be relative to the JSON file location, or absolute.
Missing `duration` defaults to `100` ms.

The segment name is treated as a path from the root of the filesystem.
A plain name like `cloud.png` is equivalent to `/cloud.png`.
If the segment name is empty, unsupported, or points to a missing file, the
effect falls back to a blank segment.

## Effect controls

| WLED control | Label in UI           | Description                                                                                                                                                     |
| ------------ | --------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Speed        | `Move speed`          | Horizontal movement speed. `0` keeps sprites stationary. Values above `0` map to roughly 4 to 128 pixels per second and wrap across the segment width.          |
| Intensity    | `Instances`           | Number of sprite instances. Low values keep one centered sprite. Higher values increase instance count up to the maximum that fits the segment and sprite size. |
| Check 1      | `Mask`                | Ignores the PNG RGB colors and uses sprite alpha as a stencil over the segment's primary color.                                                                 |
| Check 2      | `Gradient`            | Only applies when `Mask` is enabled. Fills masked pixels from the active palette instead of a flat primary color.                                               |
| Check 3      | `Horizontal Gradient` | Only applies when `Gradient` is enabled. Uses a horizontal palette axis instead of a vertical one.                                                              |

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
- `active`: number of active segments using the effect
- `bytes`: current sprite cache memory in use
- `peak`: highest cache usage seen since boot
- `loads`: successful asset loads
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
- The current implementation is ESP32-only.
- This repository intentionally does not include demo sprite assets.
