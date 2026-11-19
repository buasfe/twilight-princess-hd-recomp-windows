# Twilight Princess HD simulation timing experiment

This source snapshot follows the Windows baseline `723a832` and the Ordon camera and gait experiment through `9d435ed`. The standalone Windows probes run without building or launching the game. The Ordon baseline below uses a separate copy of the installed runtime.

## Current clock and update path

| Area | Evidence in USA Rev 2 and the host runtime |
|---|---|
| Frame loop and pacing | `main01` loops back to `02AF0EDC`. Graphics initialization calls `GX2SetSwapInterval(2)` at `02BE4950`. The host honors that interval in `runtime/src/gx2/gx2_core.cpp`, using a nominal 59.94 Hz vsync and waiting for render completion before a flip. Two vsyncs give roughly 29.97 updates per wall second when the game keeps up. The Windows Ordon benchmark logs show about 29–30 swaps/s with interval 2. |
| Presentation | `runtime/src/interp.cpp` halves the effective swap interval and uses 1, 3 or 7 hold passes for 60, 120 or 240 FPS. It advances `logic_steps()` on the full pass. It blends camera at `028E13CC`, models, particles and panes on the drawing side. The existing display cap and Vulkan pacing are independent of gameplay conversion. |
| Execution and input | `main01` polls input at `02AF0A98` and normally advances audio and HD subsystems each iteration. The process manager calls `fpcEx_Handler` at `02AD8364` for actor execution. Hold passes skip it and the priority, create and delete handlers. The SDL test route derives scenario time from `logic_steps() / 30` and would require a separate time source for input intended to change at the midpoint. |
| Counters and collision | `cCt_Counter` at `02BEF838` increments the count at `1028EBC0` on its normal step. The main loop also increments `10182E0C` per pass. `dScnPly_Draw` contains an inline counter at `1028EBC8` and service calls for collision, background motion, scene changes and particles. The hold hook skips those services and restores the draw counter. Removing the hold gates would make these effects advance at the draw rate. |
| Wall clock | `threads.cpp` reports guest time from the host monotonic clock at 62,156,250 ticks/s, with an offset for save-state restoration. The movement and animation sites below use per-step arithmetic rather than a delta derived from that clock. Scaling the guest clock would also affect sleeps, alarms and operations unrelated to motion. |
| Audio and saves | `02AD9F9C` is the frame-side audio service. The AX mixer runs on its own 3 ms cadence in `runtime/src/hle/ax.cpp`. The save-state service runs at the main loop boundary and exposes the last load frame and logic step. Preserve that boundary and the guest clock during the first prototype. |

## What the upstream experiments establish

- **Wind Waker HD.** The early design used alternating full and partial actor passes, scoped a half-step to whitelisted procedures and adjusted per-step translation, smoothing, animation and timers. The later design ran an extra Link and camera preview, then restored state before the original full pass. The local fork history retains both designs in `runtime/src/true60*.cpp` at `7310a61^` and `7a7f744^`. The changelog records problems such as Orca's event command stopping sword training and retires True 60 in #407 because it broke in many places. #436 also records that drawing could consume RNG and change visibility decisions even when actor execution was gated. Use scoped deltas and differential tests from this work. Each extra call still needs a side-effect audit. [Wind Waker HD changelog](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/blob/main/CHANGELOG.md)
- **Hookshot.** Its Switch 2 port already had a nominal 60 Hz step. Above that cap it initially still advanced 1/60 s per frame. #1612 adjusted the clock step under higher caps and the release measured the game clock against wall time with a 240 cap. Frame-counted intro cues still ran ahead of voice lines and Gohma could stall above 60. Later cue changes are listed in the changelog. Measure clock rate, simulation calls and progression independently. [Hookshot v2.0.13](https://github.com/NotRexed/Hookshot/releases/tag/v2.0.13) · [Timing regressions](https://github.com/NotRexed/Hookshot/issues/1742) · [Hookshot changelog](https://github.com/NotRexed/Hookshot/blob/main/CHANGELOG.md)

## Independent timing projects

### Twilight Princess Ultimate Edition

