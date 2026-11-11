# Handover: ClassiCube at 60fps on a Raspberry Pi 3

> Current requirement: **preserve the full 512-block view** while working toward
> 60 FPS in Normal and Huge worlds. Adaptive fog/distance is no longer an accepted
> solution. See [full-view experiments and validation](doc/pi3-full-view.md).

> Follow-up, 2026-11-10: direct Pi testing now supports paced 60fps in Normal and Huge worlds at 854x480 with adaptive view distance. See [measured results and settings](doc/pi3-performance.md). The access constraints and unresolved status below describe the earlier session.

**Written:** 2026-11-10
**From:** Windows work PC, repo at `C:\source\classiCube` (Cowork / Claude Code session)
**Status:** In progress. Three optimisation/convenience commits shipped to the user's fork and running on the Pi. Classic (256) world is NOT at 60fps and is believed out of reach with cheap changes; the user has accepted playing a Small (128) world, which is smooth. A plan for the Huge (512) world exists but is not started.

---

## 1. What I was actually trying to do

Make ClassiCube run at a steady **60fps on a Raspberry Pi 3**. It started at ~30fps. The user is happy to patch the source. The objective narrowed over the session:

- First: raise fps generally. Turned out the "30" was partly a vsync artefact and partly a genuinely heavy frame.
- Then: the user plays in **Classic mode**, whose singleplayer world is 256x64x256 — four times the area of Enhanced's 128x64x128 — so Classic is where the frame rate actually hurts.
- Settled: Classic on a **Small (128) world** is smooth and is "fine" for the user. The remaining open goal is making **Normal (256)** or **Huge (512)** hit 60, which is unsolved.

## 2. Where things stand

- The user runs the **plain `make linux RELEASE=1` build** on the Pi (desktop OpenGL via GLX). The `make rpi` build (OpenGL ES 2 / EGL) **logs them out of the desktop on launch** — cause never diagnosed, and it was abandoned, not fixed. This predates any of our changes.
- `/client gpuinfo` on the Pi reports renderer **VC4 V3D**, so the GPU driver is active (not software `llvmpipe`).
- "Limit framerate" in Classic options reads **OFF** on the Pi, so the 20–30fps readings are taken as real, not a vsync cap. *(User-reported, not independently verified.)*
- Three commits are on the fork and built on the Pi. The first (rendering) commit gave a visible improvement **in Enhanced**; the GL1 draw-call commit gave **no visible change**; the convenience commit is new and untested on the Pi.
- One further change (chunk build time budget) is **written but deliberately parked, not committed** — see `handover/chunk-build-budget.patch`.

**Status markers matter in this doc. Most per-frame numbers are hardware-independent counts measured under software rendering in Docker; the Pi fps figures are user-reported and sparse.**

## 3. How I got here

### Environment and the hard constraint

- The work PC **cannot SSH out and cannot use USB** to reach the Pi. There is **no C compiler on Windows** (no gcc/clang/MSVC). **Docker Desktop works** (linux/amd64).
- Consequence: I can only ever verify **correctness** here (compile + screenshot-diff under software rendering). **All real speed measurement happens on the Pi, by the user.** Every round is: I commit → push to the fork → user runs `git pull && make linux RELEASE=1` → user reports fps. This round-trip latency shaped everything.
- The delivery channel to the Pi is **the user's GitHub fork** `https://github.com/ic3guy/ClassiCube` (git remote `fork`, branch `pi3-60fps`). This repo is a shallow clone of upstream `cec2707`. Commits use the identity **`ic3guy <ic3guy@users.noreply.github.com>`** (set via `git config --local`) to keep the user's real name/work email off a public repo — **preserve this; do not commit under any other identity.** The user's work email must never appear in commits or pushed files.

### The Docker verification harness (reusable — see `handover/docker/`)

- Image `cc-rpi-build`: `debian:bookworm-slim` + gcc, make, libx11-dev, libxi-dev, libegl1-mesa-dev, libgles2-mesa-dev, mesa DRI, Xvfb, imagemagick, xdotool, curl. Dockerfile in `handover/docker/Dockerfile`. **The image may still exist on the PC** (`docker images cc-rpi-build`, ~560MB); rebuild from the Dockerfile if not.
- Method: export two trees with `git archive HEAD` (baseline) and `git archive $(git stash create)` (working tree), extract in the container, `make linux RELEASE=1` (or `make rpi RELEASE=1`), run under `Xvfb` with Mesa **software** GLES/GL, screenshot with `import -window root`, compare with `compare -metric AE`.
- For comparable A/B screenshots, the scripts inject a **fixed world seed** (`seed = 12345;` in `src/Server.c`) into the test copy only.
- Gotchas that cost time: mount Windows paths with `MSYS_NO_PATHCONV=1` and a `pwd -W` path. **Bash heredocs containing apostrophes break in this Git-Bash-via-tool shell** — write scripts with the Write tool, not inline heredocs. Source files are **CRLF** natively; Makefiles are **LF**; keep that or diffs explode.
- Scripts recovered into `handover/docker/`: `build.sh` (rpi target), `linux.sh` (linux target A/B + pixel diff), `move.sh` (walking test), `measure.sh`/`gaps.sh` (inject draw-call + vertex counters, print per-frame averages), `ui.sh` (tests fullscreen/world-size/quit-button), `transfer.sh` (base64 patch round-trip test). They reference `/work/*.tar`; regenerate those with `git archive`.

