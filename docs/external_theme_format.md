# N64 theme PNG format (version 1)

A gallery of ready-made packages, along with the tools used to build them, is
maintained at <https://github.com/rmmh/N64FlashcartMenuThemes>.

Install packages as `/menu/themes/*.png` on active storage. Bundled files in
`assets/themes/` are copied unchanged to `rom:/themes/`. Invalid packages and
other extensions are ignored. Storage packages override bundled names,
case-insensitively. Families are listed as they are discovered, up to twelve;
further families, and the reserved families `Custom` and `External`, appear
under `External`.
The theme name `Custom` is reserved and invalid in a package.

Use a non-interlaced PNG, at least 64 pixels wide, at most 640×484, or at most
2048×1024 when it has a type-11 static layout. Metadata occupies the first 1–4
rows, using only their first 64 pixels. Static artwork may occupy up to 640×480
below it, or the type-11 rectangle. Compact artwork coordinates are unsigned bytes.
Any PNG color type is accepted if decoding to RGBA8 preserves metadata exactly.
Metadata alpha must be 255. Integers and IEEE-754 floats are little-endian.
Ancillary chunk cache/decompressed payload is limited to 64 KiB.

## Header and records

Read metadata RGB left-to-right, continuing on the next row (skip alpha).

| Offset | Bytes | Value |
|---:|---:|---|
| 0 | 4 | ASCII `N64T` |
| 4 | 1 | Version `1` |
| 5 | 1 | Metadata rows, 1–4, no greater than PNG height |
| 6 | 2 | Header size: 16 through `rows × 64 × 3` |
| 8 | 4 | Reserved flags, zero |
| 12 | 4 | IEEE/zlib CRC-32 of bytes 16 through `header_size−1` |

Records are `(type:u8, length:u16, value:length bytes)`. End with `00 00 00`
exactly at `header_size`. Unknown types are skipped; duplicate or malformed
known records invalidate the package. Unused metadata bytes are ignored;
canonical writers zero them.

| Type | Bytes | Value / requirement |
|---:|---:|---|
| 1 | 1–63 | Required name; no ASCII controls or DEL |
| 2 | 1–63 | Required family; same restrictions |
| 3 | 64 | Required palette: sixteen RGBA8888 colors |
| 4 | variable | Background, below; omission means solid color 0 |
| 5 | 12 | Three rectangles `(x,y,width,height)`: layer 0, layer 1, border |
| 6 | 3 | Embedded-border geometry: overlap, join depth, selected expansion |
| 7 | 1 | Internal border ID; required unless type 5 embeds a border |
| 8 | 10 | Two five-byte texture animation records, layer 0 then layer 1 |
| 9 | 13 | Parameter mask:u32, rectangle:4 bytes, animation:5 bytes |
| 10 | 8 | Texture origin U/V, two finite floats in −64…64 |
| 11 | 16 | Static layout: rectangle (4×u16), pan X/Y (floats) |
| 12 | 7+5n | Indexed static palette and color-cycling ranges |

Names/families are intended as UTF-8; encoding is not validated. Available
glyphs are listed in `assets/fonts/charset.txt`; unsupported glyphs may be
missing. The picker escapes `$` and `^` as literal text. Control-character
rejection also protects persisted INI values.

Palette order is: background, panel, border, text, muted text, accent,
highlight, inactive tab, active tab, shadow, success, warning, gradient
TL/TR/BL/BR. Interface alpha is honored where applicable; gradient alpha is
unused. Palette indices are also parameter-mask bits 0–15.

## Background and texture regions

Type 4 begins with the mode byte, followed by these fields in order:

| Mode | Total bytes | Fields |
|---:|---:|---|
| 3: gradient | 9 | speed, warp (floats) |
| 2: single texture | 18 | scale, X/Y velocity, rotation (floats), alpha:u8 |
| 1: dual texture | 31 | scales 0/1, X/Y velocity 0, X/Y velocity 1, rotation (floats), alpha 0/1:u8 |
| 5: static image | 1 | All artwork below metadata, scaled to 640×480 |
| 5: static image with overlay | 18 | As mode 2; requires type 11 |

All floats must be finite. Scales: 1/64…16; velocities: −4…4, in units of
8 texels per nominal frame, positive moving the texture right/down; rotation:
−0.25…0.25 radians per nominal frame; gradient speed: 0…2 cycles/second;
warp: 0…1. Without type 11, static mode forbids types 5, 8 and 9. Large
images are tiled for upload rather than constrained by TMEM.

Type-5 rectangles must fit the PNG and exclude metadata. Single mode requires
layer 0; dual mode requires both. Texture dimensions are powers of two, each
at most 64. A texture whose dimensions are both ≤32 uses RGBA5551; otherwise
it uses CI4. Dual textures must use the same format. Round each encoded row's
byte size up to 8; combined TMEM use must be ≤4096 bytes for RGBA16 or ≤2048
for CI4 (the other 2048 bytes hold its palette).

Equal layer rectangles and animation records share decoded frames and count
once toward TMEM, unless either texture-selector channel is animated.
A border rectangle, when present, is exactly 24×24; absent rectangles are zero.

## Static layout, panning and palettes

