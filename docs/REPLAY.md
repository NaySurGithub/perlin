# Perlin replays

* **Record**: pause menu -> *Start Replay* / *Stop Replay* (side by side; the inactive one is locked). Recording can start mid-game: the world, entities and players are reconstructed from a state cache. Files go to `<data dir>/replays/*.perlinreplay` (a zip: `metadata.json`, `chunk_N.bin`, optional `thumbnail.png`, `editor.json`). Drop a `.perlinreplay` on the window to import it.
* **Play**: title screen -> *Replays*. Esc toggles the control bar. Keys: Space play/pause, Left/Right +-5 s (Shift +-1 s), `,` `.` speed, C camera mode (free / recorded player / keyframe path), I/O in/out points, M marker, K camera keyframe (Shift+K removes), F1 hides the bar.
* **Keyframes** (bar -> Keyframes): camera path (position, rotation, roll, FOV), playback speed (timelapse/slow motion), time of day (with freeze). Smooth, linear, hold and ease interpolation. Stored in `editor.json` inside the replay.
* **Export** (bar -> Export): any resolution up to 16384, any frame rate (e.g. `23.976`, `30000/1001`, `144`), 1-4x supersampling, range (whole / in-out / custom), hide GUI, file name template, output folder, screenshot, queue. Video goes through ffmpeg (`<data dir>/ffmpeg/ffmpeg(.exe)` or PATH; Windows can download it from the Advanced tab). Formats: MP4, MKV, WebM, MOV, AVI, MXF, TS, FLV, OGV, GIF, WebP, APNG and PNG/JPEG/BMP/TIFF/EXR/WebP sequences, plus *Custom* where any ffmpeg encoder/muxer/extension/extra arguments can be typed in. PNG sequences work without ffmpeg.

## Built-in encoders (no ffmpeg program)
Perlin links FFmpeg's libraries (libavcodec / libavformat / libswscale) statically, so video export does not use or need any `ffmpeg.exe`. The Windows CI build installs them with vcpkg (`ffmpeg[...x264,x265,vpx,aom,webp,nvcodec,amf,qsv]:x64-windows-static`, which makes the binary GPL because of x264/x265) and passes `-DFFMPEG_ROOT=...`; Linux/macOS pick them up via pkg-config. The export dialog lists exactly the encoders and muxers that were compiled in, and "Custom" accepts any of them by name plus `-option value` extras.

If a build has no FFmpeg libraries (`-DPERLIN_LIBAV=OFF` or they were not found) Perlin falls back to an `ffmpeg` executable next to it / in `<data dir>/ffmpeg` / on PATH, and if that is missing too to its own MJPEG `.avi` encoder. PNG sequences and screenshots never need FFmpeg.

## Smooth rewinding, skins and server packs

- **Rewinding never rebuilds terrain.** While a replay plays, the world keeps a per-second undo log (up to 768 MB). Seeking backwards rolls the loaded chunks back to that second and keeps every chunk already meshed; chunks that only exist later in the replay are simply kept. Only if the undo log no longer reaches that far (very long replays) or the dimension changed does Perlin fall back to rebuilding the world from the nearest snapshot.
- **Seeking forwards** just applies the packets in between faster; there is no restart.
- **Recorded player**: drawn standing on the ground (recorded positions are eye height) with movement and rotation interpolated between samples, wearing the skin from their PlayerList entry (updated by skin-change packets).
- **Server resource packs** in use when recording are stored inside the `.perlinreplay` (`packs/` + `packs.json`, including the pack content key) and are mounted when the replay opens, so it looks the same on any computer. Replays recorded before this change have no stored packs.
- **Mouse**: stays free in the replay; hold the right button or press Tab to capture it, Esc/Tab to release.
