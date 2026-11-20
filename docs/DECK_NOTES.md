# Steam Deck build container

Container `emerald-build` (distrobox, Arch Linux) on the Deck. Used for 32-bit builds.

## Storage

The SD-card variant failed. `distrobox create` succeeded but starting the container failed:

```
Error: unable to start container "...": crun: open `/run/media/deck/SD512/emerald-deck/containers/overlay/.../merged`: Permission denied: OCI permission denied
```

Fallback from the brief applied: `storage.conf` removed, container recreated on internal disk.
Podman graphroot is `/home/deck/.local/share/containers/storage` (internal NVMe, `/home`).
Do not retry the SD-card variant.

Leftover data from the failed SD attempt remains at `/run/media/deck/SD512/emerald-deck/containers` (about 415 MB). Some subdirectories are root-owned and unreadable by user `deck`.

## Commands that worked

```bash
# Step 2 (internal disk, no storage.conf)
ssh deck 'distrobox create --yes --name emerald-build --image docker.io/library/archlinux:latest'
ssh deck 'distrobox enter emerald-build -- echo container-ok'
```

The first create attempt (exit with `Installing basic packages... Error: An error occurred`) was a pacman sync timeout against `fastly.mirror.pkgbuild.com` and `geo.mirror.pkgbuild.com`. Retrying after removing the container worked. Mirrors answered from the host right after.

```bash
ssh deck 'distrobox rm --force emerald-build'
ssh deck 'distrobox create --yes --name emerald-build --image docker.io/library/archlinux:latest'
```

```bash
# Step 3: multilib + toolchain (guarded so a retry does not duplicate the multilib block)
distrobox enter emerald-build -- sudo bash -c 'grep -q "^\[multilib\]" /etc/pacman.conf || printf "\n[multilib]\nInclude = /etc/pacman.d/mirrorlist\n" >> /etc/pacman.conf; pacman -Syu --noconfirm && pacman -S --noconfirm base-devel cmake git pkgconf lib32-glibc lib32-gcc-libs lib32-mesa lib32-libglvnd'
```

Note: `lib32-sdl2` and `lib32-sdl2_image` are not in Arch multilib (see below), so the brief's package list fails on those two names. The command above omits them.

## Verification (Step 4)

```bash
distrobox enter emerald-build -- bash -lc 'cd /tmp && cat > t.c <<EOF
#include <stdio.h>
int main(void){ printf("%zu\n", sizeof(void*)); return 0; }
EOF
gcc -m32 t.c -o t32 && ./t32'
```

Output: `4` (32-bit pointers). `file` reports `ELF 32-bit LSB pie executable, Intel i386`.

## Known gap: 32-bit SDL2

- `pacman -Ss "^lib32-sdl"` in multilib returns only `lib32-sdl3 3.4.20-1`. No `lib32-sdl2`, no `lib32-sdl2_image`.
- `PKG_CONFIG_PATH=/usr/lib32/pkgconfig pkg-config --cflags --libs sdl2 SDL2_image gl` fails: `Package 'sdl2' not found`, `Package 'SDL2_image' not found`.
- `gl` resolves (`/usr/lib32/pkgconfig/gl.pc`, `opengl.pc`, `libglvnd.pc` present).
- Open decision: build SDL2 + SDL2_image 32-bit from source, or use another source. Not done here.

## Task 2: Deck tool and asset build (2026-11-13)

Repo sync:
- Bare repo `/run/media/deck/SD512/emerald-deck/repo.git`. Local checkout is shallow, so the bare repo needs `receive.shallowUpdate true` (set once). Push `deck-port:main`, clone to `.../emerald-deck/src` (branch `main`).

Host tools (`make -f make_tools.mk -j8`):
- Built OK: preproc, gbagfx, scaninc, mid2agb, jsonproc.
- First run failed: `png.h` missing / `libpng` not found. Re-run passed with no change; `libpng 1.6.59-1` and `/usr/lib/pkgconfig/libpng.pc` present. Likely the other agent's concurrent install was still in progress.

Assets:
- `bash ./tools/fetch_assets.sh` needs `bash`: the script has no exec bit in the clone (`Permission denied` on `./tools/fetch_assets.sh`).
- ROM check: `Pokémon Emerald (BPEE), USA/Europe 1.0: verified`. Download + manifest check + copy of 9168 files passed.
- Then fails: `ERROR: Python 3 with Pillow is needed`. Container has `/usr/bin/python3` but no `PIL`. `python-pillow` is not in the Arch repos under that name (pacman -Q / pip not run; no install per rule). Needs `python-pillow` or `pip install pillow` by the user.
- Impact: "Generating the port's files" step (intro margins) not done.
- `make -f vita/assets.mk -j8`: OK, 9300 binary assets, 420 songs. `include/constants/map_groups.h` exists after this step.

Status: partial. Blocked on Pillow for tools/fetch_assets.sh step 3.

## 32-bit SDL2 deps

Arch multilib has no lib32-sdl2. Built static -m32 SDL2 2.32.10 + SDL2_image 2.8.12 in container `emerald-build`.
Prefix: /run/media/deck/SD512/emerald-deck/deps32 ; sources: /run/media/deck/SD512/emerald-deck/build-deps

```
sudo pacman -S --needed --noconfirm lib32-alsa-lib lib32-libpulse lib32-libxcursor lib32-libxi lib32-libxrandr \
  lib32-libxss lib32-libxkbcommon lib32-libpng lib32-libxfixes lib32-libxrender lib32-libxtst lib32-pipewire \
  lib32-libdecor wayland-protocols wayland
cd /run/media/deck/SD512/emerald-deck/build-deps
curl -fsSLO https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz
curl -fsSLO https://github.com/libsdl-org/SDL_image/releases/download/release-2.8.12/SDL2_image-2.8.12.tar.gz
tar xf SDL2-2.32.10.tar.gz; tar xf SDL2_image-2.8.12.tar.gz
export PKG_CONFIG_PATH=/usr/lib32/pkgconfig:/usr/share/pkgconfig PKG_CONFIG_LIBDIR=/usr/lib32/pkgconfig:/usr/share/pkgconfig
P=/run/media/deck/SD512/emerald-deck/deps32
cd SDL2-2.32.10
cmake -S . -B build -DCMAKE_C_FLAGS=-m32 -DCMAKE_ASM_FLAGS=-m32 -DCMAKE_EXE_LINKER_FLAGS=-m32 -DCMAKE_BUILD_TYPE=Release \
  -DSDL_STATIC=ON -DSDL_SHARED=OFF -DSDL_TESTS=OFF -DCMAKE_INSTALL_PREFIX=$P
cmake --build build -j8 && cmake --install build
cd ../SDL2_image-2.8.12
cmake -S . -B build -DCMAKE_C_FLAGS=-m32 -DCMAKE_EXE_LINKER_FLAGS=-m32 -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_PREFIX_PATH=$P -DCMAKE_INSTALL_PREFIX=$P -DSDL2IMAGE_VENDORED=OFF -DSDL2IMAGE_SAMPLES=OFF -DSDL2IMAGE_TESTS=OFF \
  -DSDL2IMAGE_DEPS_SHARED=OFF -DSDL2IMAGE_PNG=ON -DSDL2IMAGE_JPG=OFF -DSDL2IMAGE_TIF=OFF -DSDL2IMAGE_WEBP=OFF \
  -DSDL2IMAGE_AVIF=OFF -DSDL2IMAGE_JXL=OFF -DSDL2IMAGE_QOI=OFF
cmake --build build -j8 && cmake --install build
# use:
PKG_CONFIG_PATH=$P/lib/pkgconfig pkg-config --cflags --libs --static sdl2 SDL2_image
```
SDL2 backends ON (dlopen at runtime): X11 (+xcursor/xinput/xrandr/xscrnsaver/xfixes), Wayland (+libdecor), KMSDRM, OpenGL, GLES, Vulkan, ALSA, PulseAudio, PipeWire, OSS. OFF: JACK, sndio, ESD, ARTS.
SDL2_image PNG comes from the bundled stb backend (no libpng link). Static libs; libSDL2.a dlopens X11/pulse/wayland, so the final link has only -lm -pthread (+ dl).
Test: SSH has no X auth; run with `XAUTHORITY=/run/user/1000/xauth_* DISPLAY=:0` (else SDL falls back to offscreen driver).

## Task 7: first run (2026-11-13)

Dist: `build-deck/dist/{pokeemerald,run.sh,app0/{voxel,ui,ui2,followers},userdata/}`. POST_BUILD seeds `userdata/settings.txt` from `tools/deck/settings.txt` only when absent.
3D off: key `voxel_3d=0` (and `voxel_battle=0`) in `userdata/settings.txt` (read by `VitaExtras_Init`, vita_extras.c). `pokeemerald.cfg` has no 3D key (`voxelRes` is ignored, `voxelFps` is only the frame cap).
Run over SSH: `DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/xauth_*) XDG_RUNTIME_DIR=/run/user/1000`, in the container. The game runs 20+ s without crash; smoke test passes.
Window: fullscreen 1280x800 by default (settings `fullscreen`, `resolution`, F11 and Alt+Enter change it, see FEATURES.md); the 960x544 picture is letterboxed through `DeckGlViewport` (glViewport macro in compat/vitaGL.h) with a scissor, bars cleared black in `DeckVideo_Swap`.
Open: (1) 8 early `Failed to queue audio: Invalid audio device ID` lines, then none (device opens with pulseaudio/pipewire drivers; alsa fails). Likely calls made before the device is opened; sound not verified by ear. (2) Intro scene shows repeated tile slivers at the left/right edges: the port's "intro margins" generation (tools/fetch_assets.sh step 3) was skipped (needs Pillow). (3) debug.log is absent (VITA_LOGGING off), perf_summary.txt exists but reports Vita labels.
Screenshot: `spectacle -b -n -f -o file.png` on the Deck host (no import/xwd/scrot in the container).

## Phase 1: swap interval (2026-11-13)

- GL_VERSION `4.6 (Compatibility Profile) Mesa 26.2.7-arch1.1`, GLSL `4.60`, renderer `AMD Custom GPU 0405 (radeonsi, vangogh, ACO)`, `vblank_mode` unset.
- Session: kwin_wayland with Xwayland; SDL video driver `x11`. No `~/.drirc`.
- `SDL_GL_SetSwapInterval(1)` after context creation: returns 0, `SDL_GL_GetSwapInterval` = 1.
- Finding: `VitaPlatform_ApplyTargetFramerate` (sdl2.c:1013) calls `eglSwapInterval` before the window exists; SDL answers -1 "Video subsystem has not been initialized". `DeckVideo_SetSwapInterval` therefore remembers an interval only after SDL accepted it, else the later real call at init would be skipped.
- Present pacing (60 and 30 FPS) not yet measured: needs a human route (xdotool input is unreliable, spectacle steals focus). Run the brief's Step 7 by hand.
- Measured with the autoplay harness (tools/deck/run_autoplay.sh): voxel_fps=1 presents at 16.67 ms (p95 16.85, p99 17.62), paced. voxel_fps=0 still presents at 16.67 ms: the interval-2 request only happens before the window exists and fails; nothing re-requests it after init. Needs a fix before the 30 FPS cap works on Deck.

## Phase 1: post presets (2026-11-13)

Task 5 was reduced to reading the code and recording state. No value was tuned and no human judged the look; Marcos will judge all of it in one pass later.

