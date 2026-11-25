# Title-screen cinematic

The Windows Vulkan build can play the original USA GameCube attract trailer or an optional local video after the Wii U HD opening. The hook runs at `dDemo_c::update`'s “Movie Start Wait” branch in `S_MV000` on USA Rev 2. After playback or a fresh press of the mapped A, B or Start button, that event continues through the game's own transition back to the title opening. The movie consumes the skip press until release, so the next title or save menu sees only a new press.

After installing the game, choose **Title-screen cinematic** on Setup's final screen. You can also hold **Shift**
while starting the launcher and choose it from the setup menu. Select your own uncompressed USA Twilight Princess
GameCube ISO (`GZ2E01`). Select your local FFmpeg executable if FFmpeg is unavailable on PATH, then choose
**Apply GameCube cinematic**. Setup saves the file locations. Keep the ISO and FFmpeg at those paths.

The GameCube cinematic plays automatically when the title screen is left idle. Choose **Use Wii U title screen**
to restore the normal Wii U behaviour. If the ISO or decoder is unavailable at playback time, the game continues
the Wii U loop from the supplied Wii U game files.

The same settings are available through F1 **Display → Title cinematics** in the game. Close the game before
changing these settings in Setup.

The decoder reads `Movie/demo_movie98_00.thp` directly from the ISO and streams decoded pictures and PCM over a local pipe. The movie uses its original 30000/1001 cadence and 32 kHz stereo audio. Vulkan fits the picture to the display with its coded aspect ratio and uses bicubic scaling with overshoot clamping. No asset extraction, network access or intermediate media files are needed.

Set `WWHD_ATTRACT_WIDE_FIT=1` to enlarge the trailer in its sustained widescreen sections. This mode removes only checked matte rows, keeps the picture proportions, and preserves the complete frame in full-height scenes. Use `WWHD_FULLSCREEN=1` for fullscreen presentation, or press F11 in the TV window.

The in-game settings also support a manually selected local MP4, MOV, M4V, MKV or WebM file with video and
audio tracks. Only use media you are permitted to use. Setup never downloads or distributes cinematic files
or FFmpeg.

For scripted testing, `WWHD_ATTRACT_VIDEO`, `WWHD_ATTRACT_ISO` and `WWHD_ATTRACT_FFMPEG` override
the corresponding saved selections for that launch.

Both options use the existing local FFmpeg decoder. Local videos keep their source presentation timestamps and display aspect ratio and use the complete decoded picture. The decoder streams raw video and PCM through a private process pipe during playback. The source media file stays as supplied.
