<!-- SPDX-License-Identifier: MPL-2.0 -->

# Third-party notices

The repository's original OSKey source and documentation are licensed under
the Mozilla Public License 2.0 as stated in `LICENSE`. Files carrying another
SPDX identifier, and bundled dependencies with their own license files, remain
under those terms.

## Apache-2.0 material

Parts of this repository are derived from Zephyr Project examples, drivers,
subsystems, or upstream patches. This includes portions of the network and USB
integration and most patches under `patch/`. The GC0308 driver and ES7210/ES8311
audio drivers also contain work originating from Espressif projects. Applicable
files retain their existing copyright and `Apache-2.0` notices. A copy of the
license is available at `LICENSES/Apache-2.0.txt`.

## LVGL

`patch/lvgl-native-sim-benchmark.patch` modifies LVGL source and retains LVGL's
MIT terms. The applicable copyright notice and license text are in
`LICENSES/LVGL-MIT.txt`.

## Tabler Icons

SVG icons and their generated display assets under `src/display/assets/` are
based on Tabler Icons and are distributed under the MIT License. The applicable
copyright notice and license text are in `src/display/assets/LICENSE.tabler`.

## Bundled libraries

Libraries under `lib/` are separate components. Their own license files and
per-file notices define the applicable terms; the repository-level MPL-2.0
license does not replace them.
