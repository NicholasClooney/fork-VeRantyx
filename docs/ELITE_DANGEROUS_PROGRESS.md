# Elite Dangerous support progress

Last updated: 2026-10-20 (UTC).

Goal: run the legally installed Windows game on VXRuntime with Apple Vision Pro and keyboard/mouse input.
Current status: **ED connects to SteamVR, but the captured VR image is still the SteamVR waiting environment.
Its own scene rendering and playable status are not verified.** The desktop window was previously reported black;
its latest contents, authentication, physical input and real AVP playback remain unverified. The official launcher
has not been implemented or bypassed.

The first implementation snapshot is [a69ce22](https://github.com/NicholasClooney/fork-VeRantyx/commit/a69ce221932a36e59e10f610fd1daca5e4cdd5bb)
on `codex/elite-dangerous-vr-support`. This page is a coarse progress log, updated as verified work lands.

## Problems and changes

| Problem | Change and observed result | Remaining limits |
|---|---|---|
| Startup stopped at missing Windows/CRT functions | Implemented critical-section spin counts, the CRT heap handle, UTF-16 `wcsncpy`, `_wtof` and Windows error messages. Successive harness runs advanced past the observed calls. | These are bounded compatibility implementations, not complete UCRT coverage. |
| Exception handling failed while reporting an earlier null call | Added honest failure results for `MiniDumpWriteDump` and the unsupported NT thread setter. Fixed a missing `WINAPI` declaration in the C SEH unwind path; the same ordinary-exception fixture failed before and passed after the fix. | No dump writer or debugger-hiding behavior is implemented. Unsupported operations do not report success. |
| Game reported network initialization failure | Added opt-in, bounded status-only diagnostics. `GetBestRoute` failure had a fallback; failed adapter enumeration produced an empty candidate list. Implemented real Darwin adapter, DNS and gateway snapshots with Windows buffer layouts and bounded address-lifetime mappings. The next valid run returned successful enumeration, no longer logged that network dialog, and advanced to input initialization. | This does not prove every network operation or authentication works. Unknown address sources and unsupported flags fail explicitly. Proxy configuration and arbitrary destination-route queries remain unsupported. |
| Game ignored DirectInput creation failure and dereferenced the absent object | Implemented real DirectInput8 A/W root objects, shared COM identity and reference-counted lifetime. Subsequent runs moved beyond that dereference. | Device enumeration/creation still fail explicitly; no fake devices or successful empty enumeration. Physical input is unverified. |
| Later startup calls were missing | Added explicit failure compatibility for ANSI volume-name lookup and named timer opening, plus real `SearchPathW` support for explicit paths/directory lists. Subsequent runs reached an ED SteamVR Scene connection. | No fabricated volume GUIDs or named timers. Default path-search policy remains unsupported. |
| Diagnostic runs could expose streaming listeners or affect shared game files | Added loopback-only encoder mode, disabled audio/ALVR for these runs, explicit product selection and head-pose-only scenarios. CRT rename preserves original overlay-source files; overlong shadow paths fail closed. Runs use independent game/dependency copies. | Overlay rename has no deletion tombstones; this is not a host-filesystem sandbox. LAN pairing security and real-headset operation were not tested. |
| Measurement could fail or overstate progress | Fixed short encoder-sample indexing, recognized the known network-error dialog, and added default-off network/window/render/activity diagnostics with payload restrictions and limits. | Automatic `controls` reflects legacy bindings and rule checks, not demonstrated input or ED rendering. Compositor/background FPS is not ED FPS. |

## What the measurements show

- `20261018T174219Z-elite-keyboard-diagnostic`: first confirmed ED Scene connection, after 21.3 seconds (`o2`, `d3`).
- `20261018T181508Z-elite-keyboard-diagnostic`: connected after 13.0 seconds; no observed unhandled exception or
  missing-call termination. At 30/40/50 seconds, one visible window remained; covered work submissions and Present
  stayed at 85 while window callbacks and process CPU time increased (`o20`, `o21`). These counts include DLLs
  loaded into the selected process. They do not identify a deadlock, login state or game scene frame.
- `20261018T212525Z-elite-keyboard-diagnostic`: a 180-second observation run connected after 13.0 seconds. The user
  described the desktop ED window as “just a black screen.” This is a separate human observation, not an automatic
  metric, screenshot or playtest. The run's sealed evidence was not rewritten to include it.

Run IDs identify **local, uncommitted evidence**; raw runs, screenshots, logs, game copies and third-party binaries
are not published with this branch. They are not publicly accessible test artifacts. The profile retains these
references without adding a performance baseline or a playable claim.

## Validation and known exception

Focused native/Windows PE/COM, adapter-packing, overlay, privacy/limit and harness tests passed. The final
implementation at the first snapshot also passed a complete `make -C VXRuntime check` execution.

An intermittent D3D12 query test still returns a zero second timestamp. Native Metal experiments reproduced an
early-resolve problem outside VXRuntime, but did not establish an Apple driver bug or a safe runtime fix. The user
explicitly waived this known test blocker for continued ED diagnosis. Original assertions and failure records are
retained; a later passing check does not mean the intermittent problem is fixed.

ARM64 full-suite, other-game `vr-check`, and real AVP playtests have not been completed for this change set.
Adapter UBSan fixtures passed; ASan did not complete. Some diagnostic branches remain unexercised. OpenVR Submit
is not instrumented; we do not infer its absence from missing logs or a background capture.

## Bounded CPU investigation

The system `sample` approach did not recover a known busy-loop hotspot reliably in the Rosetta fixture, so it was
not applied to ED. A separate default-off PC-only diagnostic passed the fixture gate and focused privacy, identity,
limit and conflict tests. It takes one two-second window, with a 20 ms CPU timer and at most 128 retained samples.
It records fixed module identifiers and relative PCs only: no stack, thread names, window text or request payloads.
This uses process-wide SIGPROF/ITIMER_PROF resources, rejects existing conflicts, and retains an inactive handler
until exit to handle late signals. Concurrent changes by other native libraries remain a limitation.

`20261018T214714Z-elite-keyboard-diagnostic` connected after 12.0 seconds (`o2`). The first CPU window completed in
2011 ms with 116 samples (`o22`), but only one mapped to the main image; 115 remained unknown. This is insufficient
to locate the black screen and does not mean the process was idle. Sample weights are not all-thread CPU percentages.
The fixed module allowlist follow-up, `20261018T215239Z-elite-keyboard-diagnostic`, connected after 12.9 seconds
(`o2`, `d3`). Its 2001 ms window retained 114 samples, all mapped: 112 in the system kernel library and two in the
main image (`o22`). Without stacks, this does not identify the callers, waiting objects or black-screen cause.
Present/work commits remained at 87 while callbacks and CPU time increased (`o21`). OpenVR Submit remains unknown.

The final focused tests, including module mapping and clock-failure cleanup, passed. The initial CPU version passed
full `make check`; the final allowlist version encountered the known D3D12 second-timestamp failure. This remains
an explicit test exception, not a passing final full suite or a black-screen fix.

## Next work (resumed 2026-10-20)

1. Investigate the 11 translated shaders that fail Metal compilation in `20261019T181052Z-elite-keyboard-diagnostic`:
   vector float2/float3 expressions are cast to scalar half, and uint2 to scalar ushort. The next lead is
   DXBC-to-MSL half-conversion handling (`dxbc.c`). The resumed investigation identifies F32TOF16/F16TOF32
   templates, rather than establishing a minimum-precision operand-flag problem; see the follow-up below.
2. Recheck the later Present/main-commit plateau at 1401/2231 after those compilation errors. CPU and window
   callbacks continue; neither a deadlock nor the causal link to those shader failures is established.
3. Establish transparent real OpenVR Submit evidence if needed. Submit remains unobserved, the final stream is
   still the SteamVR waiting background, and ED has not been demonstrated to draw its own scene. Do not introduce
   a behavior-changing Valve proxy or infer eye submissions from desktop Presents or stream FPS.
4. The matched 5de5ab7 versus 222e4d0 ABBA performance comparison was not done. The single B run's near-60 stream
   rate is not an A/B conclusion. Keep stage progress, settings, load and cache/overlay state visible in a later comparison.

Work resumed on 2026-10-20 to investigate the shader failures. Harness runs and other-game vr-check remain with
the coordinator. The sections below retain chronological evidence; older pending/paused statements refer to that time.

## Wait/caller diagnostic implementation (2026-10-19)

The opt-in `--wait-diagnostic` records three read-only snapshots at 30/40/50 seconds into
`wait_diagnostic_samples.jsonl`, sealed as the `wait_diagnostic_samples` observation. It records fixed API and
phase labels, allowlisted module-relative callers, anonymous thread/address-or-handle tokens and entry/normal
return counters. The last observed Present thread token can be correlated with its current instrumented calls.
Wait/lock/event/IOCP and D3D11 Map/Flush/GetData/Present/keyed-mutex paths are covered; OpenVR Submit is still
`not_instrumented`. No Valve code or vtable is patched, and no additional rendering or synchronization is requested.

Limits: at most 64 threads for the process lifetime, eight nested calls per thread and 256 object-address tokens.
Snapshots retain the active call stack or the last normally completed outer call, not a chronological trace.
Object tokens do not establish object lifetime across address/handle reuse. Multi-object waits have no object
list. `active` means entry without an observed normal return, not proof of a live thread, deadlock or CPU use;
nonlocal SEH/longjmp/thread termination can bypass cleanup. Module attribution uses mappings available at snapshot
time, so unloaded/reused mappings are not a historical identity guarantee. Native/third-party waits outside the
covered APIs are unknown. Overflow, token-table contention or snapshot omission explicitly marks incomplete data.
It is disabled on ARM64 and conflicts with `VXRT_TRACE_CALLS` because that facility rewrites callers. The diagnostic
adds overhead and is not suitable for performance comparisons with an uninstrumented run.

Implementation and fixture validation do not identify ED's black-screen cause. A new game harness run is pending.

Validation: native core and actual sync-wrapper fixtures passed, seven transport/schema tests passed, and real PE
fixtures verified an already-entered wait, main-image caller attribution, the last Present thread and D3D11 API
assertions with diagnostics enabled. A maximum 64-thread/eight-frame snapshot validated at 104740 bytes. Independent
review identified a suspend-sensitive token-table lock; it now uses trylock and reports unknown/overflow on
contention, with a regression test. The full `make check` run reached the known waived D3D12 zero second-timestamp
failure; every original test after that failing command was then run separately and passed. This is not a fully
passing `make check`. An earlier sandbox run could not access Metal, and the first unsandboxed run missed the CPU
fixture's 0.2-second startup sample; the unchanged CPU fixture passed in the subsequent full run. ARM64 and game
harness validation remain pending.

### First wait/caller run

`20261019T165747Z-elite-keyboard-diagnostic` passed result validation. It connected after 36.7 seconds (`o2`, `d3`),
with no observed unhandled exception or missing-call termination (`o14`, `o16`, `o18`). The final `frame_end.png`
still shows the SteamVR waiting background; automatic `controls` is not a new milestone or a playability claim.

At 30/40/50 seconds, Present entered/completed/success all increased 1 → 2 → 108 (`o21`). This run therefore does
not establish the earlier plateau at 87. Its last Present thread token was 5 in all three snapshots (`o22`): last
completed LeaveCriticalSection at `main_image+0x55b891`, then SleepConditionVariableSRW at `+0x5d6d7f`, then last
completed EnterCriticalSection at `+0x54c1ee`. These are different calls, not one continuous blocked wait. The
thread's aggregate covered-call counts were 6732529 → 11972667 → 12856447; these are not per-API frequencies.
All wait snapshots had `overflow=true`, so missing object tokens or records cannot establish absence of waits.

Read-only static analysis of the observed caller sites identifies a 10 ms shared-mode condition-variable wait in
a generation-based worker loop. The observed critical-section wrappers maintain their own owner/recursion fields
at +0x30/+0x38, rather than reading Windows CRITICAL_SECTION owner/recursion fields. Allocator contention or task
workload remain hypotheses; this analysis does not establish a specific incorrect Windows API behavior. No sync
or rendering semantics were changed from these observations.

Because Present still grew during the sampled interval, `--diagnostic-delay 60` now shifts the same three bounded
snapshots to 90/100/110 seconds (recorded in each diagnostic configuration), for a 120-second follow-up. The default
remains 30/40/50 seconds; this changes observation timing only. OpenVR Submit, authentication, game-scene rendering
and physical input remain unknown. Longer connection time with broad instrumentation is not a measured performance
regression against the earlier run.

The delayed-schedule change passed its schedule/bounds test and a complete `make -C VXRuntime check` execution,
including the D3D12 query test on that execution. The intermittent timestamp issue is not considered fixed.

### Early-exit follow-up and diagnostic startup repair

`20261019T170418Z-elite-keyboard-diagnostic` was refused (`user_not_playing`) and measured nothing. The coordinator
reported its shell command line contained the `pgrep` pattern; subsequent process checks were issued separately.
The next run, `20261019T170427Z-elite-keyboard-diagnostic`, exited early (`o1`) without a Scene connection (`o2`,
`d3=false`), with an unhandled access violation (`o14`, `o18`) at `main_image+0x48c6442`. No metadata/render/wait
samples were taken (`o20`–`o22`); the first was scheduled at 90 seconds. Read-only static analysis identifies the
faulting instruction as reading ProcessParameters.Flags after GS:0x60 → PEB → PEB+0x20. The log has no register
values to distinguish a null pointer from corruption. The final unknown stack entry is an unmapped address, not a
game RVA. Network call paths also differed from the previous run; this does not establish network causality.

The delayed schedule exposed a validator bug: it still required exactly 30/40/50 seconds. It now validates the
bounded shifted three-sample schedule, including a stopped run with zero samples, while retaining artifact hashes,
counts and schema checks. Revalidation of the original result passes without rewriting its sealed files or its
recorded historical validation message.

Investigation also found a diagnostic startup side effect. `vx_waitdiag_enable` used Windows last-error accessors,
which attach a TEB to the native UI thread and replace Darwin TSD slot 12 (GS:0x60) with the shared PEB before guest
startup. A standalone native probe on this Mac demonstrated that `localtime()` uses this pointer as its buffer and
changes the simulated PEB's ProcessParameters pointer; the `localtime_r()` control leaves it intact. The probe
restores the original slot before cleanup and never follows the corrupted pointer. This proves a mechanism, not
that ED's failing run executed `localtime()` there.

Enable now preserves only errno: its native-only body never changes Windows last-error, so it does not need to
attach a TEB. A regression requiring zero Windows error-accessor calls fails before and passes after this change.
Separately, the metadata server now detaches any TEB its snapshot helpers created before its native thread exits,
following the runtime's existing GS/TSD cleanup contract. This fixes an existing render-snapshot lifetime gap as
well as the new wait path; it cannot explain an early crash before any snapshot. A live PE fixture consumes all
request budgets and checks the guest PEB and normal completion after the diagnostic thread exits.

No game compatibility fix or crash causality is established yet. The same delayed harness command should be
measured again with these diagnostic repairs before attributing a rendering or startup failure to Windows APIs.

Validation of these repairs passed the native probe, nine protocol/validation tests, four live/core integration
tests (including diagnostic-thread exit), and a complete `make -C VXRuntime check`. The earlier intermediate
check encountered the known D3D12 zero timestamp; the final complete pass does not establish that flake is fixed.

### R32 typeless texture descriptor failure

`20261019T171403Z-elite-keyboard-diagnostic` connected after 60.3 seconds (`o2`, `d3`) but exited before completing
the scenario (`o1=true`, `d4=false`). No Windows unhandled exception or missing-call termination was recorded
(`o14=0`, `o16={}`, `o18=null`). Its final capture remains the SteamVR waiting background; the automatic `draws`
label is not evidence of ED scene rendering. The selected log contains a native Metal texture-descriptor assertion
at lines 341–343 rejecting `MTLPixelFormatR32Float` with texture-atomic usage. Native unprefixed stderr does not
independently identify its emitting process; a separate fixture reproduces this invalid runtime descriptor.

The runtime mapped DXGI R32_TYPELESS to a float Metal backing while unconditionally adding atomic usage for its UAV
binding. R32 typeless UAV backing is now R32Uint, with float/signed/unsigned views preserving the underlying bits;
atomic usage is requested only when the actual backing format is R32Uint or R32Sint. Typed R32_FLOAT resources
remain float. Render-target views and shared-resource reopening preserve the same format choice. This targets the
invalid descriptor rather than changing shader or authentication behavior.

The harness now recognizes the fixed Metal texture-descriptor assertion header as a first problem and appends a
`native_metal_assertion` observation with log-line and artifact provenance. It excludes helper-prefixed and unrelated
assertions, retains earlier first problems, and does not count a Metal header as a Windows exception or a captured
signal. Offline re-parsing locates line 341 in this run; the original sealed result remains unchanged. Launch scripts
do not explicitly enable Metal validation, and historical inherited Metal validation settings were not recorded,
so its runtime validation mode is unknown. Longer startup and the newly observed failure do not by themselves
prove progress caused by the prior diagnostic repair. A game harness rerun is still required.

Focused validation with `MTL_DEBUG_LAYER=1` reproduced an exit-134 abort before the format fix. After the fix,
typed/typeless float UAV stores read back 1 and 0.2, typed/typeless UINT atomic histograms read back 32/32/32/32
(including shared-resource reopening), and a float RTV over the typeless UAV cleared to four 0.25 values. Native
R32Uint/R32Sint backing and compatible float/uint view creation passed; signed atomic execution was not tested.
The Metal usage getter was not used as proof of atomic capability: on this Mac even integer resources omitted
that flag from the returned usage value. Actual GPU operations and readback are the evidence.

The integrated change passed all six native-assertion parser tests, the explicit Metal-validation texture fixtures,
and a complete `make -C VXRuntime check`. Existing game `vr-check` and the next ED harness run are not yet complete.


### Late Presents and shader-creation failure

`20261019T172253Z-elite-keyboard-diagnostic` connects after 17.5 seconds (`o2`, `d3`), and records no recognized
native Metal descriptor assertion (`o23=null`). This run does not reproduce the previous R32Float assertion.
The final stream is still the SteamVR waiting background (`o12`, `o13`), not verified ED rendering.

At 90/100/110 seconds, `o21` records completed drawable Presents of 1200/2193/2639 and main command-buffer commits
of 1529/2523/2968. These advance, unlike the earlier 87-Present plateau. `o22` identifies thread 5 as the last
Present thread: its samples show completed WakeConditionVariable, active Present in next_drawable, then the known
10ms shared-mode condition wait. The subsequent completed Presents rule out treating the middle snapshot as a
permanent Present block. Counts aggregate all covered APIs, and overflow remains true. DXGI window presentation
is not OpenVR Submit; actual eye submission and the contents of the game window remain unobserved.

The unhandled exception (`o14=1`, `o18`, selected log line 324) occurs at 17:24:53.136 UTC, 119.666677 seconds after
the recorded start and approximately 333ms before the harness deadline. SteamVR also records the game exiting
before that deadline. The launcher waits for SteamVR cleanup after the game exits, so its continued presence can
produce `o1=false` and `d4=true`; neither establishes an exception-free game scenario. This is not established as
a harness teardown artifact.

Read-only analysis of the selected game image finds an explicit zero-address write at `main_image+0x34c9322`.
The recorded caller chain passes through shader creation's failed-HRESULT branch and a fatal error handler.
Immediately before the exception, log line 307 reports `shader not translated: buffer t33: only t0..t21 are
supported`. The runtime returns E_INVALIDARG for this translation failure. The fatal stack establishes a shader
creation failure, but without a stage or call identity it does not prove that this particular log entry is the
same failed call. It also does not explain the earlier waiting-background captures by itself.

The t33 diagnostic comes from a raw/structured SRV declaration. Simply raising the slot limit is unsafe: the
existing logical-to-Metal buffer layout, auxiliary size table, stage-internal slots and runtime SRV state have
separate bounds. The next bounded investigation records the failed shader stage and complete resource-declaration
summary before choosing a sparse binding remap or identifying a larger binding-budget limitation. No synchronization
semantics change or successful ED scene rendering is inferred from this run.


Shader translation failures now append one fixed metadata line: requested/bytecode stage, E_INVALIDARG, SHA-256,
and seven resource-class slot masks with unique-slot counts. The independent declaration scan continues beyond
where translation failed; malformed, unsupported or over-budget input is explicitly incomplete, never a complete
resource inventory. Input hashing/scanning is limited to 1MiB and 64 chunks. No shader payload, original source
text, game file, successful translation or error return is changed. Complete means declaration-scan completion,
not shader semantic validity; thread/call identity is not added by this diagnostic.

The synthetic fixture covers high slots and later declarations, all seven classes, duplicate slots, truncations,
malformed lengths, unsupported indexing, nonaligned input and 20,000 deterministic mutations. It also compares
translation results and source bytes before/after diagnosis and scans repository-owned shader fixtures. The
focused x86 test and native UBSan run passed; ASan runs did not complete successfully and are not claimed as
validation. A new game run is required to establish the failing stage/resource budget; no t33 compatibility fix
has been implemented yet.

The integrated failure diagnostic passed `make -C VXRuntime check`, including its new fixture. The next authorized
harness run should retain 90/100/110-second sampling and extend the scenario to 180 seconds so a near-120-second
game exit is observed away from the harness deadline. Real-game failure metadata remains pending.


### Sparse high-slot structured buffers

`20261019T173752Z-elite-keyboard-diagnostic` connects after 18.8 seconds (`o2`, `d3`) but exits early (`o1=true`,
`d4=false`) with the same fatal exception (`o14=1`, `o18`) at 17:39:41.208, about 109 seconds after start and well
before the 180-second deadline. This independently rules out that deadline as the normal cause of the earlier
late exception. The final stream remains the waiting background; `o23=null` does not reproduce the Metal assertion.

Selected log lines 307–308 record a failed vertex shader, SHA-256
`55976b9167c0527be6393365f0067cc34bf668697b5984b68004e154318d071d`, with a complete declaration scan: CB b0/b1 and
structured SRVs t33/t38, no raw/typed/UAV declarations. The unsupported-t33 diagnostic precedes the fatal exception
by 8ms, consistent with the previously identified shader-creation fatal branch. This is a small resource inventory
with high logical indices, not evidence of exceeding Metal's physical buffer budget.

Raw/structured SRVs now have a per-shader logical-to-physical map over t0..t127. The map reserves declared constant
buffers, the fixed UAV range and the auxiliary buffer. Vertex shaders also reserve all vertex-input slots and their
kernel-variant internal slots; geometry shaders reserve their internal slots. SRVs use available high slots and then
unused constant-buffer slots. A shader exceeding the remaining physical budget still fails explicitly. Both vertex
variants share their map. This does not add argument buffers or extend high-slot typed texture binding.

Runtime SRV ownership, set/get, unbinding, resource-hazard and geometry dependency scans cover all 128 logical slots.
Bindings use the shader's map. SRV byte sizes and CB sizes occupy separate auxiliary-table regions, and UAV-counter
addresses follow the new layout. Buffer views include the underlying allocation offset and clamp their size to the
view and resource bounds. Existing ClearState only clears render targets; its broader state-reset gap is unchanged.
Game validation of this binding change is still pending; no ED scene rendering or Submit success is claimed.


Focused regression on the pre-remap runtime fails CreateComputeShader with E_INVALIDARG and the t33 diagnostic.
On the remapped runtime with `MTL_DEBUG_LAYER=1`, the new fixture reports zero failures: CS t33/t64/t127 checks
view offsets, sizes, bounds and a dynamic b9 constant-buffer access (13 expected values); VS b0/b1 plus t33/t38
runs with real vertex input at VB8; PS raw t127 and GS structured t127 produce expected pixels 0xff405040 and
0xff405060. All four stages pass high-slot retain/get/replace/explicit-unbind reference-count checks. Dedicated
ClearState and context-destruction reference-count assertions are not included.

The host fixture checks all 128 logical entries, unique physical bindings and stage reservations, and rejects a
15-SRV vertex shader with a physical-budget error. This deliberately negative fixture is excluded only from the
success-only dxbcscan list and tested for rejection in high-srv-check. Repository-owned HLSL/DXBC/headers are
included and reproducible with the existing d3dcompiler_43.dll; no game shader was copied into the repository.
The existing failure-diagnostic fixture now accepts sparse high slots and separately checks an exhausted-budget
failure, preserving its before/after diagnostic behavior checks.

The integrated remap passed a complete `make -C VXRuntime check`, including the new Metal-validation GPU fixture,
physical-budget rejection and existing shader/draw/compute tests. The known D3D12 timestamp exception was not
needed in this pass. The original 173752Z result validates unchanged. The next ED harness run remains pending.


### Texture UAV clear follow-up

`20261019T174838Z-elite-keyboard-diagnostic` connects after 15.8 seconds (`o2`, `d3`) and records no unhandled
exception, first problem or native Metal assertion (`o14=0`, `o18=null`, `o23=null`). The previous t33 translation
failure is absent from its selected log. This run does not reproduce that fatal shader-creation failure, but its
final stream remains the SteamVR waiting background (`o12`, `o13`); the automatic controls label is still not input
or ED scene evidence.

The selected ED PID40907 responds to the three diagnostic samples (`o20`–`o22`), last at approximately 110.006
seconds. Completed Presents advance 1480/2525/3111 at 90/100/110 seconds, main commits 1808/2854/3441 and completed
window callbacks 174/194/214. Thread 5, the last Present thread, alternates condition wait / next_drawable / a new
condition wait; there is no evidence of one permanent wait across those samples. Wait snapshots still overflow.

The last selected-game log line reports unimplemented texture ClearUnorderedAccessView at 17:50:28.524 UTC,
110.358815 seconds after result.started. Diagnostic elapsed uses a separately captured monotonic origin, so the
warning cannot be precisely ordered against the last snapshot from these values alone. No later sequence of
snapshots establishes a stall after this warning. The run's vrserver log has Scene connection records for PID40907
but no exit/disconnect record; its near-end chaperone activity is not an ED heartbeat. Thus `o1=false`/`d4=true`
only establish launcher presence, and ED's own lifetime to 180 seconds remains unknown.

The next bounded follow-up moves the three samples to 110/120/130 seconds (`--diagnostic-delay 80`) while keeping
the 180-second scenario, to observe activity after the current clear boundary. A separate late sample schedule
would be needed to establish near-end responses. OpenVR Submit, eye-image contents and compositor acceptance of ED
frames remain unobserved; neither DXGI Presents nor encoder/stream frame rates resolve those questions.


Texture UAV Uint/Float clears now pack texels using the UAV view's format and encode ordered Metal buffer-to-texture
copies into the selected mip and array/depth range. Uint preserves each channel's low bits; Float converts FLOAT,
UNORM and SNORM views, following Microsoft's
[Uint](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-clearunorderedaccessviewuint)
and [Float](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-clearunorderedaccessviewfloat)
contracts. R32 typeless integer backing is handled through view-format bytes, without requiring RTV usage.
A single immutable upload plane is reused for all selected slices; deferred RTV clears are emitted first and the
resource is marked GPU-written. 3D UAV FirstWSlice/WSize and GetDesc retain their requested range. Unsupported
formats, invalid ranges and allocation failure are logged; existing buffer-clear behavior is unchanged.

The new fixture reports 20 failures on the prior implementation and zero on the final implementation with Metal
validation enabled. Twelve 2D cases cover integer/float bit patterns, four channels, normalized conversion/clamping,
half and packed formats, typeless FLOAT/UINT views, three mips and three array slices, untouched subresources,
copy-before/after and repeated clears. Pending RTV clear followed by UAV clear and copy also passes. Additional
cases cover 1D array mip0 and partial 3D depth at mip1, WSize=-1 and GetDesc; the atomic texture regression passes.
The existing 1D texture allocator creates only one mip, so 1D mip1 remains unsupported and its existing out-of-range
Map path can assert in Metal. This broader allocation/Map issue is not repaired here. Compression, depth, sRGB,
typeless UAV view formats and other formats outside the clear table remain unsupported. Game validation of the
clear implementation is pending; removing an unimplemented call alone will not prove ED eye-image submission.


Integration ran `make -C VXRuntime check` twice; each stopped only at the user-waived D3D12 second-timestamp-zero
failure. The new clear fixture, atomic/high-SRV fixtures, 21 shader compilations and draw/compute regressions passed.
Every original Makefile check after D3D12 was then run unchanged and passed. This is not a fully green check run,
and the D3D12 flake remains unresolved. The original 174838Z result validates without modifying sealed artifacts.


### Clear upload cost investigation

`20261019T180141Z-elite-keyboard-diagnostic` connects after 18.2 seconds (`o2`, `d3`) and does not record the texture
clear warning, unhandled exception, first problem or native Metal assertion (`o14=0`, `o18=null`, `o23=null`). The
final capture remains the SteamVR waiting environment. Stream/capture rates fall to 8.9/9.9 fps (`o4`, `o6`), with
350.7ms p95 stream interval (`o7`) and GPU utilization 12 (`o10`). These are not ED eye-frame rates.

At 110/120/130 seconds, completed Presents are 125/221/467 and main commits 125/230/784 (`o21`); process CPU time
is 91.402074/100.556393/113.076883 seconds. Work continues. The last Present thread 5 (`o22`) last completed
LeaveCriticalSection at the first snapshot and is in newly entered condition waits at the later two, not a sampled
texture clear or proven continuous GPU wait. Overflow remains true. The earlier run's 90/100/110-second samples
and different startup progression do not establish matched-stage performance causality.

The texture clear implementation contains no explicit CPU/GPU wait, but constructs and fills a new upload plane
per call. Metal allocation or downstream GPU work may still cost time. No existing diagnostic counts clear calls,
clear wall time or GPU submitted/completed backlog, so allocation overhead, changed work after a correct clear,
and host load remain competing explanations. The separate buffer-clear path can explicitly wait for nonuniform
bytes; that behavior is not evidence of a wait in texture clear.

A semantics-preserving optimization caches immutable upload tiles per context, bounded to eight entries and
32MiB, with tiles at most 8MiB. Repeated hits avoid allocation and CPU plane construction; first use and cache
misses still allocate. Retaining command buffers preserve evicted data for in-flight GPU work. Large planes use
row tiles without overwriting published data. Existing `VXRT_STATS=1` enables fixed-count summaries at clear
calls 1/16/256/4096/65536 plus normal teardown; default operation emits no new cache logs.

Performance validation must compare the correct pre-cache implementation (5de5ab7) with the optimized implementation,
using identical settings and sampling windows, preferably ABBA. The older unsupported/no-op clear is not an equivalent
performance baseline. Game performance and scene submission remain pending measurement; cache correctness does not
prove that clear overhead caused this run's slowdown.


With Metal validation enabled, the cache fixture preserves all prior clear checks and passes queued A/B/A pattern
changes, entry eviction, 8MiB tiles with a final partial row and byte-budget eviction before readback. Destroying and
recreating the device/context starts a fresh cache. The measured first context reports 57 calls, 15 hits, 42 builds,
67,134,208 total built bytes and 38 evictions, with resident cache bytes at 33,554,432. The second begins with one
build/no hits and finishes with four calls/two hits/two builds. Default logging emits no new cache summaries.
The 32MiB bound covers retained cache entries, not evicted buffers still retained by in-flight Metal commands.
GPU correctness and cache reuse are verified; no game-performance improvement is established by these fixture counts.

The integrated cache change passed a complete `make -C VXRuntime check` without needing the D3D12 timestamp
exception. The original 180141Z result validates unchanged. A matched harness A/B with `VXRT_STATS=1` remains
required before claiming a performance benefit.


## End-of-day handoff — 2026-10-19

All run IDs below have suffix `-elite-keyboard-diagnostic`. These are diagnostic results, not playable milestones.

| Run UTC | Measured result and subsequent work |
|---|---|
| 20261019T165747Z | o21 Presents 1/2/108 at 30/40/50s; o22 caller/wait snapshots did not establish a deadlock. Added delayed sampling (d6cd6e1). |
| 20261019T170427Z | Early PEB/ProcessParameters access violation before any snapshots (o1/o14/o18). Repaired diagnostic startup TEB attachment, server cleanup and shifted-schedule validation (1d5b325); native localtime corruption mechanism was reproduced, game causality not proven. |
| 20261019T171403Z | Selected log contains R32Float atomic-usage Metal assertion; no Windows exception. Corrected R32 typeless UAV backing/views/shared reopening and added assertion classification (58f35e3). |
| 20261019T172253Z | Presents 1200/2193/2639 at 90/100/110s (o21); shader-creation fatal stack before the 120s deadline (o14/o18), with unsupported buffer t33 in the log. Added bounded failure resource summaries (a8c9747). |
| 20261019T173752Z | Vertex shader declares b0/b1 and structured t33/t38; fatal exception follows its failure by 8ms (log307–308, o14/o18), well before the 180s deadline. Implemented stage-safe sparse SRV mapping and auxiliary table separation (13b4fcb). |
| 20261019T174838Z | No t33 failure or unhandled exception; Presents 1480/2525/3111 (o21). Last log warns texture UAV clear is missing. Implemented view-format/range-aware ordered clears (5de5ab7); no post-warning stall was established. |
| 20261019T180141Z | Clear warning absent; capture/stream 8.9/9.9fps and interval p95 350.7ms (o4/o6/o7), Presents 125/221/467 (o21), still background. Added bounded immutable upload cache (222e4d0); slowdown causality remains unknown. |
| 20261019T181052Z | Single cache-enabled B run: capture/stream 58.1/58.0fps, p95 20.0ms (o4/o6/o7); still background, 11 Metal shader compilation failures, and Presents plateau at 1401 (o21). No ABBA or min-precision fix followed. |

The final run connects after 18.3 seconds (`o2`, `d3=true`) and reports `o14=0`, `o18=null`, `o23=null` and
`o1=false`/`d4=true`. These null error classifications do not mean its shaders compiled. Selected log lines
378–448 contain 11 `translated shader ... did not compile` records, with scalar half/ushort casts of vector
expressions. The earliest is at 18:12:09.822 UTC. At 110.006/120.007/130.003s, `o21` has entered/completed/success
Presents 1401 throughout and main commits 2231 throughout; CPU increases 132.271849 → 142.471998 → 152.555886s
and completed window callbacks 401 → 421 → 441. This is a presentation plateau with other process activity,
not proof that the entire game has stopped or that the shader errors caused the plateau.

Log line 348 records only the first cache summary: one call, no hits, one one-byte build, no evictions and one byte
resident. It does not establish a high cache-hit rate or explain the recovered stream rate. The coordinator reported
load around 6–8; there was no matched A/B. The final capture remains the SteamVR waiting environment (`o12=11.5`,
`o13=0.241`), and automatic controls/draws are not ED rendering evidence. No real OpenVR Submit is observed.

Outstanding verification and limits:

- Existing 1D texture creation forces one mip; its out-of-range Map can assert. Multi-mip 1D behavior is not fixed.
- ClearState still only clears render targets; full stage-state reset is not implemented or verified.
- R32Sint descriptor/view creation was tested, but signed atomic execution was not.
- Other-game `vr-check`, ARM64 full-suite and real AVP/input playtests were not completed for today's changes.
- Harness `o1`/`d4` track the launcher, which can outlive ED while cleaning up SteamVR. They do not prove ED's
  own lifetime or an exception-free scenario; diagnostic responses establish only their sampled times.
- The D3D12 zero second-timestamp flake remains user-waived and unresolved. The texture-clear check hit it twice
  with the remaining original checks run separately; the final cache commit passed a full check without it.
- Authentication, official launcher handoff, physical keyboard/mouse/controllers, scene correctness and playable
  status remain unverified. Stop on explicit anti-cheat evidence; no authentication/ownership bypass was attempted.

The final cache implementation and its synthetic GPU fixtures are verified as described above. Today's game
results do not justify an ED draws/runs/playable claim. Work is paused; helpers are closed.


## Half-conversion emitter follow-up — 2026-10-20

The failing expressions printed in `20261019T181052Z-elite-keyboard-diagnostic` selected log lines 378–448 match
exactly the F32TOF16 and F16TOF32 emitter templates: selected source lanes can form float2/float3 or uint2 vectors,
while half/ushort casts were hardcoded as scalar. This establishes the emitter path from existing log text without
copying game shader bytecode or assuming minimum-precision operand flags caused it.

The conversion helper now carries the destination's selected component count through halfN, ushortN and the
uintN/floatN result. It reuses the existing source swizzle/mask handling and single assignment, retaining source/
destination alias behavior. Encoding still puts one half bit pattern into each 32-bit lane with zero-extension;
decoding still ignores the upper 16 bits of each lane. Scalar conversion and the existing Metal numeric-conversion
path are unchanged. This is a width correction for two opcodes, not a new implementation of min16precision flags.

Independent synthetic DXBC tests exercise both directions through Metal compilation, dispatch and GPU readback.
The old translator reproduces float2/3/4-to-half and uint2/3/4-to-ushort compiler errors. The repaired translator
passes 3600 output words per direction (7200 total): every nonempty destination mask, vector widths 1–4, swizzles,
select-one/immediate broadcast, source/destination aliasing, untouched lanes, output upper-bit clearing and input
upper-bit discard. Inputs include signed zero, exact normal/half-subnormal values, infinities and NaN classification.
The oracle uses safe math and exactly representable values; it does not establish full rounding or production
fast-math behavior, nor implement minimum-precision operand flags. No game bytecode is part of the fixture.

Alyx menu/menu@3k/chapter@3k, The Lab title and Earth start cross-game checks are **not checked: not installed**,
as confirmed by the coordinator in both default and ED-isolated harness environments. No installation/download
is authorized and missing games are not passes. Existing 21 shader compile checks and the new synthetic GPU
fixtures provide translator regressions, but do not replace those game checks. A new ED harness result is still
required; Present plateau, actual OpenVR Submit and ED scene content remain unverified follow-ups.


The integrated change passed a complete `make -C VXRuntime check`, including all 21 existing shader compilations
and the new 7200-word GPU conversion fixture. No D3D12 exception was needed for that final pass. The first full
attempt stopped at the existing CPU-diagnostic test's first 0.2-second snapshot being unavailable (the later two
were valid); the original CPU target passed independently, then the entire suite passed unchanged. This transient
failure is disclosed, not waived or claimed fixed. Profile validation reports five valid profiles and no problems;
the original 181052Z sealed result validates unchanged. ED and cross-game harness conclusions remain pending.


