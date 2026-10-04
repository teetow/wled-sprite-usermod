# wled-sprite-usermod

`Sprite` is a standalone WLED usermod that adds several 2D effects:

- `Sprite` renders a PNG sprite or a JSON-defined animation on a 2D segment.
  The asset is selected from the segment name, decoded once, then cached for
  playback.
- `Precipitation` renders procedural falling particles for rain, snow, sleet,
  hail, sparks, or any other color-driven shower.
- `Wind` renders procedural particles blowing across the segment: leaves,
  grit, spindrift, embers. It is the horizontal sibling of `Precipitation`,
  with flutter instead of splashes and three particle colors instead of a
  head/trail gradient.
- `Leaves` keeps persistent litter on the ground and optionally sheds sparse
  leaves from Neoblaster's tree canopy, with long fades after landing.

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

Decoded sprite data is cached by filename and cache generation. To make a panel
reload an overwritten file without changing the segment name, post this to
WLED's JSON state API:

```json
{
  "sprite_usermod": {
    "clear_cache": true
  }
}
```

The command bumps the internal cache generation and clears the shared decoded
cache. Existing sprite segments reload their current filenames on the next
render.

## Sprite controls

| WLED control | Label in UI           | Description                                                                                                                                                     |
| ------------ | --------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Speed        | `Move speed`    | Horizontal movement speed. `0` keeps sprites stationary. Values above `0` map to roughly 1 to 128 pixels per second. Moving sprites make one full off-screen-to-off-screen traversal, draw with horizontal subpixel antialiasing, then respawn. |
| Intensity    | `Instances`     | Number of sprite instances. Low values keep one sprite. Higher values increase the moving spawn pool or stationary layout count up to the maximum that fits the segment and sprite size. |
| Custom 1     | `Spawn jitter`  | Randomizes moving sprite spawn timing and row. For animations, it also randomizes the starting frame. `0` removes extra spawn delay/frame offset; `255` uses the widest jitter window. |
| Custom 2     | `Shade`               | Darkens the sprite's RGB, keeping its alpha. `255` (default) is unchanged. Use this to dim a sprite: segment brightness is compositing opacity in WLED 16, so lowering it makes the sprite see-through. |
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

## Wind controls

`Wind` blows particles across the segment instead of dropping them down it.
Each particle picks one of the three segment colors from its own seed, so a
palette of three tints produces a mixed field — which is what makes the same
effect read as autumn leaves, summer grit, or winter spindrift. Particles that
leave the segment on either axis re-enter at the upwind edge.

| WLED control | Label in UI      | Description                                                                                                          |
| ------------ | ---------------- | -------------------------------------------------------------------------------------------------------------------- |
| Color 1..3   | `Particle A/B/C` | The three particle colors. Each particle owns one of them. Leave B and C black for a uniform field.                   |
| Speed        | `Mean speed`     | Horizontal travel speed, roughly 3 px/s at `1` and 60 px/s at `255`. `0` leaves only gust motion.                           |
| Intensity    | `Density`        | Active particle count. `0` disables particles; `1` is a single-particle trickle.                                                                       |
| Custom 1     | `Flutter/Swirls` | In fluid mode, strength of rotational forcing. In standalone mode, vertical sine wobble. `128` is roughly ±1 px, `255` roughly ±2 px. `0` makes particles slide flat.                    |
| Custom 2     | `Vertical drift` | `128` is level flight; above it particles settle as they cross, below it they lift. ±16 px/s at the extremes.         |
| Custom 3     | `Streak length`  | Trailing pixels behind each particle, drawn straight upwind. `0` (bare particles) suits leaves; 2-3 suits spindrift.  |
| Check 1      | `Blow left`      | Reverses travel. Off blows right. |
| Check 2 | `Fluid flow` | Enables the evolving fluid field; off retains standalone sine flutter. |

An optional `wind_gust` JSON field on each segment sets the gust ceiling
(0–255, same scale as `sx`). For example, `{"seg":[{"id":8,"sx":100,"wind_gust":220,"ix":15}]}`
gives sparse particles with speeds ranging from mean wind to the gust ceiling.
Zero, missing on effect selection, or a ceiling below `sx` gives steady mean
wind. With a positive gust ceiling, `sx=0` allows particles to wait motionless
between fronts. Partial updates without `wind_gust` preserve the current ceiling.
The optional `wind_gust_activity` byte (0–255) controls synthesized cadence
and roughness: weak gusts are rare and brief; stronger values produce more
frequent, irregular surges with sharper attacks and uneven kicks. Neoblaster
sends the same value to airborne Wind and ground Leaves; a full effect selection
without it defaults to zero.