Type 11 is only valid in static mode. Its value is
`(x:u16, y:u16, width:u16, height:u16, pan_x:f32, pan_y:f32)`. The rectangle
must fit the PNG, start below the metadata, and start at or below the bottom
of every other artwork rectangle, including animation frames and the type-12
palette strip, so it can be decoded last. Width is 1–2048 and height 1–1024.
Pans are finite, −8…8 output pixels per nominal frame.

The image is scaled uniformly by `s = max(640/width, 480/height)`, so it
covers the screen. On an axis with zero pan it is centered. On an axis with a
nonzero pan, its position starts centered and moves by `pan × F` pixels,
wrapping modulo the scaled size; that axis repeats, so its opposite edges
should join seamlessly. Positive pans move the image right or down.

The static image with overlay (mode 5, 18 bytes) draws layer 0 over the image
as `mix(image, texture0, texture0.a × alpha/255)`, using the single-texture
fields, motion, type-8 layer-0 animation and type-9 channels. Layer 1 and its
animation record must be zero. Without type 12, a static image is RGBA5551
and its area is at most 640×480 pixels.

Type 12 requires type 11. Its value is
`(strip_x:u16, strip_y:u16, count:u16, range_count:u8)` followed by
`range_count` five-byte ranges `(start:u8, length:u8, step_ticks:u16, flags:u8)`.
The strip `(strip_x, strip_y, count, 1)` lists the palette: 1–256 opaque
colors, which may repeat. The static rectangle then holds palette indices
rather than colors: every pixel is opaque gray with red = green = blue = its
index, which must be less than `count`. The image is stored as CI8, so its
area may be up to 1280×480 pixels. At most 16 ranges are allowed. Each needs length ≥2, must fit
within the palette, must not overlap another, and needs `step_ticks ≥ 1`.
Flags: bit 0 reverses, bit 1 blends, other bits are zero.

Color cycling rotates each range every `step_ticks` nominal ticks. With
`k = floor(F / step_ticks)`, entry `start + i` shows palette color
`start + ((i − k) mod length)`; reversed ranges use `i + k`. Blending
interpolates each 5-bit channel toward the next step's color by
`(F mod step_ticks) / step_ticks`, rounding to nearest. Colors outside ranges
are fixed. Custom hue shifts apply to the palette.

## Animation

Each playback record is `(frame_count:u8, columns:u8, frame_ticks:u16, flags:u8)`.
For textures, count 0/1 means static and every other field must be zero; the
unused layer-1 record in single mode must be entirely zero. Otherwise count is
2–255 and columns/duration are nonzero. Omitting type 8 means static layers.
Frame `i` of base rectangle `(x,y,w,h)` is
`(x + (i % columns)×w, y + floor(i/columns)×h, w, h)`.
Every frame must fit below metadata; derived X/Y must still fit unsigned bytes.

Flags: bits 0–1 select loop (0), ping-pong (1), once (2); value 3 is invalid.
Bit 2 reverses playback; bit 3 interpolates; bits 4–7 are zero. Texture
interpolation requires RGBA16. Duration is in nominal 60 Hz ticks; clocks are
independent and advance by actual elapsed time. Frame selection does not reset
UV motion. Readers without animation support may skip those records.

Type 9 requires a nonzero mask using only bits 0–28 and count ≥1. Each frame
packs its masked channels in ascending order into RGB bytes; alpha must be
255. Its rectangle is `ceil(packed_bytes/3) × 1`, with final unused RGB bytes
zero. Omitted channels inherit static values, with absent scales defaulting to
1 and texture selectors to 255.

| Mask bits | Bytes each | Value |
|---:|---:|---|
| 0–15 | 4 | Palette colors, RGBA8888 |
| 16–17 | 4 | Gradient speed, warp |
| 18–19 | 4 | Layer scales 0/1 |
| 20–23 | 4 | Layer-0 X/Y velocity, layer-1 X/Y velocity |
| 24 | 4 | Rotation speed |
| 25–26 | 1 | Layer alpha 0/1 |
| 27–28 | 1 | Texture-frame selectors 0/1 |

Animated floats obey the background ranges. Selectors are 255 (automatic) or
less than that layer's frame count; unused layers accept 0/255. Without
interpolation, values step. With it, scales interpolate logarithmically;
colors, alpha and other floats interpolate linearly. Selectors switch halfway
and force an unblended texture frame. Disabled mask channels have no effect.
Background mode and playback flags cannot animate. Rates are integrated;
changing scale compensates UV offsets to keep zoom centered.

## Borders

Exactly one source is required: type 7, or a type-5 border plus type 6.
An internal border forbids type 6. The embedded 24×24 sheet contains nine
8×8 patches: corners once, repeating edges, and a center mask. It uses CI4 and
point filtering. Geometry accepts full bytes; authors should normally use 0–8.
Internal IDs 0–8 are Plain, Double, Facets, Beads, Brackets, Stitches, Nodes,
Scallops, Soft, matching `assets/images/theme-border-*.png`. Their geometry
is `(2,3,1)`, except Brackets `(2,3,4)`.

## Rendering reference

See [`external_theme.c`](../src/menu/external_theme.c) for conversion/palettes,
[`background.c`](../src/menu/ui_components/background.c) for motion/blending,
and [`common.c`](../src/menu/ui_components/common.c) plus
[`constants.h`](../src/menu/ui_components/constants.h) for UI geometry.
Examples include Diurnal gradients, Sakura drift and Matrix texture interpolation.