### Shader follow-up run and OpenVR evidence boundary

`20261020T094016Z-elite-keyboard-diagnostic` after 25cb233 records no `did not compile` lines, compared with 11 in
181052Z. It connects after 17.4 seconds (`o2`, `d3`), records `o14=0`, `o18=null`, `o23=null`, and has completed
Presents 3014/3612/4209 at 110/120/130 seconds (`o21`), with main commits 17504/20395/23285. The previous plateau
is not reproduced in this run. Capture/stream rates 60.0/59.7 (`o4`, `o6`) and GPU utilization 35 (`o10`) do not
identify ED eye frames. The captured output remains the waiting background (`o12=10.7`, `o13=0.226`). Continuous
DXGI presentation alone does not establish the game window's image content or that nothing was submitted to VR.

The selected game PID43407 identifies itself as VRApplication_Scene in its log lines124/126 and opens the
compositor pipe at lines224–228. Sealed vrserver lines223–234 identify the same PID/application and an existing
HMD; HMD bindings are loaded later. The currently existing overlay vrcompositor log, not sealed as a run
observation, also records the external connection for PID43407 at local10:40:36.711. It has no per-eye Submit,
texture-receive, returned-error or resource-import record for ED. Historical previous logs were excluded. This
supports initialization/connection, not whether IVRCompositor::Submit was called or accepted.

