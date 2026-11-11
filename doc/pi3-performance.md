# Raspberry Pi 3: measured 60fps configuration

Follow-up to `HANDOVER.md`, 2026-11-10. Tests ran directly on `kidspi.local`.

## Result and visibility tradeoff

The working desktop OpenGL build (`make linux RELEASE=1`) can sustain a paced
60fps in the tested Normal (256) and Huge (512) worlds at **854×480**, by adapting
the visible distance and fog to the workload. The complete world is retained;
only nearby terrain is meshed and drawn. This does not establish 60fps with the
entire Huge world visible, or under every possible workload.

The final configuration averaged 60.0fps in each of these tests, after 15 seconds
of warmup and during 40 seconds of measurement:

| Scene | Mean fps | p95 frame interval | p99 | Frames over 20 ms | Mean effective distance |
| --- | ---: | ---: | ---: | ---: | ---: |
| Normal, ground route, seed 12345 | 60.00 | 17.52 ms | 18.67 ms | 0.21% | 59.0 blocks |
| Huge, ground route, seed 12345 | 60.00 | 17.37 ms | 17.58 ms | 0.00% | 56.0 blocks |
| Normal, ground route, seed 54321 | 60.00 | 17.37 ms | 17.60 ms | 0.21% | 63.6 blocks |
| Huge, aerial route, seed 12345 | 60.00 | 17.44 ms | 17.99 ms | 0.08% | 64.0 blocks |

The actual desktop shortcut's 128x64x128 saved world also passed a 90-second
soak: 60.00fps, p95 17.28 ms, p99 17.61 ms, and 0.06% of intervals over 20 ms.
Its uncapped prior configuration averaged 86.87fps in the fixed-camera test;
the new setup caps it evenly at 60 and adds support for larger worlds. The save's
SHA-256 remained `44fe9cc2a24a0ff16bb1080933f3279a3d0fbe5ad2066ab7c1afe2e8845d1abf`.

Frame intervals are measured between instrumented client frame endings after
swap-buffer requests. They are not direct measurements of display scan-out.
Occasional longer frames remain; this is not a guarantee that every frame takes
16.67 ms or less. Ground routes follow terrain at about five blocks/second;
aerial routes orbit above the terrain and exercise chunk streaming.

**1080p fullscreen remains slower:** an uncapped adaptive test at 1920×1080
averaged 46.19fps (p95 23.37 ms), at the minimum 32-block view distance. The tested
60fps setup uses the 854×480 window. Fullscreen itself opens correctly.
The Classic pause-menu Quit game button was exercised on the Pi and exited
successfully.

## Evidence behind the approach

Initial uncapped fixed-camera measurements used seed 12345, 10 seconds of warmup,
15 seconds of measurement, and the existing rendering changes from `handover`:

| Configuration | Mean fps | p95 interval | Draw calls/frame | Reported vertices/frame |
| --- | ---: | ---: | ---: | ---: |
| Normal, default merge gap 64 | 26.71 | 44.50 ms | 1,107 | 284,980 |
| Small, merge gap 64 | 79.89 | 14.26 ms | 322 | 85,167 |
| Normal, merge gap 1024 | 23.85 | 47.56 ms | 671 | 453,701 |
| Normal, merge gap 0 | 26.57 | 44.17 ms | 1,128 | 284,477 |
| Normal, 426×240 | 31.75 | 42.27 ms | 1,101 | 284,312 |

More aggressive batching increased geometry and reduced performance. Quartering
the pixel count helped, but did not approach 60fps. A synchronized diagnostic
spent 28.142 ms/frame in residual GPU completion waiting. Together these support
reducing submitted terrain ahead of further draw-call overhead work; they do not
prove that vertex processing is the only GPU cost. Keep merge gap 64.

At fixed distance 96, aerial tests averaged 34.17fps in Normal and 33.00fps in
Huge worlds. Adaptive distance and bounded chunk loading were therefore needed.

## Settings used on this Pi

```ini
window-width=854
window-height=480
window-fullscreen=false
fpslimit=Limit60FPS
gfx-targetfps=60
gfx-minviewdist=32
gfx-chunkbuildms=1
gfx-drawmergegap=64
viewbobbing=false
sp-worldsize=128
```

Small remains the default for newly generated startup worlds, as previously
accepted. Normal/Huge generation is available in the existing menu. The desktop
shortcut loads the existing 128×64×128 `maps/byron-default.cw`; changing the
generation default does not resize an existing save. Benchmark tests load an
isolated copy of a saved map and do not save over the original.

`gfx-targetfps` defaults to 0 (adaptive distance disabled). When enabled, the
user's configured view distance is a ceiling. Distance starts at up to 96 blocks,
shrinks on slow frame windows, and grows cautiously with sustained headroom.
Fog and projection use the effective distance. Chunk loading keeps a nearby
buffer rather than building the whole configured view radius.

`gfx-chunkbuildms` is a soft budget for new chunk meshes (default 3 ms); it permits
at least one chunk per frame, prioritizes visible chunks, and permits four times
the budget when stationary. A single chunk can exceed the budget. Existing
changed meshes are rebuilt promptly. The missing option definition from the
parked handover patch is included.

With adaptive mode and a software frame cap, pacing occurs immediately before
the swap request using a wall-clock deadline. This prevents variations in CPU
render time from creating uneven swap-request intervals. The legacy limiter is
retained when adaptive mode is disabled. The adaptive controller excludes its
own pacing wait from spare-work calculations and considers frame intervals when
the game is capped.

## Benchmarking

The benchmark is opt-in and automatically exits after collecting its samples:

```ini
benchmark-mode=3
benchmark-seed=12345
benchmark-warmup=15
benchmark-seconds=40
benchmark-uncapped=false
```

Modes: 0 disabled; 1 fixed spawn camera; 2 aerial orbit; 3 terrain-following route.
The default seed is 12345. A loaded map retains its own contents. By default the
benchmark disables frame limiting; set `benchmark-uncapped=false` to test pacing.
`benchmark-sync=true` enables a GPU completion wait for diagnosis and changes
pipelining, so use unsynchronized runs for normal performance comparisons.

Samples are buffered in memory, then written as `BENCH_ROW` CSV records at exit,
so per-frame console output does not affect measurement. Draw-call counts include
translucent depth/color passes. The existing reported vertex counter excludes
the duplicated translucent depth pass. CPU driver/GPU queue waiting can appear
inside logic or draw submission timing; those columns are not pure CPU/GPU work.

`handover/pi/run-bench.py` creates isolated run directories and writes per-run
JSON summaries and logs. It expects the Pi session's paths under
`~/classicube-bench` and `~/classicube-autotune`. Map command-line arguments must
contain a directory separator, e.g. `./benchmark-map.cw`; a bare filename is
interpreted as a player name by ClassiCube.

## Machine and session notes

Pi 3, VC4 V3D 2.1, Mesa 24.2.8, accelerated desktop OpenGL under XWayland. No clock
or overclock settings were changed. Final checks showed no active throttling
flags; a historical ARM frequency-cap flag appeared after compilation, while
the final tests ran at the reported 1.2GHz CPU clock.

The network connection dropped repeatedly and later recovered without a reboot.
Wi-Fi power saving was disabled for the session; no persistent network profile
was edited. Kernel output inspected after recovery contained no GPU fault.
The older `make rpi`/EGL logout problem was not needed for this working GLX path.

Original executable and options were backed up under `~/classicube-bench`.
The original `pi3-60fps` branch is retained. No changes were pushed to GitHub.