Neoblaster also supplies `wind_spawn_rate` in particles per second (0–3,
rounded to tenths). This starts with an empty panel and emits at the upwind
edge with ±30% timing jitter. Exiting particles expire; speed changes never
cause extra births. New particles choose the most open of a few sampled
positions or entry rows, avoiding piles while waiting for a gust. Zero drains
the field; `sx=0` pauses emission only when
the gust ceiling is also zero. A missing or negative rate on effect selection keeps standalone
Wind's original intensity-driven population. In rate mode, nonzero `ix` enables
the particle pool; its value does not set the live count. Neoblaster uses `ix:1`
so ordinary preset restores remain sparse before it reapplies the rate.

Ground and airborne motion share a clock-derived gust schedule at absolute
panel columns. Weak gusts are brief and rare; stronger gustiness brings more
frequent, irregular surges with quick attacks and uneven kicks. Fronts travel
downwind at 10 pixels/s; reversing the bearing reverses the front. Gustiness
is derived from both absolute peak speed and excess over sustained wind.

Both effects accept a per-segment `wind_gust_tuning` object. Its integer fields
and defaults, the tuned nominal look, are `spacing_pct:125`, `duration_pct:25`,
`attack_ms:20`, `roughness_pct:100`, `response_ms:470`, and `settle_ms:50`.
Spacing and duration scale the synthesized schedule; attack controls the
front's rise time; roughness controls uneven kicks. Response and settling
control how quickly particles follow a gust and return to the sustained
baseline; they do not touch the fluid grid, which relaxes on its own ~0.5 s
time constant. Missing fields retain their current values on partial updates;
a full effect selection without the object restores defaults. Neoblaster sends
the same tuning to both layers: spacing, duration and roughness from three
autosaving editor sliders, and attack, response and settling fixed at the
defaults.

**Leaf hand-off.** While ground Leaves and Wind both run, they exchange leaves
through a small buffer in this usermod (`leaf_exchange.hpp`). A ground leaf
rising more than two rows clear of the horizon continues as an airborne Wind
particle at the same LED, color and speed. An airborne particle coming down
over the ground band settles as litter once it reaches its own randomly chosen
landing row, on an LED clear of existing litter, keeping its color and
horizontal momentum. Each side publishes a heartbeat and its free room, so a
hand-off is only offered to a running effect with space, and nothing vanishes
in transit. Neoblaster gives autumn Wind particles the litter's colors and
brightness, so the two layers read as one population. `/json/info.sprite_usermod`
reports running totals as `leaf_lifts` and `leaf_landings`.

The `Fluid flow` checkbox (`o2`) enables a 16×8 incompressible fluid solver for
Wind: self-advection, viscosity, pressure projection and vorticity confinement.
Particles respond with inertia to the sampled flow rather than following
independent sine paths. Gust fronts add a local stream after pressure projection
so brief bursts retain their sharp acceleration. Vertical drift scales with
local wind and falls silent in gust-only lulls; Custom 1 adjusts swirl
strength in fluid mode and sine flutter when Fluid flow is disabled. Neoblaster enables
Fluid flow. Three swirl sources feed the grid; each lives about 8 seconds,
appears at a fresh spot, drifts downwind with the mean flow and fades out, so
eddies never pin to fixed columns. Gusts feed them harder.

The optional `wind_mps10` field (`[mean, gust]`, physical m/s ×10, 0–255) gives
the Wind solver the real wind speeds that drive its turbulence. Without it, for
example after a preset restore, the effect estimates them by inverting
Neoblaster's default speed scale (4 m/s at `sx` 70, 17 m/s at 255).

Each solver uses about 3 KB; `/json/info.sprite_usermod` reports `flow_grid`,
plus `wind_us`/`wind_max_us` and `leaves_us`/`leaves_max_us` (whole-effect
render time, ground Leaves only) for device performance checks.

WLED's usermod state hook runs before it serializes segments, so `/json/state`
reports top-level `wind_gust`, `wind_gust_activity`, `wind_gust_tuning`, `wind_mps10`, and `wind_spawn_rate`
arrays indexed by segment id.
A rate of `-1` denotes the legacy population mode.
Neoblaster merges those into the corresponding live segments. Controls follow
its fixed segment ids; ordinary WLED presets do not serialize this extension,
so the scene controller reapplies the gust ceiling after a preset restore.

Suggested starting points:

