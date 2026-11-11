# Normal-world 60 FPS release for Raspberry Pi 3

The release target is Normal (256x64x256) at 854x480 with the full 512-block view.
The user approved simpler distant geometry. Nearby chunk meshes retain their
original blocks; distant geometry uses 8-block cells. The detail threshold is
48 blocks measured to chunk centres. Fog and view distance do not adapt.

## Measured validation

The installed candidate's SHA-256 is
ef1d94c4a43a77a20cbc2c5fe6692131d7acef08f737144947f0307db929be4b.

Tests used live simulation, animated textures and HUD, a 60 FPS cap, and
completed initial meshing before warmup. Every recorded distance was 512.
No compilation ran during these tests.

| Test | Duration | Frames | FPS | p95 ms | p99 ms | Frames >20 ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Normal walking, seed 12345 | 120 s | 7200 | 60.00 | 17.38 | 17.57 | 0.01% |
| Normal aerial route, seed 22222 | 60 s | 3601 | 60.00 | 17.34 | 17.55 | 0.06% |
| New saved Normal world, walking | 60 s | 3601 | 60.00 | 17.36 | 17.53 | 0.03% |
| Existing saved Small world, walking | 30 s | 1801 | 60.00 | 17.37 | 17.53 | 0.00% |

These results support smooth 60 FPS in the tested Normal worlds, with occasional
longer frames. They do not certify Huge worlds or every possible custom build.

Interactive testing generated a Normal world and verified block placement,
block removal, map saves, two teleports, and the pause-menu Quit button. A saved
edited world also reloaded successfully. GPU readback compared both animated
atlases with the original update method after 1179 tile updates: all pixels
matched. The original saved world was tested through isolated copies.

## Changes used by this release

- Two-dimensional opaque face merging and a shader that repeats atlas tiles.
- Simpler distant meshes, closed at chunk boundaries during asynchronous changes.
- Coarse terrain uses directional environment lighting; grass is confined to the
  exposed top layer rather than repeated down every row of an enlarged cell.
- Nearby block changes rebuild immediately. Distant refreshes keep their previous
  closed mesh and obey the per-frame build budget, reducing frame-time spikes.
- Animated atlas updates use fresh texture storage, retaining three old atlas
  versions until retirement. This avoids waiting on an atlas still used by GPU
  work. All animations remain enabled. The measured animation cost fell from
  about 7.1 ms to 0.23 ms per Huge-world frame in the diagnostic comparison.
- Frame pacing works independently of adaptive distance.
- View-preserving allocation handling and bounded OpenGL terrain jobs.

Software occlusion, portal culling, multiple-range submission and the parked
Huge-world pooling experiment are not enabled. Pooling code is excluded from
this release branch. Mipmaps are disabled for the repeating terrain format.

## Playing and saving

The installer adds Minecraft (Normal) on the Pi desktop, opening
maps/normal-60.cw. The existing Minecraft shortcut continues opening the original
saved world. The executable and performance options are shared.

Use Esc, Save level to save progress. The save screen pre-fills the world name;
confirm overwrite when saving the same world again. This release does not add
automatic saving.

## Build and rollback

Run handover/pi/build-normal60.sh from the repository root on the 32-bit Pi 3
OS. The native desktop OpenGL 2 build avoids the earlier EGL launch issue.
handover/pi/normal60-options.txt contains the tested performance settings.

Installation retains the old executable, options and all existing maps in a
timestamped directory under /home/pi/classicube-backups. Its manifest records
the source revision and hashes. Close the game and run that directory's
rollback.py to restore the previous build and settings; current worlds are kept.

The source release branch is normal-60-release. On the Pi, installation uses
a separate pi3-normal-60 branch so the experiment worktree stays separate.