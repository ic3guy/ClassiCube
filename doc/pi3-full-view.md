# Full view distance performance work

> Superseded target: the user selected Normal-world 60 FPS as the release goal. See [the Normal release](pi3-normal60.md). The Huge measurements and experiments below remain historical.

The goal remains open: steady 60 FPS in Normal (256x64x256) and Huge
(512x64x512), retaining the 512-block view. The installed executable and saved
world have not been replaced by the experimental renderer.

## Reproducible checkpoint

Work is on branch full-view-60. Build the experimental desktop OpenGL 2
executable with handover/pi/build-full-view.sh, from the repository root on
the 32-bit Pi 3 OS. The script produces ClassiCube-full-view without installing
it. A normal Linux/OpenGL 1 release build also compiles.

All geometry and visibility experiments are opt in. Adaptive distance now
requires gfx-adaptiveview=true. gfx-preserveview defaults true: allocation
failure cannot silently halve the requested distance.

## Measured results

These are actual accelerated VC4 measurements at 854x480 and distance 512,
with seed 12345 unless stated otherwise. They are exploration results, not a
release certification. Each listed run reported zero initially missing meshes.

| Run | Scene / mode | FPS | p95 frame ms |
| --- | --- | ---: | ---: |
| greedy6-normal-spawn | Frozen Normal static renderer | 88.16 | See raw JSON |
| greedy7-huge-spawn | Frozen Huge static renderer | 47.93 | 21.23 |
| greedy10-normal-walk | Frozen Normal movement | 45.56 | 25.79 |
| greedy10-huge-walk-budget6 | Frozen Huge movement | 24.30 | 64.03 |
| lod5-normal-live-walk | Live Normal, optional distant detail, capped 60 | 59.80 | 22.11 |
| lod5-huge-live-walk | Live Huge, optional distant detail, capped 60 | 36.36 | 39.66 |
| lod5-huge-live-seed2 | Live Huge, seed 22222, optional distant detail | 31.55 | 41.90 |
| lod5-huge-live-orbit | Live Huge aerial route, optional distant detail | 18.58 | 58.79 |

Settings, binary hashes, warmups, durations, frame distributions and raw logs
are preserved in the progress-results archive. The table includes different
experimental configurations; use each run's settings when reproducing it.
The live Huge walking test used a 30-second warmup and 30-second measurement.
Normal averaged approximately 60 but still had frame-time spikes.

An earlier distant-detail prototype reached 77.93 FPS for a frozen Huge
walking route. Its boundary-face optimisation was removed because neighbouring
detail changes and teleportation need stronger synchronisation to avoid cracks.
That speed does not describe the current safer prototype or live gameplay.

## Implemented experiments

- gfx-greedy2d: merge compatible opaque faces in two dimensions. Texture repeats
  in both directions through an OpenGL 2 shader. Uniform lighting and material
  must match; partial blocks and light gradients retain their geometry.
  The Pi lacks explicit texture-gradient shader support, so mipmaps enable a
  fallback to original meshes. Generated-world vertex savings were about 12%;
  a simple fixture dropped from 2768 to 492 vertices.
- gfx-flushverts: bound submitted terrain vertices per OpenGL job without
  discarding geometry. Large unsplit workloads caused VC4 allocation failures
  and GPU resets. Splitting jobs completed the full-distance tests.
- gfx-hizcull / gfx-hizmesh: software visibility using actual opaque mesh faces,
  framebuffer sample coverage masks, and conservative depth bounds.
- gfx-meshgrid / gfx-meshbands: group opaque faces spatially for visibility
  checks. Cutout and transparent geometry retain their ordering.
- gfx-hizbudgetus / gfx-hizreuse: limit depth-buffer construction and optionally
  reuse blocker candidates, rasterising them again for the current camera.
- gfx-hizvc4 / gfx-hizflatdepth / gfx-hizplanedepth / gfx-hizraybounds /
  gfx-hizraycache / gfx-hizrayexit: optional coverage and depth experiments.
  Occluders crossing the camera plane are clipped to an inset visible polygon.
  Queries crossing the near plane remain visible.
- gfx-multidraw: optional OpenGL multiple-range submission with a regular-draw
  fallback. More spatial groups and fewer API calls did not improve Huge play.
- gfx-framepacing: allow deadline pacing before swap with a fixed view distance.
  Its default is off; it does not enable adaptive distance.
- gfx-farlod: optional distant geometry prototype. The real block array,
  collision and saved world remain unchanged. Nearby chunk meshes use original
  blocks; farther meshes sample 2-, 4-, or 8-block cells. gfx-lodnear controls
  the nearby region, measured to chunk centres. gfx-lodstep=3 selects 8-block
  cells outside that region. Trees, thin structures, cave openings, colours
  and silhouettes can change at distance. This is a visible quality tradeoff,
  not an exact-image optimisation.

The distant-detail prototype keeps chunk volumes closed during asynchronous
updates, including teleportation. Detail refreshes respect the frame budget;
block-edit refreshes retain their existing priority. It remains experimental.
The supplied full-view-preview-options.txt is for an isolated test copy only.

## Validation and remaining issues

The benchmark completes initial meshing before warmup. Moving routes remain
stationary during this preparation. BENCH_READY counts meshes never built or
explicitly deleted; a detail refresh can keep its already-built previous mesh.
Every recorded distance must equal 512.

benchmark-freeze=true disables simulation, animation and HUD. Those runs measure
renderer capacity. Live tests keep simulation and HUD enabled. Screenshot runs
are separate and must not be used as performance evidence. No compilation ran
during the listed measurements.

Earlier actual-face culling configurations passed five image comparisons with
zero differing pixels. The latest expanded greedy10 suite passed 10 of 11:
Normal static/aerial/movement, Huge static/movement, shaft, mining opening and
closing, glass and outside-world views. Huge aerial differed by one pixel.
Therefore the current visibility experiments remain disabled in the supplied
distant-detail preview settings.

verify-full-view.py reproduces the image checks and fails on any difference.
Its comparison holds geometry constant and toggles visibility only. Comparing
the distant-detail prototype against full detail intentionally changes images;
the exported preview pictures show that tradeoff.

The next work is profiling live singleplayer tasks and mesh refreshes, reducing
the cost of closed boundary faces while preserving safe transitions, and
checking distant-detail appearance and mining/teleport behaviour. A 60 FPS
renderer-only average does not finish the goal.

The historical adaptive-distance measurements in pi3-performance.md do not
satisfy the current requirement.