Submit is a real PE-to-PE virtual call and bypasses the runtime's builtin-export tracing. OpenVR/vrclient are
loaded from real DLLs rather than a builtin implementation; their exact final mapped file identities were not
recorded with loader trace in this run. The driver's virtual-display Present consumes compositor output, not the
game's eye Submit. A log in runtime shared-resource create/open/copy paths could supply indirect resource-flow
evidence but would not by itself reveal eye, OpenVR texture type or Submit result. No Valve vtable/proxy patch is
an acceptable substitute for transparent evidence.

`o20` contains only one visible/enabled/nonminimized window for PID43407 at each sample, no text or contents.
Arguments remain empty; the harness VR precondition merely rejects known VR-disabling arguments, so it is not
proof of the game's selected mode. There is no evidence yet to add `-vr` or modify game configuration. Window
text/Command Clips content/OCR was not read or newly collected; the coordinator requires approval before adding
text reading. A bounded window-content observation is a possible next discriminating step, subject to that approval.
OpenVR Submit, game UI state, authentication and actual scene rendering remain unknown.

### User-first, bounded window snapshot

The user authorized one late DXGI back-buffer PNG to distinguish window content from the waiting stream.
`--present-snapshot-at 150` in a 180-second run opts in; default behavior captures nothing. At that elapsed time,
the private metadata socket supplies the selected main process PID, then receives one PID-matched arm request.
Only that selected non-child process can claim the request, once across swapchains; TEST Presents do not consume
it. A failed attempt is not retried. One readback can wait for GPU completion, so this run is not a performance
comparison. The existing Present-count environment capture is suppressed in controlled mode, including helpers.

