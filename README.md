# Virtual_Badge
Code to display PNGs to a small display using an ATmega328P

## Build
Requires `avr-gcc`, `avr-libc`, `cmake`, and `python3`.

- Build: `./Build`
- Flash: `cmake --build build --target flash`

## Images
Images are generated at build time from `HD_EO_MC.png` and `HU_EO_MC.png` into `build/generated/generated_images.h` using `tools/png_to_rgb565.py`.

The generator currently:
- Resizes with nearest-neighbor using a “contain” fit into `128x160`
- Emits RGB565 with simple RLE compression for AVR flash size