Checked `a8b1e7dc9629` against the zeldaret parent `9f1a64885122` of its last upstream merge. The guide's distinction between continuous deltas and discrete timers is implemented in the main loop. `DELTA_TIME` uses measured wall time in original-frame units, clamped to four frames. `SCALE_TIME` follows the selected cap, so a doubled countdown still takes twice its intended wall time if 60 mode runs at 30 updates/s. Keep the experimental HD step fixed and retain explicit legacy-clock boundaries. [Guide](https://github.com/NoseDevilEugen/Twilight-Princess-Ultimate-Edition/blob/a8b1e7dc9629/DELTA_TIME_GUIDE.txt) · [Main loop](https://github.com/NoseDevilEugen/Twilight-Princess-Ultimate-Edition/blob/a8b1e7dc9629/src/m_Do/m_Do_main.cpp#L64)

- **Translation and collision.** `fopAcM_calcSpeed` scales the horizontal `speedF` contribution. Gravity, the vertical cap and `fopAcM_posMove` are unchanged in that diff. Link has his own scaled `posMove`, translation offsets and special-gravity setup. His actor still calls `CrrPos` once after movement on each execution. The diff leaves `d_bg_s_acch.cpp` unchanged. [Generic movement](https://github.com/NoseDevilEugen/Twilight-Princess-Ultimate-Edition/blob/a8b1e7dc9629/src/f_op/f_op_actor_mng.cpp#L539) · [Link integration](https://github.com/NoseDevilEugen/Twilight-Princess-Ultimate-Edition/blob/a8b1e7dc9629/src/d/actor/d_a_alink.cpp#L12531) · [Link collision call](https://github.com/NoseDevilEugen/Twilight-Princess-Ultimate-Edition/blob/a8b1e7dc9629/src/d/actor/d_a_alink.cpp#L17445)
- **Smoothing and turning.** `cLib_addCalcAngleS` scales the applied signed-short step after clamping. `cLib_addCalcAngleS2` scales the clamped branches and leaves the interior step at its original size. `cLib_addCalc` scales the proportional term and leaves min/max clamps in original units. Check the actual caller's units and quantization before converting any shared helper. For an unclamped camera filter with a held target, the probe's `1 - sqrt(1 - gain)` composes to the original response over two steps. [Float helper](https://github.com/NoseDevilEugen/Twilight-Princess-Ultimate-Edition/blob/a8b1e7dc9629/src/SSystem/SComponent/c_lib.cpp#L40) · [Angle helpers](https://github.com/NoseDevilEugen/Twilight-Princess-Ultimate-Edition/blob/a8b1e7dc9629/src/SSystem/SComponent/c_lib.cpp#L239)
- **Camera and animation.** `chaseCamera` scales several continuous follow and orbit terms. `Run` still increments the style timer, frame counter and tick count per call. Link scales the rate when setting the frame controller, and face texture animation advances by a delta. A rate stored during setup carries that setup frame's delta until another write. This does not establish correct cue timing at a mode transition or during a hitch. The available checks I found were build/progress CI and fix descriptions. Measured 30/60 route comparisons and collision or camera regressions remain unverified. [Camera](https://github.com/NoseDevilEugen/Twilight-Princess-Ultimate-Edition/blob/a8b1e7dc9629/src/d/d_camera.cpp#L1102) · [Animation](https://github.com/NoseDevilEugen/Twilight-Princess-Ultimate-Edition/blob/a8b1e7dc9629/src/d/actor/d_a_alink.cpp#L6430) · [CI](https://github.com/NoseDevilEugen/Twilight-Princess-Ultimate-Edition/blob/a8b1e7dc9629/.github/workflows/build.yml)

### Other engines and the HD implications

- **zeldaret/tp** at `9f1a64885122` provides the original call order and the collision contract. `CrrPos` starts by applying moving-background correction and uses both previous and proposed positions for conditional sweep tests. A second HD movement step must start from the first corrected position, with the appropriate previous-position data. Begin measurements on static ground and test a wall and slope before considering moving platforms. Reapplying the same platform transform or skipping a sweep when the displacement halves can change contacts. [Original collision source](https://github.com/zeldaret/tp/blob/9f1a64885122/src/d/d_bg_s_acch.cpp#L305)
- **BreathWindRecomp** at `06a0ab5a89bb` patches a pre-existing delta clock and Havok's solver microstep count. Its reported held-cucco jump height was 0.634 m at 30 FPS and 0.640 m at 60 with the solver patch. It also provides an event cap and forces the known frame-counted `Demo014_0` scene to 30. Reuse the comparative tests and legacy-event boundary. The numerical solver setting belongs to Havok. [Timing notes](https://github.com/Ctrl-Arnav/BreathWindRecomp/blob/06a0ab5a89bb/docs/SAVAGE-WIND.md#L678) · [Runtime implementation](https://github.com/Ctrl-Arnav/BreathWindRecomp/blob/06a0ab5a89bb/runtime/hle/framepace.c#L105)
- **Meowmaritus's Wind Waker hack** at `13696724637a` implements a low-rate phase flag from the frame-counter modulus alongside scaled movement. Its author reports doubled AI aggression, timer-driven softlocks and a failed scripted jump. Apply the phase idea specifically to known HD timer writes and keep discrete dispatch gated. [Phase implementation](https://github.com/Meowmaritus/Wind-Waker-60FPS-Hack/blob/13696724637a/SOURCE%20ASM%20FILES/FPSHack_01_MAIN_UPDATE.asm#L35) · [Reported gameplay failures](https://github.com/Meowmaritus/Wind-Waker-60FPS-Hack/blob/13696724637a/README.md)
- **SM64-60-Project** at `36cd7a1fadcb` uses the 60 FPS flag for interleaved render passes. The game loop gates level-script execution and its timer, and the threaded path waits two game vblanks. Its existing ground solver does four quarter-steps, each using the corrected position from the previous one. That sequencing is applicable to an HD collision fixture. Its 60 FPS flag is evidence about presentation, with game logic still on the original tick. [Game loop](https://github.com/CalebVernon/SM64-60-Project/blob/36cd7a1fadcb/src/game/game_init.c#L805) · [Ground solver](https://github.com/CalebVernon/SM64-60-Project/blob/36cd7a1fadcb/src/game/mario_step.c#L347)

### Implemented camera counter experiment

The Windows differential probe now executes the four recompiled blocks ending in the five camera timer writes at `+0x160`, `+0x170`, `+0x174`, `+0xA8` and `+0xAC`. It changes only the ten identified increment operands to add one on the legacy boundary or zero on the intervening step. The synthetic run exercises both post-mode-dispatch paths and the path that advances only the two shared timers. It also checks unsigned wrap, the return register and the flag write and comparison preserved on the counter-only path. After 30 legacy ticks all five counters match stock at every shared sample. `+0xA8` advanced 30 times in stock and gated runs and 60 times in the ungated control. The test covers these post-dispatch writes in isolation. Camera mode dispatch, live input and the second native collision correction remain integration work.

## Windows probe

`tools/audit/run_timing_probe.py` checks the local RPX revision, extracts selected blocks or leaves from the existing generated C, compiles them with the unchanged PowerPC helpers, and runs them against synthetic state. It invokes these routines 30 or 60 times in a nominal second. Inputs and upstream geometry are synthetic. The output and extracted code remain in git-ignored `build/timing-probe/`.

| Area | Local entry point and measured result |
|---|---|
| Acceleration and displacement | Link's speed update at `0203244C` adds a per-step increment to `Link+0x3458` and caps it at `020324B0`. The position path at `02071E68` or `02071EB8` uses the vector-add leaf `02C379DC` on position at `Link+0x4D0`. With acceleration 1.5 and cap 12, both runs reach 12 and travel 318 units. With cap 11.5, the run at 60 Hz travels 306.625 versus 306.5. The end-velocity correction is exact for constant linear acceleration at shared sample times. Clipping inside an interval changes the result. |
| Turning | The wrapped 16-bit angle helper `02BEFF08` is called at `020411FC` on Link's movement yaw `+0x4DE`. Doubling the divisor and halving the limits yields the same final yaw. The worst shared-sample difference was 24/65536 of a turn (0.132°) in the ordinary case and zero in the tested wrap case. Integer division and the minimum-step threshold need separate coverage. |
| Animation | `J3DFrameCtrl::update` at `02B520C4` adds the rate at `+0x08` to its frame at `+0x0C`. With repeat mode, a loop of 24 frames and an original rate of 1.25, both runs matched at every shared sample and ended at frame 13.5. The probe measures frame phase. Animation cue consumption, model evaluation, blending, root motion and animation-driven combat remain to be traced. |
| Camera | Camera execute `028DF72C` calls `028D8AA4` at `028DF8A8` and `028DF8DC`. The distance filter writes `camera+0x2B0`, applying a native gain of 0.01. The probe scales that gain to `1 - sqrt(1 - gain)` per half-step. Shared-sample error was 0.000092 distance units and the first update took effect at the 16.67 ms sample. These are scalar filter results. The full camera view, mode transitions, target tracking and occlusion are untested. |

Run from this worktree on Windows with LLVM MinGW available. Pass the path to the same local USA Rev 2 RPX used to create the generated directory.

```powershell
python tools/audit/run_timing_probe.py --gen build/gen --rpx <local-Zelda.rpx>
```

## Ordon horizontal orbit and gait follow-up

The Ordon camera and gait experiment uses the same scheduler and grounded collision path as `1753c1a`. For local tests, follow the Windows build guide, keep a separate test save and set `WWHD_INTERP=0`. Set `WWHD_ORDON_TRUE60` to `1` for the experiment or `0` for stock. The Windows validation used 1× at 16:9.

- **Horizontal orbit.** The settled chase style splits the native horizontal rate filter into two gains that compose to the original gain. The first increment uses the native stick value after normalization. The supplemental pass samples the host stick, reuses the native normalization for a held input, and applies the measured axis gain when the sample changes. The original dead-zone test, angular conversion and response curve govern the update. The supplemental view is checked against native collision before publication. The original camera mode dispatch and five counter gates still run at the legacy cadence.
- **Gait.** After supplemental collision correction, the runtime runs the native frame controller on a temporary copy at half rate for active looping clips. The resulting phases are supplied to the native Link joint and envelope evaluators. The body uses its corrected world transform and the face, hat, sheath, sword and shield follow their body joints. The guest animation frames, morph history and callback state are restored immediately. The resulting model matrices are available to the native painter and restored before the next gameplay dispatch. Live frame controllers and event dispatch remain on the legacy tick, including footsteps, hit and state-transition processing.
- **Vertical camera.** In Ordon the vertical stick enters the HD zoom/style routine at `028D8CB4`, which changes persistent mode and range state at camera offsets `+0x948` and `+0x94C` through `+0x984`. The chase solver consumes this state for elevation and framing. A second call or a generic pitch increment would change transition behaviour. The vertical path stays on its native 30 Hz clock until the continuous zoom changes can be isolated from mode transitions and reapplied through the chase solver.

### Windows validation

The scripted route covered held horizontal orbit from rest, release, reversal, vertical input, walking, running and turning. In the first sustained stationary turn, median stock rotation was 2.417° per update. The experimental pass medians were 1.200° and 1.219°, with the same direction on both passes. Average angular speed in that window was 69.6°/s stock and 67.1°/s experimental. The subsequent turn while walking delivered 50 consecutive present transitions with both a new horizontal angle and a changed leg configuration. Turn rates during moving sections varied with route and camera collision.

The verifier reported a separate streak of 125 consecutive fresh movement, camera, body and skeletal states at 60.38/s. All 818 successful experimental presents matched their painter tickets. The supplemental animation trace recorded 370 lower-body samples, including 11 phase wraps, and the measured non-wrap increments matched the half-rate step. The skeletal signature uses distances between the body root and boots and between the boots, so rigid movement or rotation cannot account for those changes. Two consecutive TV captures showed different poses and views. Screenshot capture perturbed the loop, so timing comparisons use other segments.

Use `tools/audit/verify_ordon60.py --require-skeleton --min-run 90` with the saved stock log at `build/true60-motion-validation/stock/route.log` and final log at `build/true60-motion-check/true60/route.log`. The run used scripted input and disabled audio. Physical controller feel, audible footstep timing and 21:9 at 2× need hands-on checking. Supported looping poses were tested, while one-shot animations and cutscenes remain on their legacy paths. Occlusion can still drop a supplemental camera update.

## Independently scheduled Ordon walking preview

The Windows candidate adds an opt-in path for grounded walking on static terrain in Ordon Village. Run it with `WWHD_ORDON_TRUE60=1` and `WWHD_INTERP=0`. Use `WWHD_ORDON_TRUE60=0` for stock. The private preview under `build/Ordon True 60 Preview` contains both launchers, a copied save and controller bindings, and the exact binary used for both measurements.

- **Scheduler.** The native loop alternates an original gameplay pass and a supplemental pass, one nominal 59.94 Hz GX2 vsync apart. The original pass owns actor dispatch, script and button edges, audio, events, and frame counters. The supplemental pass uses the existing hold gates for those services, then runs the scoped walking and camera code at the native actor-dispatch site. A game-thread pair ID connects the two passes across the legacy swap. The normal pad read moves after the native flip wait when the loop takes that path, and the supplemental pass samples the host left stick again.
- **Movement and collision.** The initial correction sweeps half the native requested displacement. The supplemental correction sweeps from the first *corrected* position, temporarily supplying that position as native collision's previous position and restoring the original previous position afterward. Fresh analog input can adjust the supplemental direction and speed within bounded native rates. Collision uses the native floor, wall and roof checks. The supplemental sweep omits moving-background correction and requires supported static ground.
- **Camera and drawing.** The settled chase camera splits the native center damping and follows the corrected Link position on the supplemental pass when the background ray check permits it. The camera process publishes its updated eye, center and view matrix through the native path. The five legacy camera counters retain their original cadence. Native Link Draw consumes the corrected body and attachment root transforms, using the joint matrices from the most recent skeletal evaluation. Both passes go through the native painter with interpolation disabled.
- **Measurement.** `WWHD_ORDON_TRACE=1` logs separate clocks for input, movement, collision, camera, native draw, Link body root and legacy actor steps. The painter records the revisions it consumes and passes a ticket through the GX2 command queue to the successful Vulkan TV present. This measures accepted Vulkan presents and the game state passed to the painter. Display scan-out remains unmeasured.

### Windows route comparison

Two Windows/Vulkan launches used the same candidate, 1× rendering at 16:9, copied saves and the scripted Ordon warp `170:F_SP103,1,0,6`. The route supplied straight walking, turns, stops and right-stick input. Independent hold passes were enabled only in the experimental launch. During the sustained experimental segment, 189 consecutive present-to-present transitions spanning 3.16 s had one new collision-corrected Link position, camera eye and Link body root each. The equivalent longest stock streak was 95 transitions at 29.95/s.

| Timing | Stock | Experimental |
|---|---:|---:|
| Movement and native collision, sustained comparison window | 29.93/s | 60.07/s |
| Camera and body root, sustained comparison window | 30.24/s | 60.07/s |
| Successful TV presents in that window | 30.24/s | 60.07/s |
| Legacy actor dispatch in that window | 29.93/s | 30.20/s |
| Median movement interval in that window | 33.36 ms | 16.66 ms |
| Longest consecutive fresh-state present cadence | 29.95/s | 59.85/s |

Experimental movement alternated a 16.16 ms median from the original commit to the supplemental commit and 17.24 ms in reverse in the comparison window. Four consecutive experimental TV captures showed changing images. Capturing them stalled the loop, so the cadence comparison excludes that part of the run. From 0.5 to 0.7 seconds after the first straight-walk commit, the measured horizontal speeds were 298 and 308 game units/s, respectively. Early acceleration differed and the full routes diverged after turning, so this test establishes sustained speed in the same range and leaves exact trajectory parity open. Across the entire capture, including startup, stops and stock fallback, the candidate averaged 43.45 movement commits and 53.59 presents per second. Both runs matched every accepted present to a ticket (454 stock and 835 experimental), with zero interior drops or duplicate tickets.

From the timing worktree, run `python tools/audit/verify_ordon60.py --min-run 120` for the full saved logs. Set `--start 7.079 --end 10.237` to reproduce the sustained comparison. The verifier also accepts two explicit logs for future A/B runs.

The experiment leaves skeletal animation, manual camera orbit, combat and other actor systems on the legacy clock. Walking, unsettled camera transitions, ledges, moving backgrounds, events, full-game progression and saving in experimental mode have not had broad compatibility coverage. A physical controller, 21:9 and 2× rendering were outside this validation route. Next expand the Ordon checks to slopes, walls, stops and reversals, camera occlusion and input in both controller modes. Compare native joint phase and root motion at common sample times before raising the skeletal clock.

## Earlier paired walking prototype

The prototype at `e47aa29` used `WWHD_ORDON_SUBSTEPS=1` and `WWHD_INTERP=0` to enable paired correction in the normal Ordon Village walk procedure. It deferred to stock during events, on unsupported movement states and backgrounds, during the settings overlay, in interpolation modes and during turbo or uncapped play.

At `0208634C` the runtime takes Link's position before the normal movement routine. At the Link-only call to `028C56B8`, it splits the proposed displacement and runs native background collision correction through each half. The second sweep reads the first corrected position as both its starting position and the collision previous-position input. The legacy previous-position field is restored for downstream actor logic, and moving-background transforms can apply only on the first correction. The walk procedure, input edges, animation and post-move actor dispatch still run once per legacy tick. The five post-dispatch camera counter gates now run at their native instruction sites and allow each field to advance once per legacy tick for the active camera. The distance filter uses two composed half-steps at its original native call sites.

The Windows Vulkan smoke run used the existing Ordon warp, a copied save, and a scripted walk, turn and wall approach. It exited normally. During steady walking the log recorded 59.8–59.9 movement commits and collision corrections per wall second, including one window with zero wall contacts and another with wall contacts. At idle the rates returned to about 30. Full camera executes and the runtime's legacy step counter stayed near 30. The filter logged around 60 scalar steps per one-second reporting window during steady play. No duplicate camera counter writes were observed in this route. A separate run through the packaged stock launcher exited normally and recorded 300 native Link movement entries over 300 stock frames, matching the baseline procedure counts.

Both movement corrections currently happen inside the same actor tick. The full camera, player input sampling, animation and presentation stay at 30 Hz. The next upgrade needs a separately scheduled half-step, a way to refresh Link's pose and attachment transforms between actor ticks, and a continuous camera update isolated from mode dispatch and its other state writes. Deferring just the second collision call would leave pose and interaction state derived from the midpoint. Running Link or the camera's full execute twice would also repeat the legacy per-call work documented below.

## In-game baseline and integration boundaries

### Windows Ordon baseline

The existing `WWHD_WATCH` hook, the local executable and a copy of the normal save produced a baseline without rebuilding the game. The full benchmark state could not reload in this Windows build because the Amiibo guest thread had a different stack address. Two launch attempts stayed at the title screen. The third used the existing debug warp and reached `F_SP103` with Link under control. A TV capture and the stage marker confirmed the location.

Use a scratch copy of the game runtime with `portable.txt`, the runtime DLLs, and a separate copy of the normal save. Set the game path with `--game` and give `--save` the copied save directory. These were the existing test settings for the successful run.

```powershell
$env:WWHD_WARP = '170:F_SP103,1,0,6'
$env:WWHD_INTERP = '0'
$env:WWHD_TEST_ORIGIN = '245'
$env:WWHD_TEST_STICK = '1-2.2:0:0.75,2.2-3.4:0.75:0'
$env:WWHD_TEST_RSTICK = '3.8-4.7:0.65:0'
$env:WWHD_WATCH = '0207151C'
$env:WWHD_WATCH_EXPR = '3+305c'
$env:WWHD_WATCH_LIMIT = '700'
$env:WWHD_EXIT_AT_FRAME = '635'
$env:WWHD_NO_HOST_INPUT = '1'
$env:WWHD_NO_GAMEPAD = '1'
$env:WWHD_NO_AUDIO = '1'
$env:WWHD_HIDDEN_WINDOWS = '1'
```

Capture standard error to a private log. Run `python tools/audit/count_ordon_steps.py <path-to-log>` from the experiment worktree. It requires an Ordon stage marker and counts only Link's movement entry from the actual actor call at `0208634C`. From flip reports 301 to 601 it counted exactly 300 entries, with 253 in procedure `0x0003` and 47 in procedure `0x0004`. This window measured 25.3 updates per wall second with tracing enabled. Title-only logs fail the check. The log and screenshot are in git-ignored `build/ordon-timing-baseline/`.

### Integration boundary

- **Link.** The procedure table initialized at `0202159C` resolves the observed idle and walk paths to `0208D9D8` and `0208DBE8`. Both call `020419D0`, which runs locomotion and can take state-changing branches, and `0203FFD4`, which handles environment-dependent transitions. The actor calls these procedures before `0207151C`. That movement routine calls through actor state, computes velocity and changes position. The caller then runs background collision correction at `020863C8` before committing downstream pose and interaction state. Doubling the procedure and actor dispatch repeats transitions. Doubling just the tested speed, yaw and position operations applies a second move without the matching collision correction.
- **Camera.** The gameplay camera execute at `028DF72C` reads input, dispatches the active camera mode, and increments multiple integer counters after the mode call, including `+0x170`, `+0x174` and `+0x160` from `028DFD04`. The scalar filter from the probe updates only one part of that state. Varying its gain does not scale orbit and collision branches or the mode timers. Calling the full execute a second time would advance those counters twice.

The runtime prototype covers two committed collision corrections inside the existing walk tick and the named camera counter writes. A separately scheduled motion step and a continuous camera update remain to be integrated.

### Next implementation steps

1. Trace the same route for position, velocity, yaw, animation phase, collision flags before and after `020863C8`, and the camera's active dispatch target and eye/center. Keep the entry counter as a cross-check. An instruction-site trace can be built from affected recompiler chunks and one runtime object using the existing compile artifacts where inputs and flags match.
2. Extract the continuous portion of the observed walk and free camera modes from state transitions. Advance it using newly sampled analog input between legacy ticks and run the original geometry correction on each movement step. Keep action edges, animation cues and actor/event dispatch on the legacy tick until separately audited. Cover the speed-cap crossing, stop and reversal, wall and slope contacts, camera collision and dialogue entry before widening the allowlist.
3. Gate the path behind an explicit experimental start flag. Count each committed movement and camera update after correction, stamp each with monotonic time, and report both counts separately from presents and legacy actor dispatch. A valid target run shows two commits per legacy tick and approximately 60 commits per wall second under steady load. Reuse a baseline/experiment pair of short runs, including pause, return to play and mode exit. A save or load requires a defined tick boundary.

## Feasibility beyond the probe

| System | Work needed before changing frequency |
|---|---|
| Combat and physics | Convert gravity, drag, acceleration, root motion and movement together. Preserve collision ordering, swept contacts, moving platform displacement and invulnerability durations. Repeated contacts need a once-per-event rule. Test sword hits, knockback, jumps, wall slides and fast projectiles. |
| Enemy AI and world state | Convert decision timers deliberately. Repeating processes can reorder collision callbacks, consume RNG or trigger drops twice. Keep legacy AI and script clocks until separately measured. |
| Cutscenes and events | Advance animation continuously where safe. Dispatch cues when their times are crossed and keep one-shot command transitions on an audited clock. Check dialogue, doors, triggers, camera cuts and boss phase transitions. |
| UI | Keep menus, fades, repeats and dialogue on their legacy clock at first. The existing hold hooks illustrate why those callbacks must stay gated even when drawn more frequently. |
| Audio | Keep AX mixing and alarm time stable. Clock animation-driven sounds and rumble by threshold crossings and preserve voice start/stop ordering. Check audiovisual sync through dialogue and scene changes. |
| Saves and replay | Preserve normal save layout. Save states that include experimental fractional timers, input state and tick phase need versioning or a safe legacy boundary. Log RNG and event ordering across repeat replays. |

Fixed 60 Hz is the first defensible target. It gives an exact 2:1 relationship with the legacy step and reproducible input and event ordering. Higher fixed rates such as 120, 180 and 240 Hz are integer multiples of 30, but shrink the simulation budget to 8.33, 5.56 and 4.17 ms per step and still require independent collision and determinism validation. A 144 or 165 Hz display has no integer relationship to the 30 Hz schedule. Once available, a fixed verified simulation can feed any refresh rate through independent presentation. Arbitrarily varying gameplay step sizes with the display would also vary collision results, integer timer rounding and the order of reactions under load. There is insufficient evidence here to offer any higher-frequency gameplay mode.