The PNG is written exclusively to `runs/<run_id>/present_snapshot.png`, with parent/final symlinks rejected and
no overwrite. `runs/` is ignored by git. The harness seals only fixed PNG-header metadata, byte size and SHA-256;
it does not decode pixels, read text chunks, compute brightness or apply stream-frame metrics to this artifact.
The user must inspect it first: no agent may open, describe, OCR or analyze its pixels. No window text/Command
Clips content is added. Missing output is `no_png`, not evidence that Present was absent: unsupported formats,
encoding or write failure can also prevent an artifact. This change does not establish ED rendering or Submit.

The raw overlay `vrcompositor.txt` is also copied and sealed at the end of the run. It may include historical
sessions and has no automatic Submit-derived claim. Shared-texture tracing and a direct Submit observation remain
unimplemented. Synthetic runtime fixtures cover PID selection, child/target exclusion, no-arm/no-Present cases,
TEST handling, no retry across swapchains, path conflicts/symlinks and last-error preservation. Synthetic harness
fixtures cover one-shot scheduling, snapshot-only transport, status validation and header/hash sealing without
real game pixels. No real ED back-buffer snapshot has been collected by this implementation yet.

Validation of this instrumentation: `make -C VXRuntime check` passed in full, including 18 snapshot harness
checks and all 10 synthetic runtime cases; no D3D12 timestamp exception was needed on this attempt. Profile
schema validation passed (five profiles, zero problems). Cross-game VR checks remain not checked: those games
are not installed. The next authorized run adds `--present-snapshot-at 150` to the existing 180-second diagnostic
command; only the user inspects its new PNG. Snapshot overhead is not an ABBA performance measurement.

