# SVG Rendering Troubleshooting

Three independent issues can break ThorVG-backed SVG rendering in LVGL.

## Native simulator ABI mismatch

`native_sim` is a Linux process. Building its Rust static library for
`x86_64-unknown-none` introduces soft-float `compiler-builtins` symbols such as
`sinf` and `cosf`. These symbols can satisfy ThorVG's math calls even though
their ABI is incompatible with the Linux x86-64 calling convention, corrupting
vector geometry.

Build the Rust library for `x86_64-unknown-linux-gnu` on 64-bit x86
`native_sim`. Real ESP32 and ARM targets remain on their bare-metal Rust
targets because all linked code uses the same target ABI there.

## Partial-buffer coordinates

In partial render mode, ThorVG draws into a buffer representing only part of
the display. Its viewport must therefore use buffer-local coordinates:

```c
rc.y - layer->buf_area.y1
```

Using `partial_y_offset` mixes an offset within the invalidated region with an
absolute display coordinate. SVGs outside the first buffer area can disappear,
clip, or render at the wrong position. Apply
[`lvgl-svg-partial.patch`](../../patch/lvgl-svg-partial.patch) to fix this on
every target using partial rendering.

## Image recolor

LVGL's SVG decoder submits vector tasks directly, bypassing bitmap image
recolor. The same patch applies `image_recolor` to solid SVG fills and strokes,
which covers the monochrome OSKey icon set.

## Verification

- Confirm the native simulator uses `x86_64-unknown-linux-gnu` for Rust.
- Render SVGs at several vertical positions and across partial-buffer boundaries.
- Invalidate and redraw the same regions to catch intermittent clipping.
- Verify semantic icon colors instead of checking only the SVG source colors.
- Build at least one native simulator, ESP32, and ARM target.