### The three commits (all on the fork)

1. **`c89c1ab` "Reduce GPU and draw call cost of map rendering"** — the big one.
   - Split block meshes into **opaque / translucent / cutout** groups (new third group in `Builder.c`; `Builder_PartBase()` decides by `Blocks.Draw[]`). Opaque blocks (dirt/stone/grass) now draw **without alpha testing**, so the GPU can early-depth-reject hidden pixels. Only leaves/glass/sprites keep `discard`. New `MapRenderer_PartsCutout` parallel to Normal/Translucent throughout `MapRenderer.c`/`.h`.
   - **Merged draw calls**: adjacent in-memory sections of a chunk part are drawn in one `glDrawElements` with face culling on for the whole pass (`DrawPartSections()` with a `mergeGap`). Reduced spawn-view Classic draws 1531→1161.
   - **GL2 fog moved to the vertex shader** (`out_fog` varying) — removes a per-pixel divide/exp. **Only affects the ES2 build**, which the user does not run, so it is inert for them.
   - **Sky/clouds drawn after terrain** (`Game.c` `Render3DFrame`) so hidden sky pixels depth-fail.
   - **Chunk build backoff retargeted** from ~30 to ~60fps (`CHUNK_TARGET_TIME` in `MapRenderer.c`).
   - Options added: `gfx-opaquenoalphatest` (default true), `gfx-drawmergegap` (default 64).

2. **`52e2776` "Use one OpenGL call per chunk draw on the GL1 backend"** — on the *linux* build, the chunk draw path re-sent vertex/colour/texcoord pointers before every draw (4 GL calls/draw); now set once per chunk via `Gfx_BindVb_Textured` and draw with index-buffer offsets, matching how GL2 already worked. Option `gfx-fastchunkdraw` (default true). **Result on the Pi: no visible change.** This is the single most important empirical datapoint (see §4, GPU-vs-CPU).

3. **`9fc1122` "Add fullscreen and world size options, and a quit button in classic menu"** — convenience, not speed. `window-fullscreen=true` starts fullscreen (`Game.c`, calls `Game_ToggleFullscreen` after load). `sp-worldsize=N` overrides generated singleplayer world width/length, rounded down to a multiple of 16 (`Server.c`) — **applies to all modes**, so `128` makes Classic start Small. Classic pause menu gained the same "Quit game" button as the Enhanced menu (`Menus.c`; widget array grown to `[1+5+2]`). Both options default off/unset. **Tested in Docker (builds, quit button exits, fullscreen fills screen); NOT yet tried on the Pi.**

### Diagnostic detours that mattered

