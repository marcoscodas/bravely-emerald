#!/bin/sh
# usage (on the Deck, repo root): perf_matrix.sh   -> shots/perf/<name>/report.txt, summary on stdout
K="voxel_blur voxel_grade voxel_bloom voxel_sway voxel_grass voxel_rain voxel_wet voxel_mist voxel_splash voxel_particles voxel_glint voxel_outline voxel_shadow voxel_lut voxel_bump voxel_shafts voxel_tiltshift voxel_ao"
all() { for k in $K; do printf '%s=%s ' "$k" "$1"; done; }
out=/run/media/deck/SD512/emerald-deck/shots/perf
run() { # name setkey extraenv
  SETKEY="$2" EXTRA_ENV="$3" timeout 140 sh tools/deck/run_autoplay.sh tools/deck/autoplay/still.txt 75 "$out/$1" >/dev/null 2>&1
  echo "=== $1"; cat "$out/$1/report.txt" 2>/dev/null; echo "GLERR lines: $(wc -l < "$out/$1/gl_errors.log" 2>/dev/null)"
}
run legacy "voxel_shaders=0" ""
run low "$(all 2) voxel_res=0" ""
run mid "" ""
run high "$(all 4) voxel_res=4 voxel_pitch=4" ""
run high_novsync "$(all 4) voxel_res=4 voxel_pitch=4" "vblank_mode=0"
run mid_novsync "" "vblank_mode=0"
echo DONE