| Look        | Particle colors        | Mean speed | Density | Flutter | Vertical drift | Streak |
| ----------- | ---------------------- | ---------- | ------- | ------- | -------------- | ------ |
| Autumn leaves | Yellow / red / brown | Medium     | Low-medium | High | Slightly settling | 0   |
| Summer grit | Three dark neutrals    | Medium     | Low     | Low     | Slightly settling | 0    |
| Spindrift   | Three near-whites      | High       | Medium  | Medium  | Level          | 2-3    |
| Embers      | Orange / red / yellow  | Low        | Low     | Medium  | Lifting        | 1      |

Particles carry the same parallax as `Precipitation`: each one's seed doubles
as a depth value, so background particles travel slower and render dimmer.

## Leaves controls

Use two `Leaves` segments: one covering the ground band, and one covering the
6x7 Neoblaster tree plus a landing row below its trunk, with `Tree shedding`
enabled. The latter contributes extra leaves beneath the tree. Use alpha-over
blending (17). Neoblaster owns calendar activation, snow suppression and colors.

| Control | Meaning |
| --- | --- |
| Color 1–3 | Tree autumn palette: yellow, red, brown |
| Speed | Sustained physical wind in tenths of m/s: 40 means 4 m/s, 150 means 15 m/s |
| Intensity | Ground density, up to one leaf per eight pixels, capped at 32; shedding mode uses it as opacity |
| Custom 1 | Shedding: remaining tree foliage, 0–255. Ground: shared gust activity, 0–255 |
| Custom 2 | Ground: physical gust peak in tenths of m/s. Shedding: seasonal taper, 0 at peak through 255 to stop births |
| Custom 3 | Ground: horizon row within the segment (reserve rows above for lift). Shedding: autumn progress, 0–31 |
| Check 1 | Blow left |
| Check 2 | Tree shedding instead of ground litter |

Shedding uses one or two independent sources from Neoblaster's present, turned
canopy cells. Each fades in on the tree for 10 seconds, falls with gravity to
the ground in 2 seconds, fades out there for 10 seconds, then waits 12–22
seconds before choosing a new source at peak seasonal activity. Custom 2
stretches that quiet interval so the full cycle rate declines with the season,
while fade and fall durations remain fixed. A second source starts at a different
time and prefers a separate canopy column. At most two leaves are airborne at once.
Tree-fallen leaves stay in their source column while falling and after landing,
regardless of wind controls; the separate ground-litter segment handles wind.
Ground litter starts in open positions at half opacity and reaches full
strength in 2 seconds; it never draws below half opacity, so no particle is
wasted as a faint smudge. Neoblaster also lightens any tree color within 3:1
contrast of the ground (judged at daytime brightness) before sending it. Density
is based on ground area only, excluding the reserved sky. Each leaf has a
world position, depth, velocity, drag, static friction and separate lift height.
Perspective projects near leaves three times faster than distant ones; movement
through depth changes both screen row and scale. Leaves occupy one LED each.
Spacing is a hard rule: no two visible leaves ever share or touch an LED,
diagonals included, so litter can never draw a solid streak. Every placement
and every move is checked. A blocked leaf glances off one row in depth or
stops short; a resting leaf is never pushed by a neighbour, and an airborne
leaf with nowhere to land drifts sideways until a gap opens. Before contact,
moving leaves steer around neighbours within 3.5 LEDs, and each leaf carries
its own flutter, small-scale turbulence the 16×8 grid cannot resolve, so
neighbours do not travel in lockstep convoys. Depth flutter is sized in screen
rows, and the band's top and bottom edges reflect leaves and push back gently,
so litter spreads evenly across rows instead of lining the horizon. Moving
litter keeps a Clark–Evans ratio of about 1.25–1.55 (1.0 is uniform random).

Nothing wraps: a leaf blown off the panel is gone. A different leaf arrives
0.5–4 seconds later. Once wind can move litter (mean or gust of at least 3 m/s),
about two thirds of arrivals blow in from the upwind edge, on the free entry
row farthest from nearby litter; the rest, and every arrival in calm air, fade
in at an open spot. Strong wind thins the visible litter somewhat: around 14 of
24 leaves at 8 m/s, 10 in a 15 m/s gale.

The evolving fluid field supplies local curved flow. At 1/2 m/s most leaves
rest, 3–4 m/s brings noticeable skittering, 5–8 m/s escalates sustained motion,
and stronger winds increase transport and lift. Local gust wind crossing
8–10 m/s permits lift in rotating patches; gravity brings leaves back down.
Lift uses a modeled updraft derived from the 2D flow, not a full 3D air solver.
Momentum and eddies decay after a gust or return to calm. Ground state uses
under 5 KB per segment. Shedding retains its canopy lifecycle and ignores wind.