Current values in `src/platform/deck/deck_post.c` (`kPresets`, columns grade, saturation, vignette, bloomGain, threshold, bloomPasses):
- Low: 0.60, 1.04, 0.12, 0.0, 0.70, 1
- Medium: 0.80, 1.06, 0.18, 3.0, 0.65, 1
- High: 1.00, 1.12, 0.30, 5.0, 0.58, 2
- `kShadowTint` (day, dusk, night): {0.94,0.97,1.06}, {1.02,0.90,1.08}, {0.86,0.92,1.12}
- `kHighTint` (day, dusk, night): {1.04,1.01,0.95}, {1.10,0.97,0.84}, {0.97,1.00,1.05}

Checked in code: Medium grade 0.80 is at most 0.85 (faithful plus), and Low `bloomGain` is 0, so Low never blooms (`DeckPost_Run` also skips the bloom passes when `light.bloom` is 0).

Weather with `light.bloom > 0` (`BaseLightFor`, `voxel_renderer.c` lines 2825-2893): SUN 0.10, RAIN 0.08 + 0.30 * bolt (spikes at lightning), SAND 0.16, ASH 0.10, PARTICLES 0.12, SHADE 0.12. FOG and the default (indoor) case have no bloom (0.0).

To judge later (Marcos):
- Dusk and night grade looks heavily teal at the 19:19 clock; shots `.claude-logs/t4/q1.png` and `q2.png`.
- Indoors the post passes `daylight=1, glow=0`, which selects the day tint, not a neutral one.
- `Pass_DayNightGrade` (in-scene tint) stacks with the post grade, so the time-of-day grade is applied twice.
- No noon shot has been taken yet.
- The warp fade-to-black and fade-to-white tint check is not done (the autoplay script has no warp).
- Rain with lightning: bloom spike following `bolt` not observed.

## Phase 2: wind sway (tree crowns, long grass)

- Weights are baked at chunk build: `VoxelBuilder.swayW` (parallel byte array, `voxel_mesh_builder.h`), written in `VoxelBuilder_Tri` from the input vertex height over the crown foot; packed as alpha = 255 - weight in the tree-phase `PackVoxelVertices`. 255 = still. `VoxelVertex` itself is unchanged (building model files are read into it).
- Shader `src/platform/deck/deck_sway.c`: phase from world position (`gl_Vertex.xz + uChunkOffset`, offset = chunk draw offset minus map-crossing shifts), wind from `uWind`/`uTime`. Amplitude constants (to tune by eye): 0.8 tile x strength, damped `0.25 + (s-0.25)*0.5`; weight top 0.5 for crowns, 1.0 for long grass.
- Setting `voxel_sway` (0/1, default 1, EFFECTS group); also off with `voxel_shaders=0` or if the program fails to build (old fixed-function path).
- Verified (field.txt, still.txt, DECK_GL_DEBUG=1): 0 GLERR, `[SWAY] ready`; static camera screenshot pairs differ in the tree rows (13k and 7.5k of 51k samples over 2 to 4 s); crowns intact over trunks and between rows.
- Not checked: crossing a map border (phase continuity), long grass on Route 119/120, rain/sand amplitude, sway with `voxel_sway=0` picture.

## Phase 3: procedural ambient bed

- Plan: `docs/superpowers/plans/2026-11-13-deck-phase3-ambient.md`. Synth: `src/platform/deck/deck_ambient.c` (pure DSP, no game headers). Game mapping and hook: `deck_ambient_game.c`, called from `Platform_QueueAudio` (`sdl2.c`) after the master volume loop, on the game thread.
- Layers: wind (gust-modulated low-passed noise), rain (high-passed noise plus droplet ticks), waves (swell-shaped noise, 6 to 10 s), crickets (4 chirp-train voices), birds (3 voices, swept sine calls), cave (low tone plus drips), thunder (on the lightning edge). Slew 2.5 s per layer (rain 1.5 s), 1 s master fade; indoors, battles, menus (VitaUI, new UI, vanilla start menu window) and fast forward are silent. Thunder tail about 7.5 s (tau 1.2 s); a bolt during a rumble queues one more.
- Setting `ambient_volume` (0 to 10, default 6, 0 = off with no DSP). Final gain = ambient_volume/10 x master volume.
- Data test: `tools/deck_tests/test_ambient.c` (build line in its header, run in the distrobox): NaN, peak, RMS band, clipping, block seam steps, off switch, master 0 silence, fade, fast-forward mute ramp, no write past the frame count.
- Log lines (only with `DECK_GL_DEBUG` set and not 0, at most 2000 lines per process) in `userdata/debug.log`: `ambient: mapType ... -> wind ... master` on every context change and `ambient: 5s out peak rms queue min max` every 5 s. `VITA_LOG` is buffered and never flushed on kill, so use debug.log direct appends for short runs.
- Not checked by ear: loudness against the music, bird/cricket character, wave pacing, thunder delay, crossfade feel, water radius at beaches.

## Phase 4: standing tall grass, player bend, flowers

- Plan: `docs/superpowers/plans/2026-11-13-deck-phase4-grass.md`. Setting `voxel_grass` (0/1, default 1, row "GRASS & FLOWERS" in EFFECTS); also off when `voxel_shaders` is 0 or the sway program failed (`DeckGrass_Enabled`), then the old flat look. Toggling bumps `sWorldEpoch` and the flag is in the chunk content hash, so chunks rebuild.
- Tall grass (E1): cells whose behaviour is MB_TALL_GRASS (and not a tree part) get the long grass tuft (tree atlas 80,32) as a leant card, 0.55 of its height, `EmitTallGrass` in `voxel_tree.c`, tree phase of the chunk worker, one `VoxelLighting_Sample` at the foot (`lightingConstant`). The ground pass is unchanged; `VoxelTree_Part` is unchanged (lighting, entities and battle read it). The GBA tall grass field effect is skipped where a tuft stands (`VoxelTree_TallGrassStands`, `voxel_entities.c`).
- Cap: `CHUNK_TREE_CAP` 768 to 3072 (staging 516 KB worst case; VBOs are sized to the actual count; uint16 vertex indexes top out at 21,504).
- Bend (D3): grass cards carry bit 0 of the sway weight byte (`builder->swayBend`; weight stored even). `VoxelEntities_GetWalkers` = player + nearest 3 visible objects within 4 tiles; uniforms `uBend0..3`, `uFrameOffset` (chunk draw offset, the walkers are in the current frame). Lean 0.6 tile away, sink 0.3, falloff (1 - d/0.9)^2 x weight. No CPU history/spring: recovery is the falloff as the walker moves on.
- Flowers (E2, procedural): the General metatiles that show tiles 508-511 (`VoxelWorld_IsFlowerCell`, read from the metatile entries once) get two small leant cards a cell (hash of the cell picks place and one of 4 colours). Art is painted by `deck_flowers.c` into the free atlas rows py 112..123 when `trees.rgba5551` loads (graded like the rest). Mauville and Ever Grande flowers are NOT covered (offsets not read, several animation phases). The ground keeps its flat animated flowers.
- Debug: `[GRASS] chunk x,y tufts N treeVerts M (built B, cap C) treesMs T` lines in `gl_errors.log` (first 150 chunks).

## Phase 5: rain (GPU streaks C2, wet ground C4, mist C5, splashes C3)

- Plan: `docs/superpowers/plans/2026-11-13-deck-phase5-rain.md`. Code: `src/platform/deck/deck_rain.c/.h`, hooks in `voxel_renderer.c` (`Frame_ReadAtmosphere`, `Pass_Ground`, `Pass_Atmosphere3D`). Setting `voxel_rain` (0/1, default 1, row "RAIN EFFECTS" in EFFECTS); also off with `voxel_shaders=0` or when the programs failed: legacy rain particles and flat ground.
- Streaks: 2048 quads in one static VBO, positions computed in the vertex shader from the same wrapped box as `DrawWeather`; legacy `DrawWeather(rain)` is skipped when they draw. Wet ground: `Pass_Ground` runs a ground program while the wetness `sWet` > 0.01 (ramp 20 s in rain, 90 s to dry, outdoors only), normals from derivatives. Mist: `DrawFogBanks` again, grey-blue, `smoothstep(0.35,1,amount)*0.5*sWet`. Splashes: 1024 ring quads on the camera ground plane (`sCamera.ground + 0.03`), spot re-hashed each cycle in the vertex shader.
- Debug env (read once): `DECK_FORCE_WEATHER=rain|storm|sun|fog|sand` (Deck build only, overrides weather, amount, fog density and bolt in `Frame_ReadAtmosphere`; storm = bolt every 8 s; audio not forced); `DECK_RAIN_OFF=streaks,wet,mist,splash` (comma list). Log: `[RAIN]` lines in `gl_errors.log` (init, every 10 s amount and wet).
- Autoplay: `EXTRA_ENV="DECK_FORCE_WEATHER=rain" tools/deck/run_autoplay.sh tools/deck/autoplay/still.txt 75 <abs outdir>`; wet reaches 1.0 about 30 s after the world appears.
- Verified (Deck, `DECK_GL_DEBUG=1`, 0 GLERR in every run): streaks visible in the 60 s shot; wet ground darker (mean RGB 41/81/83 vs 45/89/91 with `wet` off, 34% of pixels differ) with bluer puddle patches; mist hazier (18% of pixels differ, mean +2 to +3); rings visible on the ground with splashes on; `voxel_rain=0` run for the fallback.
- Not checked: looks and amounts by eye, the storm flash (frame not on a bolt), performance (deferred), battles (rain is not drawn there), splashes on cliffs/water/roofs (they use the camera ground plane, so they float or hide there; relief heights are the follow-up), wet look on every tileset, puddle scale, sand/fog/snow (unchanged).

## Phase 6: particles (F2), water glints, character outline (G4), soft shadows (B4)