- **The jerkiness while walking was view bobbing, not fps.** Noclip flight felt smooth at the *same* 20–30fps. `viewbobbing=false` fixed the perceived stutter. Important: do not chase "stutter" as if it were a frame-rate bug.
- **noclip being smooth told me chunk building was NOT the walking bottleneck** — which is why the chunk-build-budget change got parked.
- **Classic render distance NORMAL did not help** near mid-map: I measured identical draw counts at viewdist 128 and 512 from spawn in the 256 world (the world simply isn't larger than the view there).

## 4. What is unresolved

- **GPU or CPU bound? (the key open question).** My best guess is **GPU vertex throughput**, ~2/3 confidence, NOT proven. Reasoning: Normal ≈291k verts/frame at ≈25fps ⇒ ≈7.3M verts/s; Small ≈125k verts/frame at that rate ⇒ ≈58fps, matching "smooth". The GL1 draw-call reduction (commit 2) changed nothing, which argues against CPU/driver call overhead being the limit; low resolution barely helped, arguing against pixel fill. **Two cheap Pi tests would settle it, both with Limit framerate OFF:** (a) fps in a Small world — ~55–60 ⇒ vertex-bound, well above ⇒ draw-call-bound; (b) Normal with `gfx-drawmergegap=1024` in options.txt — slower ⇒ vertex-bound, faster ⇒ draw-call-bound. **The user was asked to run (b) and never reported back.** Getting these two numbers should be the next session's first move — the Huge plan branches on the answer.
- **`gfx-drawmergegap=1024` on the Pi: never measured.** In Docker it cuts Classic draws 1161→711 but raises vertices 291k→452k (+56%). Whether that helps or hurts on VC4 is exactly test (b).
- **The parked chunk-build-budget change** (`handover/chunk-build-budget.patch`): adds a per-frame time budget for building chunks (`gfx-chunkbuildms`, default 3ms while moving, 4x standing still) and builds visible chunks before deferring hidden ones. Passed Docker correctness + a walking test. Parked because noclip was smooth, implying building isn't the moving-slowdown cause. It is still the right tool **if** a future fix (short view distance in a big world) makes constant chunk loading the bottleneck — e.g. Huge-plan phase 3. The patch is against the current fork tip; re-test before trusting it.
- **Convenience commit `9fc1122` unverified on the Pi.**
- **The `make rpi` logout** is undiagnosed. If anyone wants the ES2 path (and its vertex-shader fog), this must be understood first.

## 5. Facts and figures used

Per-frame counts are **Docker, software-rendered, hardware-independent** (they count draw calls / vertices the game issues, which don't depend on the GPU). fps figures are **user-reported from the Pi**.

- Classic 256 world, spawn view, linux build: **upstream 1531 draws / 286,600 verts; commit c89c1ab 1161 draws / 290,800 verts.** — *Sourced (measured in Docker, counters injected).*
- `gfx-drawmergegap` sweep, Classic 256: 0→1191 draws, 64 (default)→1161, 256→1001 (verts 318k), 1024→711 (452k), 65536→697 (467k). — *Sourced (Docker).*
- Enhanced 128 world: upstream 441 draws, c89c1ab 315 draws. — *Sourced (Docker).*
- Small (128) world ≈125k verts/frame. — *Estimated (scaled from the 128/256 vertex measurements).*
- Huge (512) ≈4,500 draws / ≈1.1M verts/frame, "probably single digits" fps. — *Estimated (extrapolated from 128/256; NOT measured).*
- VC4 vertex throughput ≈7.3M verts/s. — *Estimated (derived from one user fps reading of "~25" and the 291k count; treat as a single rough data point, not a spec).*
- Pi fps: Enhanced "smooth"; Classic "20 or 30", once "17"; commit 52e2776 "hasn't really improved anything". — *User-reported, approximate.*
- Pi: VC4 V3D renderer; Limit framerate OFF. — *User-reported.*
- Upstream base commit `cec2707`. Fork `github.com/ic3guy/ClassiCube`, branch `pi3-60fps`, commits `c89c1ab`, `52e2776`, `9fc1122`. Commit identity `ic3guy <ic3guy@users.noreply.github.com>`. — *Sourced (git).*

## 6. Files in this package

- **`HANDOVER.md`** — this file (output).
- **`handover/chunk-build-budget.patch`** — the parked, uncommitted chunk-build time-budget change against `MapRenderer.c` + `Options.h` (`gfx-chunkbuildms`). Working artefact; apply with `git apply handover/chunk-build-budget.patch`. Re-test before committing.
- **`handover/docker/Dockerfile`** — builds the `cc-rpi-build` verification image (input to the harness).
- **`handover/docker/*.sh`** — the Docker test scripts (working artefacts). They expect `/work/base.tar` + `/work/patched.tar` from `git archive`, and a `classicube.zip` texture pack (auto-downloaded from `http://static.classicube.net/default.zip`). CRLF-sensitive; `sed -i 's/\r$//'` them before running in-container.
- **The actual code changes are NOT here — they are the three commits on the fork.** `git log cec2707..pi3-60fps` shows them. This repo's working tree is clean apart from the untracked `handover/` folder.
- Two auto-memory files also persist at `C:\Users\w.denman\.claude\projects\C--source-classiCube\memory\` (`pi3-60fps-goal.md`, `verify-pi-build-in-docker.md`).

## 7. Suggested next step

**Get the two GPU-vs-CPU numbers from the Pi (§4) before writing any more rendering code.** Everything downstream — whether to merge draw calls harder or to cut geometry (hidden-chunk culling, simplified far terrain) — depends on which resource is the limit, and right now that's a 2/3 guess. Ask the user for: (a) Small-world fps, and (b) Normal-world fps with `gfx-drawmergegap=1024`, both with Limit framerate OFF.

If the user instead wants to proceed to the **Huge-at-60 plan**, it is a 5-phase plan whose core is **budget-driven render distance** (draw nearest chunks until a per-frame budget is spent, set fog to match — this is the only phase that *guarantees* 60, at the cost of view distance), preceded by a **benchmark mode** (scripted fixed-world flyover that prints a logic/draw-submit/GPU-wait frame breakdown — this doubles as the proper fix for the slow Pi round-trip, and finally answers the GPU/CPU question on real hardware). The parked chunk-build-budget patch feeds the plan's chunk-streaming phase.

**Delivery reminder:** code reaches the Pi only via the fork (`git push fork pi3-60fps`, then user `git pull`). Show the user the commit message and get an OK before pushing — the user has reviewed every push so far. Keep the `ic3guy` noreply commit identity.