### Armed snapshot without an artifact: 101805Z

`20261020T101805Z-elite-keyboard-diagnostic` records one successful PID82395 handshake and arm at150.003s,
but no PNG at sealing (`o24`, `no_png`). Nobody inspected a game-window image because none was produced. This
run's completed Presents are144/260/336 at110/120/130s (`o21`), not the preceding run's3014/3612/4209. These
samples precede the arm and cannot establish subsequent Present activity. `o2=21.0`, `o14=0`, `o18=null` and
`o23=null` do not identify a capture failure; `o1=false`/`d4=true` still track the launcher. The raw compositor log
is now sealed (`o25`), without an inferred Submit claim.

The current output path is canonical, its ancestors are not symlinks, and the run directory has the expected
owner. Synthetic capture to a repository `runs/` path succeeds; unsupported-format and existing-file cases also
behave as expected. This does not prove the game's runtime permissions, format, GPU state or write outcome.
The original controlled capture discarded its return status, so missing output could not distinguish a request
never claimed from readback, encoding or file-write failure. No particular game-capture defect has been reproduced.

Controlled capture now reports at most five fixed metadata lines: claimed/readback/encode/write/done, with PID,
Metal format, dimensions and an allowlisted result category. It records no pixels, paths or arbitrary error text.
The harness seals a strict PID-filtered projection as an appended observation, checks ordering and metadata
consistency, and preserves incomplete/missing records as uncertainty. Capture semantics, one-shot limits and
user-first image restrictions remain unchanged. The next diagnostic uses `--diagnostic-delay 120` (150/160/170s)
with `--present-snapshot-at 150`, so there are activity snapshots after the arm. Neither these diagnostics nor
window capture directly observe IVRCompositor::Submit.

Validation: all 23 synthetic harness tests and all 11 synthetic runtime cases pass; full `make -C VXRuntime check`
passes without the timestamp waiver on this attempt. The original 101805Z result validates unchanged. Profile
schema check reports five valid profiles. GPU/allocation/write/fsync/close failures were not fault-injected;
their categories are code paths, not measured game findings. No game capture fix or successful new capture is
claimed before the next harness run. Other-game VR checks remain not checked: not installed.