- Plan: `docs/superpowers/plans/2026-11-13-deck-phase6-particles.md`. Code: `src/platform/deck/deck_particles.c/.h` (particles, glints), `deck_chars.c/.h` (outline, blobs); hooks in `voxel_renderer.c` (`Pass_Ground`, `Pass_CastShadows`, `Pass_Entities`, `Pass_Atmosphere3D`, entity shadow alpha) and `voxel_entities.c/.h` (`VoxelEntities_GetBlobs`, `VoxelEntities_CharacterRange`, behind PLATFORM_DECK). Settings (EFFECTS, default 1, also off with `voxel_shaders=0` or a failed program): `voxel_particles` ("PARTICLES": pollen, fireflies, leaves, glints), `voxel_outline` ("CHARACTER OUTLINE"), `voxel_shadow` ("SOFT SHADOWS").
- Debug env (read once): `DECK_FORCE_TIME=dawn|noon|dusk|night` (Deck only, `VitaDayNight_Update` overrides light and sun after its own computation, outdoors only; the clock and saves are not touched; consumers of `VitaDayNight_Get/GetSun` see the forced hour, including the ambient bed (it reads `VitaDayNight_Get`, so birds and crickets follow it), grade, glow, motes, sun for shadows). `DECK_PART_OFF=pollen,fire,leaf,glint` single effects off. Example: `EXTRA_ENV="DECK_FORCE_TIME=night DECK_FORCE_WEATHER=sun" tools/deck/run_autoplay.sh tools/deck/autoplay/still.txt 75 <abs outdir>`; `SETKEY="voxel_particles=0"` for the B shot.
- Particles: 288 quads in one static VBO (pollen 192, fireflies 64, leaves 32), seed in the vertex, motion in the vertex shader inside the mote box (22 x 18 around the camera target), wrapped like the rain; three draws (pollen and leaves alpha, fireflies additive). Pollen count follows `light.motes` (sun, daylight) and replaces the 32 legacy motes when drawn; fireflies when `glow` > 0.3 on route and town maps; leaves by the tree vertex count of the visible chunks (not anchored to trees). None in rain, none indoors, underground or in 3D battles; no pollen or leaves in fog, sand, ash or snow. `[PART]` line in gl_errors.log every 5 s under `DECK_GL_DEBUG`: counts per kind (the exit test's particle count).
- Glints: the ground uses a glint program (texture x colour as the fixed path) where the wet program is not bound; sparkles from a cell hash of the world position on blue texels (`b - max(r, 0.8 g)`), strength from sun and glow, none in rain. No builder change (no water flag in the vertices).
- Outline and rim: only the object cards' vertex range goes through the outline program (doors, effects and the occluded-player silhouette stay fixed pipeline). Dark one-texel outline on clear texels beside a sprite texel; warm rim on texels whose neighbour toward the sun is clear. The quad is the sprite rectangle, so the outline only shows inside it.
- Soft shadows: a radial blob (0.8 x 0.52 tiles, peak 0.20 x light of the card) under every outdoor, non-riding card, pushed 0.07 along the sun, drawn before the silhouette shadows whose alpha is x0.62 while blobs are on (peak 0.22; overlap about 0.38 vs 0.36 before).
- Verified: see HANDOFF "Phase 6 status".

## Settings levels and camera tilt (2026-11-14)
Every Deck effect has its own row in WORLD > EFFECTS (ambient volume stays in GENERAL > SOUND). Level encoding of the effect keys, stored by key name in settings.txt: 0 OFF, 1 AUTO (follows `voxel_quality`: LOW/MEDIUM/HIGH), 2 LOW, 3 MEDIUM, 4 HIGH; any other value counts as AUTO. AUTO is 1 on purpose: old files stored 1 for "on", so old 0/1 keys keep their meaning with no migration. Only `voxel_bloom` and `voxel_grade` used 2 for AUTO: `MigrateLevelSettings` (vita_extras.c) maps 2 to 1 when the file has no `fx_levels` key (then writes `fx_levels=1`). The old `voxel_rain=0` also switches off the keys split from it (wet, mist, splash), and `voxel_particles=0` the glints. `voxel_shaders` stays the master switch.

| Row | Key | OFF | LOW | MEDIUM and HIGH |
|---|---|---|---|---|
| COLOR GRADE | voxel_grade | none | preset 0 strength | preset of AUTO/level (HIGH = strongest tint, vignette) |
| BLOOM | voxel_bloom | none | gain 1.5 | gain 3.0 / 5.0 (old presets) |
| DIORAMA BLUR (DOF) | voxel_blur | none | band blur x0.5 | x1 |
| WIND SWAY | voxel_sway | fixed trees (also no grass bend) | amplitude x0.5 | x1 |
| GRASS & FLOWERS | voxel_grass | flat ground | 50% of the cells (hash of the cell) | 100% |
| RAIN STREAKS | voxel_rain | legacy rain | count x0.5 | x1 |
| WET GROUND | voxel_wet | dry | puddles x0.5 | x1 |
| RAIN MIST | voxel_mist | none | opacity x0.5 | x1 |
| RAIN SPLASHES | voxel_splash | none | count x0.5 | x1 |
| PARTICLES | voxel_particles | legacy motes | count x0.5 | x1 |
| WATER GLINTS | voxel_glint | none | strength x0.5 | x1 |
| CHARACTER OUTLINE | voxel_outline | none | strength x0.5 | x1 |
| SOFT SHADOWS | voxel_shadow | silhouette only | blob alpha x0.5 | x1 |
| 3D RESOLUTION | voxel_res | 960x544 | 960 wide | MEDIUM 1120x636, HIGH 1280x726 (AUTO follows the preset) |
| TILT-SHIFT | voxel_tiltshift | the old band blur | radius x0.6, 8 taps | x1 12 taps / x1.4 16 taps |
| AMBIENT OCCLUSION | voxel_ao | none | radius 0.88, 2 slices x 4 steps | MEDIUM 0.88, 3 x 5; HIGH 1.05, 3 x 6 |
| LIGHT SHAFTS | voxel_shafts | none | gain 0.35 | gain 0.65 / 0.9 |
| RELIEF (BUMP) | voxel_bump | the old picture (old programs and paths) | multiplier +-10% | +-15% / +-20% |
| LUT GRADE | voxel_lut | the procedural grade | S-curve x0.5 | x1 / x1.3 |
| SUN SHADOWS | voxel_sunshadow | baked cast rays (the old picture) | map 1024, 2x2 PCF | MEDIUM map 2048, 3x3 PCF; HIGH 2048, 5x5 at 1.25 texel spacing |
| BATTLE BACKDROP | voxel_battledim | the crisp battle stage | 1 blur pass, overlay 0.25 | MEDIUM 2 passes, 0.40; HIGH 3 passes at 1.6x step, 0.55 |
| GROUND DETAIL | voxel_groundrelief | none | multiplier amplitude 0.20 | 0.35 / 0.50 |
| UI DEPTH GLOW | voxel_uiglow | none | 8 taps, radius 2 px, 0.12 each | 12 taps (2 rings) 0.10 / 16 taps 0.085 |
| CAMERA TILT | voxel_pitch | CLASSIC 40, 36, 32 (default), 28, 24 degrees (own values, not levels) | | |

MEDIUM equals the look before the levels, HIGH equals MEDIUM except for bloom and grade (which had presets). Bloom LOW is a weak glow instead of none (the LOW preset used to have no bloom).

Camera tilt: `CtrSettings_VoxelPitch()` (voxel_platform stub header, Deck branch) reads `voxel_pitch` every frame through `AdaptDistance`, so a change applies live. The horizontal camera distance is multiplied by cos(pitch)/cos(40) to keep the eye-to-target length (tile size on screen); the chunk rows north of the target grow with the pitch (the top ray of a low camera reaches further): `northTiles` in the chunk scan, max 46. Battle pitch (30) and the swoop (lands at 45 from whatever pitch it starts) are untouched. The DoF band rows (226 top, 127 bottom, tuned at 40) are left as they are.

## Phase 7: tilt-shift, scene resolution, light shafts, LUT grade
Plan: docs/superpowers/plans/2026-11-13-deck-phase7-post2.md. All in `deck_post.c`. The composite shader is one body with `#ifdef POST_X`; it is built with POST_X first and, if that fails, without (the phase 1 composite). Env `DECK_POST_NO_X=1` forces the plain composite (fallback test), `DECK_POST_FORCE_FAIL` the whole legacy path. Texture units: 0 scene, 1 bloom, 2 blur or tilt copy, 3 scene depth, 4 LUT, 5 shafts.
- Tilt-shift: circle of confusion from the linearised scene depth (`2nf/(f+n-(2d-1)(f-n))`, near 1, far 62) around the eye to target distance, `smoothstep(0.06, 0.40, |z-zf|/zf)` with the near side x1.4; gather of 8/12/16 golden-angle taps on the half-res copy weighted by `1 + 3 lum^2`, radius up to 1.4 percent of the height. Because it uses the real depth it follows `voxel_pitch` (no screen rows). Replaces the band blur when on and the scene target has a depth texture, outdoors; indoors both are off.
- 3D resolution: scene target 960 / 1120 / 1280 wide (height 544 / 636 / 726), recreated by `DeckPost_WantScene` when `voxel_res` changes (a failed create goes back to the old size and is not retried). The 3D viewport takes the target size, the projection aspect stays 960/544, so every mapping of the logical 960x544 (UI mirrors, tags) and `deck_shot.c` (reads the window) are unchanged. At 960 the composite samples NEAREST, above that LINEAR. The half-res blur steps scale with the width. UI and 2D stay 960x544.
- Light shafts: half-res radial march (24 taps, decay 0.95, bright part above luminance 0.62, jittered start) toward a fixed point above the picture (x = 0.5 - sun dx x 0.25, y = 1.3), added warm in the composite before the grade. Amount = max(sun strength, glow) x clamp(daylight / 0.25); zero in rain, fog (density 0.05), sand, ash, indoors, underground and at night. The true sun position is above and beside the frustum at these pitches, so a projection would be unstable.
- LUT grade: 256x16 RGB8 strip (16 slices of blue) built on the CPU: the procedural grade (tint by luminance, saturation, strength of the preset) plus an S-curve (0.20 day, 0.32 dusk, 0.14 night, x level); rebuilt when daylight or glow moved more than 0.004 or a level changed. Sampled with two bilinear taps; the fade stays a mix in the shader; the vignette stays analytic. `voxel_lut` off: the old grade code, unchanged.
- Verified: see HANDOFF "Phase 7 status".

### Adding a setting
A leveled row is three edits: (1) a `FAST_EXTRA_VOXEL_<NAME>` id in `include/platform/vita_extras.h` (Deck block), (2) one `if (strcmp(key, "voxel_<name>") == 0) return ...` in `LookupFastId` (vita_extras.c, case 'v'), (3) one row in `gVitaExtras` with `5, 1, sVoxelLevelValues` (count 5, default AUTO). In the reader include `deck_levels.h` and use `DeckLevel(id)` (-1 off, else 0..2) or `DeckLevelPick(id, low, medium, high)` (0 when off). Labels: at most about 18 characters (the label column is 156 px of the 320 px menu); values at most 8. Tab row arrays hold 48 rows (vita_ui_options.c) and 64 (ui_scr_options.c).


## Phase 9: relief (voxel_bump)
Pseudo normal map, Deck only. The height is the luminance of the albedo texel; a 3x3 Sobel on the texel neighbours (nearest, integer offsets, so the pixel art stays crisp) tilts a per pixel normal built from the screen derivatives; the sun from `gVoxelSun` (toward-the-sun = -dx, up, -dz, 45 degrees) lights it, a cavity term adds light independent depth, and the result is a multiplier around 1.0 on the colour each program already computes (LOW 0.10, MEDIUM 0.15, HIGH 0.20). Dusk and night keep a third of the directional term. Where the baked vertex colour is dark the directional part fades (no double darkening with cast shadows); in rain the relief is scaled by 1 - 0.4 x wet and the wet sheen is untouched. Shared GLSL and the plain program for buildings: `deck_bump.c`. Surfaces: ground (wet and glint programs, plus the wet program at wetness 0 for dry ground without glints), building pages (plain bump program), trees and grass (sway program, amplitude 0 when sway is OFF), character cards (outline program, outline alpha 0 when the outline is OFF). Not done: parallax (sub-texel offsets tear nearest edges and move alpha tested edges; not judgeable without tuning rounds). Plan: docs/superpowers/plans/2026-11-14-deck-phase9-bump.md. Building pages have no slot (a clear neighbour takes the centre height), so seams between pieces of building art can show faint lines (the gradient is soft clamped).

## Phase 8: sun shadow map (voxel_sunshadow)
Plan: docs/superpowers/plans/2026-11-14-deck-phase8-sunshadow.md. Code: `deck_sunshadow.c/.h`, `POST_SUN` block in the composite of `deck_post.c`, `Pass_SunShadowMap` in `voxel_renderer.c`, `VoxelLighting_SetNoCast` in `voxel_lighting.c`. Casters are drawn depth only (fixed pipeline, alpha test for crowns and cut-out building pages, character cards) from the sun into a texel-snapped 64 x 64 tile box around the camera target. The receivers are all resolved in the composite from the scene depth texture (unit 3) and the map (unit 6): world position from the inverse of P * MV, normal from the derivatives (forced to face the viewer), slope scaled bias and normal offset, PCF, edge fade, multiplier = ambient / 0.9 inside shadow. A face turned from the sun (n.L <= 0.05) is skipped (its baked face shade is already dark). Not drawn at sun strength < 0.04 (night), in battles, indoors, underground.
- Double darkening: while on (>= LOW and the composite built with `POST_SUN`), `Lit()` returns true at once, so the chunk builder skips every baked cast shadow ray; contact AO and the sun angle face term stay. The flag is part of `VoxelLighting_Hash` and a change bumps `sWorldEpoch` (worker idle first), so the chunks rebuild when the setting changes. OFF is the old baked picture.
- Older shadows: the flat character cast silhouettes (`EmitCastShadow`) are skipped while the map of the last frame was drawn (the characters cast through the map); the soft blob (`voxel_shadow`) and decal shadows stay.
- Constants (`deck_sunshadow.c`): SS_HALF 32, SS_DIST 48, SS_RANGE 96, SS_FACE_REF 0.90, SS_MIN_SUN 0.04; bias `tw * (1 + 3 sinT / max(n.L, 0.25))` capped at 10 texels (composite). Fallbacks: no `POST_SUN` composite or no depth texture -> off and the baked rays come back; env `DECK_SUNSHADOW_OFF=1`. Log: `[SUNSHADOW]` lines (every 300 frames the map pass CPU ms and draw counts).
- Limits: ground and cliffs are not casters; no sway in the map; tall grass tufts share the tree range so they cast too; one box, no cascade; the bloom and tilt-shift copies are taken from the unshadowed scene.

## Phase 10: battle backdrop blur and dim (voxel_battledim)
In a 3D battle the 3D stage is the backdrop only: the battlers, the HP boxes and the text come from the GBA overlay (`Pass_Overlay2D`), drawn after the post pass, so they stay sharp. `DeckPost_WantScene(..., battle)` now lets the scene go through the post target in a battle when `voxel_battledim` is on; `DeckPost_Run` with `in->battle` turns every other effect off (no grade, bloom, tilt-shift, shafts, LUT, sun) and only blurs the scene copy (half resolution, 1 / 2 / 3 passes, step 1 / 1 / 1.6) and lays black over it (0.25 / 0.40 / 0.55), both scaled by the screen fade. Uniform `uBack` in the composite. Constants `kBackPasses`, `kBackStep`, `kBackDim`. OFF = the crisp stage as before.

## Phase 11: stronger relief on models (voxel_bump)
Amplitudes 0.10 / 0.15 / 0.20 -> 0.18 / 0.30 / 0.45; in the GLSL block the sun term gain 1.6 -> 2.8, the cavity 3.0 -> 4.5, the tilt of the normal 3.0 -> 5.0, the night floor of the lit factor 0.4 -> 0.55. OFF unchanged.

## Phase 12: ground micro-relief (voxel_groundrelief)
`deckGroundMul` in the shared block (deck_bump.c), called by the two ground programs (wet and glint; `DeckRain_BumpBegin` also runs for this setting alone). The ground kind comes from the texel colour (grass: green over red and blue; sand: bright and red over blue; path: the rest; blue texels = water get none): grass soft clumps (noise at 1.5 and 3.6 per tile), sand ripples (a warped sine), path grain (9 and 17 per tile) plus a low frequency undulation (0.16 and 0.07 per tile) that tilts the light slowly. Finite differences of the height give a normal, lit by `uBumpSun` (world space), fades on cliffs and walls by the derivative normal. Shading only, no vertex change. Amplitude 0.20 / 0.35 / 0.50. No tile or slot id exists in the ground vertices, so the colour classifies it.

## Phase 13: animated sun and moon rays (voxel_shafts)
The radial march over the scene is gone. `kShaftFs` draws procedural beams at half resolution: 3 / 4 / 6 layers of slanted bands (constant along x + slant * y, the slant from the sun focus), each with its own density, drift speed and direction, breathing (a sine) and dust sparkles inside the beams, strongest at the top. Colour: warm by day, golden at dawn and dusk (glow), pale blue for the moon at night with motion x0.45. Weather factor `wf` (renderer): sun 1.0, clear 0.8, fog or fog density >= 0.05 1.4, shade 0.45, sand 0.5, rain 0, others 0.35; the moon amount is (1 - daylight) x 0.5 x wf. Gain by level as before (0.35 / 0.65 / 0.9).

## Phase 14: UI depth glow (voxel_uiglow)
`deck_uiglow.c`: black, faint copies of the UI drawn first around a short offset (1, 2.5 px) in 1 or 2 rings. Two hooks: `Ui_Render` (the new UI list: menus, HUD; a black copy of the vertex list with the element's alpha x the tap opacity, drawn N times with a modelview shift) and the GBA overlay quad in `voxel_renderer.c` (dialogue, battle: `glColor4f(0,0,0,alpha)` on the same quad). The 3D scene and the UI itself are untouched. The battle overlay also holds the battlers' sprites, so they get the halo too. Env `DECK_UIGLOW_OFF=1`.


## Phase 13b: rays at the angle of the sun and moon (voxel_shafts), 2026-11-14
The beams no longer use a fixed slant. `Pass_DeckPost` (voxel_renderer.c) projects the light direction through the real camera and passes the unit screen direction (`beamX, beamY`, x right, y up) and the sine of the light elevation (`beamElev`) to `DeckPost_Run`; `kShaftFs` runs its bands along `uDir` (`a = dot((x * aspect, y), perp(dir))`) and the falloff exponent `uLen` makes low light longer (0.85 at dawn and dusk, 1.34 overhead).
- Light vector: world toward-the-light `(-dx, 1, -dz)`, the same convention as the baked rays and the shadow map (`DeckSunShadow_Begin`). Sun path: `VitaDayNight_SunPathAt(timeMs)` (vita_daynight.c), `dx = -1.35 cos(p pi)`, `dz = 0.35 + 0.20 sin(p pi)`, p = (minute - 240) / 960 over 04:00 to 20:00, held on the horizon point outside it (continuous, unlike the 15 minute `SunAt` steps). Moon path: `VitaDayNight_MoonPathAt`, same shape over 18:00 to 06:00 (p = (minute - 1080) / 720, east to west), held outside it. The daynight module defined no moon before, so this is a new definition (rises in the east at dusk, opposite end of the sky from the setting sun). `VitaDayNight_BeamTime` gives the time of day the scene is lit for (the clock, or the `DECK_FORCE_TIME` hour; invalid indoors or with the cycle off: then the fixed daytime sun is used).
- Projection (`BeamScreenDir`): the target sits on the view axis at depth len; the picture of target + s L (s = 5 percent of len) through the pinhole of `sCamera` (vertical fov, aspect cancels in square screen units) gives the direction at the screen centre. `voxel_pitch`, yaw and distance are in it. Light from below or behind is clamped so the beams stay at least 20 degrees off horizontal (`sy >= 0.35`), coming from the top.
- Crossfade: the sun and moon vectors are blended by the moon share (`moonAmount / (sunAmount + moonAmount)`), the same share that crossfades the colour, so the beams turn smoothly (through vertical) while sun fades to moon; no jump at dawn, dusk or night.
- Intensity: `kShaftGain` was 0.35 / 0.65 / 0.90 (d953c91..e17ba5d), then x0.72 = 0.25 / 0.47 / 0.65, then (Marcos, still too bright) x0.5 = **0.125 / 0.235 / 0.325** for LOW / MEDIUM / HIGH, sun and moon alike (the moon is also x0.5 of the sun amount, `wf` unchanged). Debug aid: env `DECK_SHAFT_GAIN=<multiplier>` scales the gain (the angle shots use 5).
- Evidence (noon-like still.txt, all HIGH, `DECK_FORCE_TIME`, gain x5 only to make the angle readable, shots/beams/g5_*.png): dawn 05:30: narrow bright beams from the top right running down to the left, about 60 degrees off vertical; noon: vertical; dusk 17:30: broad golden beams from the top left running down to the right (the other lean), long; night 23:00 (moon at p 0.42, dx -0.35): pale blue beams, nearly vertical with a slight lean. A normal-gain dusk shot at the final constants shows soft warm diagonal bands that do not wash out the scene. `DECK_FORCE_TIME` has only the four times; the intermediate-angle shot with `clock_speed` was not taken.

## Phase 13c: smooth particles
Cause, from `deck_particles.c` and voxel_renderer.c: the motion was already continuous (`f->seconds` is the performance counter in seconds, `uTime` in the vertex shader, no frame counts, ticks or rounding). The digital look came from: one straight constant velocity drift plus a single sine per axis (fireflies traced a perfect circle), no lateral sway on dust, a fast twinkle (1.5 to 3.5 rad/s) on dust, and sprites 2 to 4 px wide with a hard-edged profile in the 960x544 NEAREST scene, so a 3 px/s drift stepped from pixel to pixel. Fix (`kPartVs`): dust gets two lazy sways per axis (0.27 to 0.83 rad/s), a two-sine bob and a gust that leans the higher motes; fireflies get three incommensurate sines per axis (0.19 to 1.07 rad/s, never a closed loop), a slow bob and a slower pulse (smoothstep 0.15 to 0.85); a quad narrower than 2.6 px (`uViewH`, projection scale and clip w) is widened to that and its alpha scaled by the area ratio, and the dust profile is a gaussian (`exp(-3 r2)`), so sub-pixel drift moves brightness over neighbouring pixels. Counts and levels unchanged. Only shot-level evidence (no 0.5 s frame pair compared for particles).

## Phase 3b: ambient volume and frequency rows (2026-11-14)
`ambient_volume` (OFF, 1..10, default 6) is replaced by `ambient_level` (row AMBIENT VOLUME): 0 OFF, 1 LOW, 2 MEDIUM (default), 3 HIGH. Gain (`kLevelGain`, deck_ambient_game.c) 0 / 0.15 / 0.30 / 0.48 times the master volume; the old default was setting 6 = 0.6, so MEDIUM is half the amplitude (-6 dB), LOW is x0.25 and HIGH x0.8 of the old default. Old files: `MigrateLevelSettings` (vita_extras.c) maps `ambient_volume` once, marker key `amb_levels=1` (0 to OFF, 1..3 LOW, 4..7 MEDIUM, 8..10 HIGH; the old default 6 lands on MEDIUM). New row AMBIENT FREQUENCY, key `ambient_density`: 0 SPARSE (default), 1 NORMAL, 2 FREQUENT = x0.6 / 1.0 / 1.5 on the one-shot rates (`DeckAmbient_SetDensity`: bird call wait and cave drip wait are divided by it; the beds (wind, waves, rain, cave tone, crickets' trains, thunder from the game) are untouched). `tools/deck_tests/test_ambient.c` passes (built with `gcc -m32` in the container, RESULT: PASS; it exercises `DeckAmbient_Mix` at gain 1, so it does not see the level table). The original game SFX and music volume settings were not found changed or touched: the master volume still multiplies the ambient gain.

## Phase 2b: sway of small plants
`deck_sway.c` (kVs): besides the tree sway, vertices of small plants (weight w up to 0.8 for flowers and 1.0 for tall grass; tree crowns reach 0.5) get a second sway across the wind and a slight droop, both scaled by w squared (base fixed), phase from the world position. Root cause of the reported stepping was NOT proven: the standing flower cards and grass tufts already used the continuous shader (`uTime`, world phase). The flat flowers drawn into the ground are the original game's tile animation (frames swapped through `VoxelRenderer_NotifyTilesetAnimWrite`, 4 bit indexed tiles), which steps by design and cannot be cross faded cheaply; this is the most likely thing seen stepping and is left as is. Pixel comparison of four shots 0.5 s apart (shots/flow1): the flower area changed 29 percent of pixels between shots, against about 4 percent on a static roof, so the cards do move.

## Phase 15: battle effects (voxel_battlefx), 2026-11-14
Row BATTLE EFFECTS (WORLD tab), key `voxel_battlefx`, levels OFF / AUTO / LOW / MEDIUM / HIGH; OFF = the previous battle look including the backdrop blur and dim (`voxel_battledim`). Env `DECK_BATTLEFX_OFF=1` for A/B. Code: `deck_battlefx.c/.h`, the `uBF0..3` block in the composite of `deck_post.c` (battle path), the `DeckBattleFx_Begin` / `DeckBattleFx_DrawBackdrop` calls in `Pass_DeckPost` (voxel_renderer.c), the hit hook in `Cmd_hitanimation` (battle_script_commands.c). `DeckPost_WantScene(..., battle)` is true when the dim or the effects are on. Logs: `[BATTLEFX] ready`, `[BATTLEFX] hit: type .. target .. crit .. big .. at uv ..`; a failed sprite program build logs `[BATTLEFX] sprite program build failed` and only the composite effects stay.
- Why it never touches the UI: in a 3D battle the stage goes through the post composite, then `DeckBattleFx_DrawBackdrop` draws (additive) into the window, and only then `Pass_Overlay2D` draws the battlers, HP boxes and text box. Everything here is behind them.
- Hit hook: `Cmd_hitanimation`, game thread, at the point the hit blink animation is queued (not for misses, no-effect results, or a substitute taking the hit). It knows `gCurrentMove` (type `gBattleMoves[].type`, or `gBattleStruct->dynamicMoveType & 0x3F` for Hidden Power and Weather Ball like moves), the struck battler `gActiveBattler`, `gCritMultiplier > 1` (crit), `gBattleMoveDamage` and the target's max HP (big = damage >= 25 percent of the max). `DeckBattleFx_Hit` writes a small payload and bumps an atomic sequence (release); the GL thread reads it (acquire) in `DeckBattleFx_Begin`, so there is no lock. The hit time is the GL frame that sees the new sequence (within one frame of the blink).
- Effects (strength constants LOW / MEDIUM / HIGH in `deck_battlefx.c`): (1) type hit: tint flash in the composite plus a big additive glow behind the target in the move type's colour (normal white, fire orange, water blue, electric yellow, grass green, ice cyan, psychic pink, ghost purple, dark purple, dragon indigo, others listed in `TypeTint`), x0.5 / 0.8 / 1.0; screen shake on big hits (backdrop only) x0.5 / 0.8 / 1.0; (2) living backdrop: zoom 1.04 plus 0.012 sine, pan 0.006 / 0.004 uv, x0.6 / 0.8 / 1.0, and 48 gold motes x0.4 / 0.7 / 1.0 (blue-white in rain or hail); (3) spotlight: warm soft light on each platform (the places of `VoxelBattle_Shadows`) alpha 0.20 x0.6 / 0.8 / 1.0; rim light on the sprite edges NOT done (the sprites are part of the GBA overlay picture, not in the post pass); (4) battle weather from `gBattleWeather`: rain streaks (110), sandstorm haze plus 46 streaks, hail (70 dots), sun glare (top right), x0.5 / 0.8 / 1.0; the overworld weather code was not reused (it is a 3D world pass); (5) entry: radial zoom out from 1.24 and a soft light burst over 0.9 s, quick white flash 0.32 on a crit; (6) low HP: battler 0 under 20 percent HP: red edge pulse (2.6 rad/s) and 40 percent desaturation, x0.6 / 0.8 / 1.0, eased in; (7) soft ground shadow: already existed (`VoxelBattle_Shadows` ovals in `Pass_Overlay2D`), unchanged; the wet-floor reflection is NOT done; (8) chromatic aberration on hit frames: R and B taps shifted 0.0045 uv x0 (off on LOW) / 0.6 / 1.0, along the line from the hit.
- Tested once (grass_battle_fx.txt, all HIGH, `voxel_battlefx=4`): see HANDOFF; the hit events and the target position logged and the picture read. Not seen: the hit flash itself (0.3 s window, shots every 5 s), low HP, battle weather, other move types.

## Phase 12b: finer ground relief, indoor floors (voxel_groundrelief), 2026-11-14
Marcos: the floor bumps read as big waves. `dgHeight` (deck_bump.c) constants, noise frequency per tile, old to new: grass 1.5 and 3.6 to 5 and 12 (and the grass height x0.65); sand ripples 5.5 (warp noise 0.9) to 15 (warp 3); path grain 9 and 17 to 18 and 29; finite difference step 0.07 to 0.02; gradient gain 0.30 to 0.09 and height term 0.35 to 0.25 (the finer noise has steeper gradients). The slow undulation keeps its scales (0.16 and 0.07 per tile) at 25 percent of its old amplitude (weights 2.2 and 1.2 times 0.25). All of it is in world space (the world xz of the vertex), so nothing shimmers when walking; the finest octave of each kind fades out when it nears the pixel size (`fwidth` of the world position) so it cannot alias. Amplitudes (0.20 / 0.35 / 0.50) unchanged outdoors.
Indoor floors (`f->indoor`): they have no colour class of their own and fell into the sand or path branch (the lab floor read as sand and showed diagonal ripple stripes, see shots/gr_after_indoor), and `deckBumpMul` ran its Sobel over the tile pattern at 0.18 / 0.30 / 0.45. `DeckBump_SetIndoor` and `DeckBump_ApplyIndoor` (called after `DeckBump_Set` in `DeckRain_WetBegin` and `DeckRain_BumpBegin`) cut the albedo relief to x0.12 (0.022 / 0.036 / 0.054), the procedural ground to x0.2 (0.04 / 0.07 / 0.10), force the path grain class (no grass, no sand ripples) and switch the undulation off (`uGroundP.y = 1`). Evidence (still.txt noon and dusk, 3x crops before and after, shots/gr_noon2_before_after_3x.png and gr_dusk_before_after_3x.png; the lab floor before and after, gr_after_indoor and gr_after2_indoor): the large dark soft blotches are gone, the ground is a fine speckle; at dusk some dark speckle remains; the lab floor shows only the tile grid and a fine grain. Honest limit: the "before" crops come from earlier runs with a slightly different scene time (NPC positions), so only the ground texture, not the whole image, is comparable.

## Phase 8b: banding on far houses, 2026-11-14
Symptom: about seven thin dark horizontal lines (pitch about 8 px) across the walls of houses in the tilt-shift blurred distance, seen while walking. Diagnosis from single-switch runs at default settings (still.txt, noon, far house at 3x, shots/band_switch_cmp.png): `voxel_sunshadow=0` removed the lines; `voxel_bump=0`, grade/LUT off and the box-filtered half-res copy did not; `voxel_res=4` (1280x726, 1:1 with the window) hid them; tilt-shift off showed the sharp wall with the same faint lines. A rotated tilt-shift tap pattern (committed, kept: it trades structured error for fine grain at HIGH) did not remove them either. Cause: the composite's sun shadow receiver took the surface normal from `cross(dFdx(wp), dFdy(wp))` of the depth-reconstructed world position, one pixel baseline; at grazing angles on far walls the depth quantisation made it flip from row to row, so rows passed the `ndl > 0.05` test and self shadowed. Fix (deck_post.c, POST_SUN): the normal is `cross(P(x+3px) - P(x-3px), P(y+3px) - P(y-3px))` from four depth taps (6 px baseline). Result: the lines are gone at default (shots/gr_fin2_def/m20000.png; was shots/gr_head_field_def). Also tried and reverted in the same round: skipping near vertical surfaces (`nn.y > 0.45`) did not remove them. Side commits of this round: half-res copy is a 2x2 box filter (`kCopyFs`, correct on its own: the point sampled copy aliased), tilt-shift gather rotated per pixel by interleaved gradient noise (`ign`). Floor check requested by Marcos: indoor lab floor at 8aa6f41 vs the head, all HIGH and default (shots/floor_cmp_3x.png): identical grain at all HIGH, default is calmer; the indoor procedural ground was then lowered from x0.2 to x0.1.
## UI polish (deck-ui branch)
Eight settings in GENERAL > INTERFACE (all `PLATFORM_DECK`, keys by name in settings.txt; levels 0 OFF, 1 AUTO, 2 LOW, 3 MEDIUM, 4 HIGH; OFF draws the UI exactly as before). Rule from Marcos: the original art style stays (panel art, borders, colours, original glyph pixels); effects go behind and around it. Env switches (read once): `DECK_UIFX_OFF=1` all eight off, `DECK_UIGLASS_OFF=1` only the glass and tint GL pass. Logs: `[UIFX]` in gl_errors.log. Code: `src/platform/deck/deck_uifx.c/.h` (GL pass and helpers), `src/platform/ui/ui_fx.c` (Core 1 helpers), hooks in `ui_widgets.c`, `ui_draw.c`, `ui_render.c`, `ui_core.c`, `ui_assets.c`, `ui_sprites.c`, `ui_battle_hud.c`, `sdl2.c` (ShowNativeUi), `vita_ui_options.c`.

| Row | Key | What it does | Constants (low / medium / high) |
|---|---|---|---|
| PANEL GLASS | voxel_uiglass | new UI panels (UiW_Panel, DialogBox, PanelDark, Window, the full frame) register their rect in the draw list; Core 0 copies the picture, blurs it at half resolution (gaussian, `kGlassPasses` 1/1/2, `kGlassStep` 0.8/1.2/1.6) and draws the frosted layer per panel at its z position (`firstVert`), cut to the panel shape (rounded or chamfered SDF). The fill over it becomes slightly translucent: `kGlassFill` alpha 244/232/214 of 255. The edge is drawn as a ring and the hard shadow as strips outside the box while translucent, so the art is the same pixels. The glass is pulled toward the panel colour (`kGlassBaseMix` 0.70/0.60/0.50); edge highlight `kGlassEdge` 0/0.15/0.30. The full frame keeps its own alpha, only what is behind it is blurred. | see left |
| PANEL TINT | voxel_uitint | gradient between two theme colours (tabTop, capTop) shifted by the time of day (warm at dusk, deep blue at night), drifting continuously (`sin` of seconds, never stepped), breathing from MEDIUM. Opacity `kTintAmt` 0.04/0.06/0.09, drift `kTintSpeed` 0.25/0.8/1.2. Sheen band code exists (`kTintSheen`) but is 0 (Marcos: no strong sheen). Works under the glass or alone (fill alpha 244/236/226). | |
| TEXT CLARITY | voxel_uitext | only dark low-opacity copies of the same glyphs under light text: LOW a 1 px shadow (a 0.31), MEDIUM 4 faint ring copies (a 0.18) + shadow (a 0.45), HIGH 8 ring copies. Dark text is untouched. No size boost exists. The group label of the port options menu goes from (88,108,102) to (50,68,64) while this is on. `UI_MAX_QUADS` is 16384 on the Deck (each glyph up to 10 quads). | |
| MENU CURSOR | voxel_uisel | selected row: soft white pulse (`2 + 12 sin` alpha x 0.5/0.8/1.1) and a sliding trail from the previous row (`kSlide` 0.22/0.30/0.38 of the distance a tick, theme colour, alpha 70). The row itself stays where the original draws it. | |
| MENU ANIMATION | voxel_uianim | a screen that opens or changes fades in and slides 6/10/14 px over 6/9/12 ticks (vertex alpha scaled in `UiDraw_Publish`, list slide applied by Core 0, the glass follows `fade`). A closed menu is kept (`UiDraw_Snapshot`) and fades out over the next ticks; the game state is closed at once, input is never delayed. | |
| HP BARS | voxel_uibars | bar fill follows the value (`kBarRate` 0.16/0.10/0.07 of the gap a tick, min 0.15 percent), keyed by bar position; HP/EXP bars of UiW_Bar and the battle HUD. Colours are the original ones (no gradient); a faint pulsing red glow behind an HP bar at 25 percent or less. GBA overlay bars (the vanilla battle) are not touched: the game animates them already. | |
| BUTTON ICONS | voxel_uiglyphs | 0 OFF = the port's PlayStation circles, 1 GBA = A and B letters for cross and circle (square and triangle keep their shapes), 2 DECK = A / B / X / Y in the Deck colours (cross A, circle B, square X, triangle Y, the mapping in `deck_input.c`). `UiW_Glyph` and so every footer prompt. The options screen hint text follows. GBA text codes inside game strings are not swapped (the GBA glyph is a font tile; the Deck A and B are at the same place as the GBA ones). | |
| WIDE UI LAYOUT | voxel_uilayout | (safe subset) item icons get a slight soft shadow (`UiSpr_ItemIcon`, black copy at +1,+2 scale, alpha 0.28); the 4:3 port menus (VitaUI canvas) get a soft shadow cast on the side margins and a very faint breathing gradient in them (`DeckUiFx_Margins`, called from `ShowNativeUi`). NOT done: side preview panel on the item and party screens (the new UI screens are 16:9 already; the VitaUI canvas is 320x240 software drawn, a side panel needs per screen layout work), party icon bob (the party screens already bob the icons by frame). | |

Paths: new UI (`ui_v2`) gets all eight. The VitaUI canvas (default menus, opaque, nothing behind it) gets button prompts only where `UiW_Glyph` is used (none; its prompts are baked art), the margins, and the darker label; no glass or tint there (no panel list, nothing to blur). The GBA overlay (dialogue, battle, map name popup, title) is unchanged except the existing `voxel_uiglow`; the dialogue/battle boxes are opaque, so glass would not show, and the glyphs must stay the original pixels.

Sign and footer investigation (Marcos, 2026-11-14): the map name popup (GBA, wooden) measured x 101-526, y 42-190 in OFF, default and HIGH, the same as `docs/screens/final_A_noon_allhigh_t2_beams_moved_uiglow_sign.png` (98-530); 2x crops show the same crisp glyphs in all three. Nothing in the UI polish scales it (it is the overlay quad, never touched). The settings menu has no footer text line; the low-contrast text was the group label, darkened (above).
## Intro and menu effects (voxel_flatfx, deck-title branch)
Row INTRO & MENU EFFECTS (OFF / AUTO / LOW / MEDIUM / HIGH), env `DECK_FLATFX_OFF=1`. Before: every non-3D frame was drawn by `VDraw` + `RenderGameTexture` (sdl2.c) straight into framebuffer 0; the post chain (`DeckPost_WantScene` / `DeckPost_Run`) runs only inside `VoxelRenderer_RenderFrame` (`f.postActive`), so the intro, title and menus never saw it. Now `DeckFlat_Begin` (after the clear in `RenderGameTexture`) binds a capture target the size of the letterboxed picture in window pixels; the game quad and the wide margin art (`VitaStage_*`, margin fades) draw into it unchanged; `DeckFlat_End` composites 1:1 with NEAREST (pixel art stays sharp) and `Ui_Render` still draws on top, unprocessed. `deck_flatfx.c`: bright pass, half-res bloom (2 blur passes), quarter-res wide glow of bright and saturated texels (stands in for a sprite glow, there is no sprite mask), split-tone grade, saturation, S-curve, vignette, film grain, a slow light pulse. Bloom and glow only add what lies beyond the base (`max(bloom - base, 0)`), after the first run washed the white menu boxes out (docs/screens/flatfx_mainmenu_first_bloom_washout.png). Level tables at the top of the file: bloom 0.18/0.28/0.40, threshold 0.80/0.76/0.72, glow 0/0.12/0.20, vignette 0.14/0.22/0.32, grain 0/0.010/0.018, pulse 0/0.010/0.018. Not applied: the new UI (Ui_Render), the native menu path (`ShowNativeUi`), 3D frames, battles. No CRT scanlines (they would break the pixel grid). Fallback: shader or target failure logs `[FLATFX]` and draws straight as before.
Intro slivers (repeated tile slivers at the left and right edges of the leaves scene, noted above around line 116): still present in this build (docs/screens/flatfx_intro_leaves_no_margins_slivers.png) because `assets/ui/intro_margins.bin` was not in `dist/app0/ui` (`tools/ui_gen/gen_intro_margins.py` was never run: fetch_assets step 3 needed Pillow). Generated in the container with python3 and copied to the dist, the slivers become continuation art (flatfx_intro_leaves_with_margins_off.png); left over: a thin dark vertical band at the lower left (window x 95-130) and a few leaf shapes cut at the right edge. The Deck build should run the generator once (build_vita.sh does for the Vita).

## Title screen (voxel_title, deck-title branch)
Row TITLE SCREEN (ORIGINAL / BRAVELY, default BRAVELY), env `DECK_TITLE_ORIGINAL=1`. The original composition stays (docs/screens/title_original.png vs title_bravely_2dhd_phase3.png).
- Hook points: `title_screen.c` exports `gDeckTitlePhase` (0 off, 1 logo shine from the start of the fade-in, 2 version banner, 3 PRESS START, 4 leaving); the state machine, input, music and exits are untouched. `VDraw` (sdl2.c) asks `DeckTitle_Wanted()`; when true it hides BG0 and BG1 (`gFastDrawDispcntClear`), keys the backdrop to magenta 0x7C1F (`gFastDrawBackdropOverride`) and clears the backdrop blend bits, so the frame texture holds only BG2 (POKEMON logo, shine window effect) and the sprites (EMERALD VERSION, PRESS START, copyright) exactly as the game draws them, with its fades and blinking. `RenderGameTexture` then calls `DeckTitle_Draw` instead of drawing the quad.
- `deck_title.c` reads the layers the game loaded into emulated VRAM plus the unfaded palette when the title starts (BG0 charbase 2 / screenbase 26: sky bands are bank 14 indexes 3..10, silhouette 11, eyes 2, animated markings 15; BG1 clouds charbase 3 / screenbase 27). Back to front: smoothed sky gradient, far clouds (mip bias blur, slower), Rayquaza as a sprite (sharp-bilinear sampling: crisp texels with sub-pixel drift, warm rim light, markings take the live palette colour so the pulse stays), near clouds, light rays from the upper right, motes and bokeh discs (sines of the clock), the game's palette fade, the BRAVELY and HD-2D lettering, then the front layer: soft glow behind, rim highlight on upper left edges, a shine sweep every 7 s. Rayquaza and the cloud overlay appear with PRESS START like the original.
- Lettering: the game's own font (`gFontNormalLatinGlyphs`, raw 2bpp rows: high byte = left four pixels, leftmost pixel in the top bits; 1 face, 2 shadow, 0 and 3 clear), emboldened, scaled with nearest sampling, outlined: BRAVELY in the EMERALD VERSION look (white, grey underside, navy outline) at x4 left of EMERALD, HD-2D in the POKEMON logo look (yellow, blue outline) at x3 right of VERSION. The logo art has no B or Y, so letters could not be reused for the whole word; one font gives one consistent look.
- Test: `tools/deck/autoplay/title.txt` (one START at 9 s skips the intro), shots 13 s (phase 1, shine), 19 s, 34 s (phase 3); ORIGINAL run with `SETKEY='voxel_title=0 voxel_flatfx=0'`. Fallback: build failure or art not found in VRAM logs `[TITLE]` and draws the original. Run scripts relaunch a game from the worktree dist afterwards: kill it by /proc cwd so only the main clone game stays.

## Title and intro reveal (HD-2D, deck-title branch, supersedes the lettering notes above)
- Keys: `voxel_title` TITLE SCREEN (ORIGINAL / HD-2D), `voxel_flatfx` INTRO EFFECTS, `voxel_reveal` REVEAL FLASH (OFF / ON, default ON), env `DECK_REVEAL_OFF=1` (HD-2D look from the first frame, no flash). Reveal timeline in `deck_reveal.c`: the title and each intro scene start as the plain flat picture; a white flash (rise 0.14 s, fall 0.95 s, wall clock) fires, the HD-2D look takes over under it (flat pass strength and title elements ease in over 1.4 s). Title: flash 3.0 s after the title starts (or at once when START skips to PRESS START); BRAVELY drops in and EMERALD VERSION is pushed down by 76 px when PRESS START comes (after the logo's own slide). Intro: `gDeckIntroSegment` (intro.c) = 1 Game Freak logo and leaves, 2 bike, 3 Poke Ball, 4 Groudon, 5 Kyogre, 6 clouds, 7 lightning, 8 Rayquaza, 255 the copyright screen (stays flat); flash times after the scene start in `kSegFlash` (9.1, 8.7, none, 1.15, 2.0, 1.5, none, 1.7 s, from measured scene lengths 18.3 / 17.4 / 0.7 / 2.3 / 4.1 / 3.0 / 2.0 / 3.5 s; none in the sub-second cuts so it does not strobe). The scene resets to flat at the next scene start.
- BRAVELY: Cinzel (SIL OFL 1.1, licence in docs/licenses/Cinzel-OFL.txt, downloaded once from https://github.com/google/fonts/tree/main/ofl/cinzel, 122 KB) rendered offline by `tools/deck/gen_title_font.py` into `deck_title_font.h` (run-length alpha masks). Dark ink with a very soft light edge, no plate. The font file itself is not in the repo.
- EMERALD VERSION is drawn by `DrawBanner` as one piece from the game's own sprite sheet (`gTitleScreenEmeraldVersionGfx` / `Pal`, 8bpp 1D tiles) at the live y of the game's banner sprite (x 98 / 162); those sprites are hidden in the GBA frame while the HD-2D title is up. An earlier colour-based split of the banner out of the frame also caught the logo's black outline and was dropped. The front layer is untouched game output (an earlier rim highlight turned the logo's black outline grey and was removed): POKEMON edges match the original texel for texel (docs/screens/title_logo_edge_crop_4x.png).
- HD-2D crest: the official logo could not be found (three tries: Wikipedia / Commons searches), nothing was invented; the badge, its sparkle and its second flash are dropped. If a transparent PNG is ever supplied the old hook was `ui/hd2d_badge.png` (removed with the badge).
- Not verified: the intro reveal frames in the scenes after 2 (only the scene 1 timing and no shots at the flash peaks of scenes 2 to 8), the reveal with START pressed mid-way, `voxel_reveal` OFF and `DECK_REVEAL_OFF` runs, performance, fast-forward (the reveal clock is wall time).

## Ambient occlusion (voxel_ao, deck-ao branch)
Row AMBIENT OCCLUSION in WORLD > EFFECTS, key `voxel_ao` (OFF / AUTO / LOW / MEDIUM / HIGH, default AUTO; AUTO follows `voxel_quality`; the enum id is the last of the PLATFORM_DECK block). Env `DECK_AO_DEBUG=1` shows the raw occlusion as grey (screenshots).
- What: GTAO (ground truth ambient occlusion) from the scene depth texture, a GLSL 1.20 port of the three.js GTAOShader (MIT, see NOTICE.md; Jimenez et al. 2016). All in `deck_post.c`: gather at half resolution into `sAo[0]` (`kAoFs`), depth-aware separable 5 tap blur (`kAoBlurFs`, horizontal into `sAo[1]`, vertical back), then the composite upsamples bilinearly with weights by relative depth likeness (`uAoTex` unit 7, `uAO`) and multiplies the colour `c` after the tilt-shift, sun and band blur and before the bloom and grade. Runs only when the scene has a depth texture (`DECK_GL_DEPTH_TEXTURE24`) and not in a 3D battle (forced off like the grade). Indoors it is on. A failed program or target build only switches it off (`[POST] ao programs failed` / `ao targets failed`; `[POST] ready` logs `ao=1`).
- View space without matrices: pos = (ndc x aspect x tanHalfFov x z, ndc y x tanHalfFov x z, -z), z the linear view depth from near/far (`DeckPostFrame.tanHalfFov`, `aspect`, set in `voxel_renderer.c` beside zNear/zFar). Normal from the depth (three.js NORMAL_VECTOR_TYPE 0: the smaller side per axis, extrapolated from 2 taps), baseline one half-resolution pixel (two scene pixels). The depth is read at the lower left scene texel of each 2x2 block (`pc = vPos - 0.25 uTexel`), the same in the gather, blur and upsample, so offsets land on texel centres (NEAREST depth).
- Levels: LOW radius 0.88, 2 slices x 4 steps (16 taps); MEDIUM 0.88, 3 x 5 (30); HIGH 1.05, 3 x 6 (36). Common: thickness 1.6 with a soft weight (full up to 0.55 x thickness, zero at thickness; an occluder NEARER to the eye than the pixel counts 2.5x its depth difference, so floating crowns and cards do not darken the ground behind them), distance falloff on, occlusion exponent 1.8 (`pow(ao, 1.8)` in the gather), intensity 0.85 (`mix(1, ao, 0.85)` times the screen fade), fade to nothing from 30 to 55 world units (zFar 62), sky = no occlusion. Per pixel rotation by interleaved gradient noise (no noise texture); the blur removes the grain.
- Tuning findings (Littleroot, 1 tile about 1 world unit, about 57 px per unit at 960 wide): the first values (radius 0.55, thickness 0.6, exponent 1) gave only 5 px hairlines because a hard 0.6 thickness rejected most wall samples and the raw term was too weak; a 0.9 radius with thickness 1.5+ gives a gradient about 25 px wide at wall bases, the exponent brings the deep corners to about 35 to 45 percent.
- Debug env (read once in `DeckPost_Init`): `DECK_AO_DEBUG=1` pure raw AO as grey (bypasses grade, vignette, bloom, LUT), `DECK_AO_RADIUS=<multiplier>`, `DECK_AO_THICK=<world>`, `DECK_AO_INTENSITY=<0..1>`, `DECK_AO_POW=<exponent>`, `DECK_AO_DIRS=1..3`, `DECK_AO_STEPS=1..6`, `DECK_AO_BLUR=0` (skip the blur).
- Cost measured (AMD, Windows, same Littleroot script, 3D frames after 10 s, `post_gpu_ms` mean): OFF 1.49 ms, AUTO 2.58 (+1.09), HIGH 2.63 (+1.14).
- Known limits: needs the depth texture (the renderbuffer fallback has none); only what is in the depth buffer occludes (alpha cutouts that do not write depth do not, billboards that do write a flat card); half resolution can leak a pixel wide halo at thin silhouettes (the upsample weights limit it); the occlusion is not in the tilt-shift source copy, so it is applied after the blur (smooth, no visible issue expected); a thin cost shows in `post_gpu_ms`. Tuned on a Windows build; not yet checked on the Deck.

## Lit windows at night (voxel_windows)
Row NIGHT WINDOWS (OFF / AUTO / LOW / MEDIUM / HIGH), key `voxel_windows`. Needs `voxel_shaders` on. Files: `src/platform/deck/deck_windows.c/.h` (picked up by the CMake GLOB), `Pass_NightWindows` in `voxel_renderer.c` (Deck only).
- Window texels: `buildings.bin` already carries a glow mask (VXB9, bit 15 of a page's height field, one byte per texel after the RGBA5551 texels, same Morton 8x8 order, `VoxelBuildings_ReadGlow`; it was unused). 29 of 270 pages have it; the marked texels are the teal/blue glass (checked on the file: all opaque, about (3,5,7) / (5,6,7) of 8 per channel). Nothing is detected by colour at run time. Pages without a mask get no lit windows.
- Mask texture: per page, GL_LUMINANCE 8 bit, same size as the colour page (rows bottom up), 6 LRU slots, at most 2 built per frame (a page not ready draws dark for a frame). Built only while the effect is active.
- Draw: after `Pass_DayNightGrade` (the grade is a multiply and would darken it), before `Pass_NightLamps`. The visible chunks' building spans are drawn again from the chunk VBOs (mask texture, `ftransform` so the depth equals pass 3, `GL_LEQUAL`, no depth write, blend ONE / ONE_MINUS_SRC_ALPHA: the glass is replaced, not added to). Entities in front of a window hide it. The fragment shader discards every texel off the mask.
- Strength: `daylight` of `VitaDayNight_Get()`: 0 at >= 0.45, smoothstep to full at <= 0.20, times gain LOW / MEDIUM / HIGH 0.80 / 1.00 / 1.20. Day keys: 17:30 = 0.35 (about 35 percent lit), 19:00 = 0.175 (full), night 0. Off indoors, in 3D battles and when `VitaGame_IsOutdoor()` is false.
- Colour: warm (1.0, 0.78, 0.42) x 1.15 plus a hot core `(0.26, 0.22, 0.12) * k^2`; luminance about 0.9 at full strength, above the bloom bright pass threshold of every preset (0.70 / 0.65 / 0.58, `kPresets` in deck_post.c), so bloom picks it up (bloom must not be OFF).
- Variety: per house = a cell of 5 map tiles of the vertex position (map-local, stable across map crossings): 15 percent never lit, flicker (4.5 percent slow sine + 2 percent fast, own phase and rate per house, from `uTime`, nothing random). Per window tile brightness 0.84..1.0.
- Limits: house identity is a 5 tile cell, not the building (vertices carry no building id), so a wide building crossing a cell edge can be half lit. Doors and street lamp glass are not done here (lamps keep Pass 5.2). Depth equality with pass 3 relies on both using `ftransform` (bump program included). If a shader fails, `[WINDOWS]` goes to gl_errors.log and the windows stay as drawn.
- Test: `DECK_FORCE_TIME=night` (23:00, full) or `dusk` (17:30, about 35 percent); `noon` for the day shot.

### Window light rays
Lit windows also throw light onto the ground in front of the house. Done in the composite of `deck_post.c` (POST_X, needs the extended composite), in screen space, after the ambient occlusion and before the bloom is added. The taps read `uScene`, no extra texture unit.
- Direction: the rays spill DOWN the screen from a lit window (the facades face the camera) and lean sideways with the sun or moon screen direction. The skew `uWinSkew` comes from `beamX` (the `BeamScreenDir` value already passed to the shaft pass), so the angle changes through the day and night.
- Hit test: each pixel walks up the screen from itself, 8 taps per column, 0.012 of the screen height per tap, the outer columns fanning out with distance. A tap counts as a window when it is warm and bright (luminance over 0.75, red over 0.8, red above green above blue), weighted `(1 - j/9)^2`. The test needs three horizontal taps (+-11 px) to agree, so a small bright dot such as a firefly does not streak into a ray. Gain about 2.6, warm tint (1.0, 0.72, 0.38).
- Amount: `DeckWindows_Amount()` (same dusk and night curve, 0 by day, indoors and in 3D battles), passed as `DeckPostFrame.winRaysAmount` and set as `uWinRays` (0 skips the loop).
- Debug: `DECK_WINRAYS_STRENGTH=<float>` multiplies the amount, `DECK_WINRAYS_DEBUG=1` forces amount 1 (no night needed, but there are still no lit windows by day).

## Sprite lighting (voxel_sprlight, SPRITE LIGHTING)
Sun lit character billboards (player, NPCs, followers, any card inside `VoxelEntities_CharacterRange`). Levels OFF / AUTO / LOW / MEDIUM / HIGH via `DeckLevelPick(FAST_EXTRA_VOXEL_SPRLIGHT, 0.40, 0.70, 1.00)`; `voxel_shaders` stays the master switch. No new program: it extends the G4 outline program (`kOutFs`, deck_chars.c), which already draws the character range in `Pass_Entities`.
- Pseudo normal from the card's own shape: 6 taps (2, 4..12 texels) along the sun axis (x = sun side, y at half rate) on each side of the texel, clamped to the texel's 64 x 64 atlas slot. `nrm = (solid away - solid toward) / 6`: +1 at the sun side edge, -1 at the far edge, 0 in the middle. Plus a luminance step hint (0.9 weight) when both neighbours are solid. `shade = clamp(0.8 nrm + 0.9 hint, -1, 1)`.
- Luminance x (1 + A shade), A = 0.20 x level x env x light, so within +-20 percent. A small warm (sun) or cool tint follows the shade (about 5 percent). The ground relief (`deckBumpMul`) still multiplies first.
- One rim, no stacking: the old outline rim is the same code path. Strength 0.9 x level x env x sun strength (outline level alone when sprite lighting is OFF). Dusk (sun strength below 0.6) adds a second texel at 0.6 weight and a redder colour (0.42, 0.20, 0.06; day 0.34, 0.24, 0.10). Night (sun strength < 0.25 blends in): moon blue rim (0.06, 0.10, 0.22), strength at most 0.4, direction from `VitaDayNight_MoonPathAt(BeamTime)` when the sun is below 0.02.
- env: indoors 0.10, rain 1 - 0.85 x rain amount (`DeckChars_SetEnv(indoor, rain)`, called once a frame next to `DeckRain_Update`). Uniform `uLight` = (shade amp, rim strength, dusk, sun 1 / moon 0). The program also runs when only sprite lighting is on (outline OFF, bump OFF): outline alpha 0, clear texels discarded.
- Debug: `DECK_SPRLIGHT_DEBUG=1` forces shade 0.45 and rim 0.9 regardless of sun, level and weather (logs `[SPRLIGHT]` once).
- Limits: shading is shape based (12 texels per side), not aware of sprite orientation; a sprite filling its 64 slot to the border shades from its own edge texel. The vertical sign of the sun axis is the unchanged outline convention and is unverified in game. Cards outside the character range get nothing.

### Character glow at night
Characters glow themselves at dusk and night, the same code path as the sprite lighting (player and NPCs alike).
- Emissive lift: `uEmis` in the outline shader (`kOutFs`, deck_chars.c) adds a warm lift (about (1.0, 0.78, 0.45)) to the sprite's own texels, so the character itself lights up instead of sitting in front of a halo. Strength follows the night amount.
- Halo billboard: `DeckChars_DrawGlow` still draws one quad per character from the same vertex range (feet line, right axis, height h from the six vertices; centred 0.4 h above the feet, 1.5 h to every side, faded over the first stretch above the feet so the ground does not cut it). It is now only a faint ground light: peak alpha 0.7 x amount (the earlier version was 2.6 x). Premultiplied (ONE, ONE_MINUS_SRC_ALPHA), depth test on, depth write off, drawn before the character cards so each sprite sits on top of its halo. Battle sprites and indoor maps get none.
- Amount: full at night, 0 once the sun strength reaches 0.5, linear in between (from the sun strength given to `DeckChars_OutlineBegin`). Scaled by the sprite lighting level (OFF = no glow), halved at full rain, zero indoors.
- Env: `DECK_SPRGLOW_STRENGTH` float multiplier (default 1), `DECK_SPRGLOW_DEBUG=1` forces night (still honours level, rain, indoors).
- Limits: the halo is a flat quad, so a wall or tree between camera and character hides it by depth; a card without two vertices on its lowest edge is skipped. The night grade runs after and darkens the halo like the rest of the world; judge strength in game.

## Tree crown shading (voxel_crowns, TREE SHADING) and flying shadows (voxel_flyshadow, FLYING SHADOWS)
Code: `voxel_tree.c/.h` (`VoxelTree_CrownStrength`, `CrownShade`), `ChunkContentHash` and `Frame_Begin` in `voxel_renderer.c`; `src/platform/deck/deck_flyshadow.c/.h`, hooks in `Frame_ReadAtmosphere` (Update) and `Pass_CastShadows` (Emit).

### Crown shading
- Colour only, no triangles. The vertex `shade` of the crown card (the factor `voxel_lighting` multiplies, so cast shadows, AO and the sun face term still stack) gets a gradient: crown top 1.0, mid 0.91, foot 0.76 at HIGH (the foot is the crown rim and, on a tileset's own 2x2 tree, the trunk base), clamped to >= 0.75. Large crowns: f = 1 / 0.5 / 0 over the two row cards; the small crown is one card, top 1.0 to foot 0.76 linear.
- Sun side: +-4 percent x sun strength across the crown towards `-gVoxelSun.dx` (the sun key is already in `VoxelLighting_Hash`).
- Strength LOW 0.4, MEDIUM 0.7, HIGH 1.0 (`VoxelTree_CrownStrength`; 0 with `voxel_shaders` = 0 and always 0 off Deck). Long grass tufts, tall grass and flowers are untouched.
- Chunks are baked: the strength is in `ChunkContentHash` and a change bumps `sWorldEpoch` in `Frame_Begin` (as `voxel_grass`), so every chunk rebuilds on a change, no restart. Sway (`deck_sway`) is untouched.
- Not done: a cooler tint on the lower third (`shade` is a scalar, a colour needs a vertex format change) and a rim darker than the foot.

### Flying shadows
- `DeckFlyShadow_Update(dt, camX, camZ, outdoor, battle, rain, sun)` each frame; `DeckFlyShadow_Emit(groundY)` in the shadow pass (after the character blobs, before the silhouettes). Fixed pipeline textured quads (alpha mask), colour 0x08,0x14,0x28 with alpha, depth tested, no depth write, polygon offset, on the flat plane `sCamera.ground + 0.03` (no relief query: on a hill the shadow can sink into the slope or float a little).
- Spawn every 25 to 70 s (x2.5 in rain), first one 10 to 30 s after start. Straight line through a point up to 4 tiles off the camera target, starts and ends 17 tiles out, 8 to 16 s by size. Max 2 live. A camera jump over 12 tiles retires them. Indoors, underground, 3D battle, level OFF or `voxel_shaders` 0: none, and live ones are cleared.
- Species (real Pokemon only): the flyers of the current map's land table (`gWildMonHeaders` by `gSaveBlock1Ptr->location`), a flyer being `TYPE_FLYING` in `gSpeciesInfo[].types` or in a short extra list (Vibrava, Flygon, Latios, Latias, Lunatone, Solrock, Dustox), weighted by the real slot odds. The table is used every time when its flyers sum to 20 percent or more, in proportion below that (35 percent under 7). Otherwise a Hoenn fallback: ocean route (Wingull 55, Pelipper 35, Taillow 10), town or city (Taillow 50, Wingull 30, Swellow 20), route by day (Taillow 40, Wingull 25, Swellow 15), by night (sun < 0.25: Zubat 30, Golbat 15, Wingull 15, Swellow 5). 1 in 40 spawns on a `MAP_TYPE_ROUTE` is a big one (Rayquaza, Latios, Latias, Salamence, Dragonite, Charizard). A species without `followers/mon/NNN.bin` (national dex number) is skipped. Water maps skip the land table.
- Shape: the follower file (FMN1: 8 frames, up, up, down, down, left, left, right, right; 4bpp tiles, palette index 0 clear) gives the silhouette: the alpha mask of the two frames of the direction nearest the heading, alternating with the species' wing beat. The mask is cropped to the union box of the two frames and scaled so its long side is the species span in tiles (table `kFlyers`: Zubat 1.2, Wingull/Taillow 1.6, Swellow/Altaria/Pelipper/Crobat/Latios/Latias 2.4, Skarmory/Salamence/Dragonite/Charizard 3.2, Rayquaza 5, other Flying types 1.8), laid flat, stretched 1.2x along the flight line.
- Texture: one 128x64 RGBA8 texture per live shadow (`sTex[2]`), decoded and uploaded once at spawn, never rewritten while it flies (the frame step moves the UVs). It never touches the entity atlas. Draw state is saved and restored.
- Soft edge: three equal translucent copies of the quad grown by 0, 6 and 12 percent; core opacity LOW 0.14, MEDIUM 0.18, HIGH 0.22, x (0.30 + 0.70 x clamp(sun / 0.6)), x0.5 in rain.
- Debug: `DECK_FLYSHADOW_NOW=1` spawns one at once and one every 6 s (up to 2 live, outdoors only). `DECK_FLYSHADOW_SPECIES=<internal species id>` forces the species; no sprite file = no spawn.
- Screenshot: `EXTRA_ENV="DECK_FLYSHADOW_NOW=1 DECK_FORCE_TIME=noon DECK_FORCE_WEATHER=sun"` on a route at HIGH, shoot 1 to 8 s after the first frame. Crowns: compare `voxel_crowns=4` with `=0` in a wood (the shot after a change waits for the chunk rebuild).

## Cloud shadows (voxel_clouds)
Soft cloud shadows drift over the ground and buildings in daylight. Done in the composite (deck_post.c, POST_X) from the scene depth; no texture, no extra texture unit. Levels (`DeckLevel(FAST_EXTRA_VOXEL_CLOUDS)`, table `kCloudMax[3]`): LOW 10 percent, MEDIUM (AUTO follows `voxel_quality`) 15, HIGH 22, max darkening of the red channel in full daylight and clear sky.
- World position: `uCamInv` (camera clip to world) times the depth sample, from `DeckSunShadow_CameraInvVP()` (deck_sunshadow.c, same P*MV capture as the sun resolve, independent of the sun map), copied into `DeckPostFrame.invVP` in `Pass_DeckPost`.
- Mask: 2 octave value noise on world XZ (`cldN`), base blob 9 tiles, second octave x2.3; `smoothstep(0.42, 0.66, n)` gives about 35 percent coverage with soft edges. Drift: wind (0.8, 0.6) in XZ, 0.15 tile/s, from `in->time`; world space, so no shimmer with the camera.
- Colour: multiply by `(1 - s, 1 - 0.88 s, 1 - 0.70 s)`, so the shadow is cool, not grey. Sky (depth >= 0.9999) skipped; fades with view distance 30..58.
- Strength: `s = kCloudMax[level] * cloudAmount * fade * DECK_CLOUDS_STRENGTH`. `DeckPostFrame.cloudAmount` = sun strength x daylight x weather factor: sun 1.0, clear 0.9, shade 0.5, fog and sand 0.3 (also when fogDensity >= 0.05), rain 0, other 0.4. Night, indoors, underground and battles: 0.
- Order: cloud, sun and AO shadows are applied in a pre-pass before the tilt blur (`POST_SHADEONLY` shade target), so the blur works on the shaded picture; bloom and grade come after.
- Debug: `DECK_CLOUDS_STRENGTH=<mult>`; `DECK_CLOUDS_DEBUG=1` shows the mask as grey (white lit, dark cloud) at full level strength, still not indoors or in battle.
- Limits: the shadow uses the surface XZ, not the sun direction, so tall objects get their mask a little offset from a true projected shadow. Needs the depth texture on the scene target (`sTiltOk`) and the POST_X composite. Composite source buffer raised 16384 to 20480 bytes. Cost: one depth tap, one matrix multiply, 2 hashes x 4 corners per pixel, no loops.
- AO default: `sAoInt` 1.0, `sAoPow` 2.6, radius multiplier 1.5 (kAoRadius x 1.5: 1.32 / 1.32 / 1.58 world units); `DECK_AO_*` overrides kept.

### Blur focus test
The tilt-shift focus test uses `tiltCoc()`: the circle of confusion is the minimum over the pixel and three taps lower on screen, so an upright card (character, truck) is judged by its feet and the sharp band no longer sits some pixels above it.

## Battle scenery (voxel_battlescene, Deck/Windows only)
Row BATTLE SCENERY, key `voxel_battlescene`, OFF / AUTO / LOW / MEDIUM / HIGH (AUTO follows `voxel_quality`, `deck_levels.h`). `voxel_shaders` must be on. With the row OFF the 3D battle is the old one (`voxel_battledim` blur and dim, `voxel_battlefx` unchanged).

| Effect | LOW (0.4) | MEDIUM (0.7) | HIGH (1.0) | Where |
|---|---|---|---|---|
| 1 Focus band (tilt-shift replaces the uniform blur) | radius x0.6 | x1.0 | x1.4 (field table) x0.75 | `deck_post.c` kSceneTiltRadius, kSceneBandA/B |
| 1 Dim overlay (needs voxel_battledim not OFF) | 0.05 | 0.085 | 0.12 | kSceneDim |
| 2 Particles (pollen, fireflies, leaves) and rain density | 0.42 | 0.51 | 0.60 of field | `voxel_renderer.c` Pass_Atmosphere3D |
| 3 Bloom gain | 0.7 x0.4 | 0.7 x0.7 | 0.7 | kSceneBloom |
| 3 Shafts gain | 0.6 x0.4 | 0.6 x0.7 | 0.6 | kSceneShaft |
| 4 Foreground bokeh shapes | 3 | 5 | 8 | `deck_battlefx.c` kFgFs, kLeafCount |
| 5 Opponent type glow alpha (additive) | 0.056 | 0.098 | 0.14 (+-15 %, 6 s) | SCENE_GLOW |

1. Focus band: the composite's depth tilt-shift (`uTilt`) focused on `focusDist` (eye to battle target). Blur starts at relative depth error 0.18 and is full at 0.75 (field: 0.06 / 0.40, uniform `uTiltB`). Max radius = field table x0.75. Works indoors too. The old uniform backdrop blur is not run while scenery is on. The overlay (battlers, HP boxes, text) is drawn after the post pass and stays sharp.
2. The Deck particles and rain are drawn in 3D battles outdoors (not underground) with the field's weather and day/night rules (`DECK_FORCE_WEATHER` applies). The box is centred on the camera target (x +-11, z -14..+4).
3. Grade, bloom, LUT and shafts use the user's own settings for those keys (they can still be off). `DeckPostFrame` was already filled for battles in `Pass_DeckPost`. Vignette is part of the grade.
4. Foreground bokeh: one fullscreen quad, procedural discs and leaf lenses in `kFgFs`, x within 0..0.11 of the left and right edge, y in -0.05..0.75 (the text box covers the bottom 28 percent), drift 0.02 to 0.05 screen/s with wrap and fade, alpha <= 0.35 x level. Green/yellow by day, cool dark at night. Drawn in `DeckBattleFx_DrawScene` after `DeckBattleFx_DrawBackdrop`, before `Pass_Overlay2D`.
5. Type glow: `gBattleMons[B_POSITION_OPPONENT_LEFT].types[0]` with the battlefx type colour table, a soft additive disc behind the opponent plus a faint ground ellipse; fades out during a hit flash (1.2 s) and back in.
- Limits: needs the depth texture (`sTiltOk`), without it the battle gets grade, bloom and shafts but no blur. Only the left opponent's first type glows. Indoor battles: no particles, focus band still on. The bokeh is cosmetic and drawn under the UI.
- Debug: `DECK_BATTLESCENE_DEBUG=1` logs `[BATTLESCENE]` every 5 s. `DECK_START_BATTLE=1` with `DECK_START_LITTLEROOT=1`: about 3 s (180 frames) after the field is controllable one wild Poochyena level 3 battle starts (a level 5 Treecko is added when the party is empty); `Deck_DebugBattleTick` in `src/battle_setup.c`, called from `CB2_Overworld`.

## Open items (deck-ao)
Not measured: frame time of the new passes (only GTAO was, about +1.1 ms GPU on the AMD PC). Not checked: WORLD > EFFECTS menu scrolling with the many new rows. Not built on a Deck: all of the above was tuned on Windows. Cancelled or deferred: wandering wild Pokemon in the overworld (cancelled), battle sprite idle animation (deferred). Gotcha: do not `#include <stdlib.h>` or `<math.h>` after game headers in game files (the `min`/`max`/`abs` macros clash); use `extern char *getenv(const char *name);`.