The canopy template and coordinate hash in `leaves_effect.hpp` mirror
Neoblaster's `src/tree.ts`; update both if the tree silhouette changes.
`test/leaves_harness.cpp` exercises the shipped effect with a deterministic clock.
Discovery reports `info.sprite_usermod.leaves_fxid`.

## Starfield controls

`Starfield` paints procedural twinkling stars over the segment, recreating the
twinkle animation from the Blaster rasterizer. The segment is divided into a
stable grid of roughly square cells and one star is placed at a deterministic
random position in each selected cell. Each star also receives a deterministic
magnitude: high-magnitude stars are brighter and appear earlier as `Night depth`
rises, while the many low-magnitude stars fade in later and remain naturally
dimmer. The magnitude distribution is stable, so changing intensity never moves
stars that are already visible. Twinkle modulates each star's magnitude-derived
alpha rather than making every star equally bright.

| WLED control | Label in UI       | Description                                                                                                  |
| ------------ | ----------------- | ------------------------------------------------------------------------------------------------------------ |
| Color 1      | (primary star)    | Star color. The starfield paints every twinkle in this color (or palette if `Gradient` is enabled).          |
| Speed        | `Twinkle speed`   | Twinkle pulse rate. `128` reproduces the rasterizer's 1.6 rad/s feel; lower slows the pulse, higher speeds it up. |
| Intensity    | `Night depth`     | Reveals stars in magnitude order. `0` blanks the segment; `255` reveals the full catalogue of roughly `pixels / 4` stars (~25% coverage). |
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
- `wind_fxid`: runtime effect id assigned to `Wind`
- `active`: number of active segments using the effect
- `bytes`: current sprite cache memory in use
- `peak`: highest cache usage seen since boot
- `cache_generation`: current cache generation, incremented when the cache is cleared
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

## Host tests

Particle effects are stepped on a host before they go near a panel:

```sh
./test/run-host-tests.sh            # build and run
./test/run-host-tests.sh --frames   # plus ASCII frame dumps
```

`test/wled_stub.hpp` supplies the slice of WLED the effects touch — Segment,
`strip.now`, the color macros and the real `sin8_t` — so `test/wind_harness.cpp`
compiles the shipped `wind_effect.hpp` unchanged and steps it frame by frame with
a clock the test owns. Because the effects seed their PRNG from `strip.now`, a
pinned start time replays the identical field every run: no wall clock, no
hardware, no flaky particle test.

This is where physics belongs. It has already earned itself: it caught a spawn
margin that reached further outside the segment than the despawn margin, so a
recycled particle was re-rolled every frame — visible on a panel only as a faint
flicker at the entry edge, and invisible in a still capture.

Requires only `g++`; there is no PlatformIO or toolchain dependency.

## Local development

The helper scripts fetch the selected channel from the official
`github.com/wled/WLED` remote, create a detached temporary worktree, apply the
versioned alpha-over compatibility patch from `patches/`, and link this repo
into `custom_usermods` through `symlink://...`. The original WLED worktree and
its currently checked-out branch are not changed.

`stable` is the highest non-prerelease WLED version tag and is the default.
`nightly` is the official moving `nightly` tag. If the compatibility patch no
longer applies cleanly, the build stops so upstream blending changes can be
reviewed explicitly.

```sh
./build-local.sh /absolute/path/to/WLED esp32dev
./build-local.sh /absolute/path/to/WLED esp32dev --channel nightly
```

Successful builds are copied to
`.build/<channel>/<pio-env>/firmware.bin`. The adjacent `build-info.json`
records the exact WLED ref and commit, usermod commit and dirty state,
compatibility-patch hash, PlatformIO environment, and build time.

Build and deploy directly to a live WLED device:

```sh
./build-local.sh /absolute/path/to/WLED esp32dev --channel stable --deploy-host wled-32.local
```

Deploy a previously built firmware image without rebuilding:

```sh
./build-local.sh /absolute/path/to/WLED esp32dev --channel stable --deploy-host wled-32.local --skip-build
```

When using `--skip-build`, select the same channel used to create the stored
artifact.

## Notes

- Effect name in WLED: `Sprite`
- Additional effect names in WLED: `Precipitation`, `DitheredGradient`, `Starfield`, `Wind`
- The current implementation is ESP32-only.
- This repository intentionally does not include demo sprite assets.
