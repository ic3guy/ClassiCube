# Full view distance performance work

The current requirement is 60 FPS in Normal (256x64x256) and Huge
(512x64x512) worlds, preserving the 512-block view. The earlier adaptive-distance
results in `pi3-performance.md` do not satisfy this requirement. The installed
game now has both `viewdist=512` and `gfx-minviewdist=512`.
This branch also makes adaptive distance explicitly opt in through
`gfx-adaptiveview=true`; a frame target alone cannot enable it.

## Benchmark protocol

Run on the actual Pi's accelerated VC4 desktop OpenGL driver at 854x480. Use
`handover/pi/run-bench.py` in the isolated benchmark directory, not the installed
save directory. Check that `BENCH_READY` reports zero pending unbuilt meshes
throughout the measurement and every recorded distance is 512. The runner
rejects a changed distance for a fixed-distance test.

`benchmark-preload=true` completes initial meshing before the warmup. Static
renderer comparisons use `benchmark-freeze=true` to stop simulation and texture
animation, with the same seed and camera. Those measure renderer capacity;
live gameplay must also pass with simulation enabled before calling the goal
complete. Moving-camera tests include a terrain route and an above-world orbit.

For correctness comparisons, use fully loaded fixed-camera screenshots with
frozen simulation, comparing optimisation flags off and on. Include underground
rooms, glass, a shaft open to the sky, and an outside-world camera. Do not use
screenshot frames as performance evidence. Never compile during measurements.

## Experiments

All new rendering flags default off. The production executable has not been
replaced by these experiments.

* `gfx-flushverts`: optional OpenGL job boundaries after this many submitted
  terrain vertices. Very large unsplit full-distance scenes cause repeated VC4
  GPU resets on this Pi; splitting jobs avoids that stall while retaining all
  geometry and framebuffer contents.
* `gfx-portalcull`: conservative chunk portal connectivity, treating unknown or
  dirty data as transparent. It brought little benefit in generated worlds.
* `gfx-hizcull`, `gfx-hizboxes`: conservative software occlusion. Guaranteed-solid
  cuboids rasterise coverage at framebuffer pixel centres into 4x4 tile masks.
  Combined partial masks use a farthest-depth bound. The camera's current view
  and mesh bounds are used; the view distance is unchanged. Unknown, dirty,
  near-plane-crossing, and ambiguous camera cases fall back to drawing.
* `gfx-hizmesh`: use actual opaque terrain faces as occluders. This resolved
  small image differences found with volume proxies and fine mesh bands.
  `gfx-hizvc4` additionally tests the VC4's signed 12.4 coordinate conversion.
* `gfx-meshbands`: optional height bands of opaque mesh faces, allowing a hidden
  underground portion to be skipped even when its chunk extends above ground.
  Cutout and translucent face order is retained.

The first full-resolution depth implementation cost too much CPU time during
movement. Tile masks reduce storage and per-pixel work. Tight mesh bounds and
smaller solid proxies improved a frozen Normal scene from 27.51 to 75.67 FPS
uncapped. The corresponding Huge result was 20.39 FPS. The aerial results were
still only 15.05 FPS (Normal) and 5.77 FPS (Huge), so the objective remains open.
These are exploration runs `fv5-normal-base` and `fv6-gl1-*`, using full distance,
seed 12345, 5 seconds of warmup, and 10 seconds of measurement.

An occupancy flood-fill found that sealed spaces account for only about 14-16%
of opaque faces adjacent to nonopaque space in these generated worlds. Removing
only sealed caves cannot deliver the needed gain. The remaining work targets
actual occlusion and geometry submission, with live simulation checks afterward.

## Correctness checkpoint

The actual-face variant passed five within-process A/B comparisons: an open
shaft, Normal spawn and aerial views, and Huge spawn and aerial views. All five
images had zero differing pixels (`visual11-*` and `visual12-*`). The simulation
and camera were frozen, and initial meshing was complete. Earlier volume-proxy
and fine-band variants failed at a shaft edge and are not suitable for installing
with those settings.

These conservative variants remain too slow during movement. The next work is
reducing the terrain mesh itself. Larger unsplit workloads also exhaust VC4 GPU
allocation space; 65,536-vertex job boundaries completed the Huge A/B checks.
The source and benchmarks remain experimental; no replacement was installed.

The VC4 coordinate experiment was checked against Mesa's
[coordinate shader implementation](https://fuchsia.googlesource.com/third_party/mesa/+/refs/tags/gitlab/mesa-22.3.6/src/gallium/drivers/vc4/vc4_program.c).
It did not produce a useful moving-frame speedup in the tested scenes.
