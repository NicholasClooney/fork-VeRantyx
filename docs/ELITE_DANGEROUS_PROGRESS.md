# Elite Dangerous support progress

Last updated: 2026-10-18 (UTC).

Goal: run the legally installed Windows game on VXRuntime with Apple Vision Pro and keyboard/mouse input.
Current status: **ED connects to SteamVR, but the desktop window is black and the captured VR image is still the
SteamVR waiting environment. It is not verified playable.** Authentication, game scene rendering, physical input
and real AVP playback remain unverified. The official launcher has not been implemented or bypassed.

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

## Next work

Locate a specific failure from the bounded CPU evidence before changing rendering behavior. The next investigation
needs caller/wait-object evidence rather than interpreting kernel-library samples as CPU percentages or a deadlock.
A transparent real OpenVR Submit observation point has not yet been established, so that remains
unknown rather than introducing a behavior-changing proxy. Only implement a further compatibility fix once the
evidence identifies a specific failure.

Work stopped for the agreed break before the 22:03 UTC deadline on 2026-10-18. Test processes and listeners were
cleaned up; no further run is scheduled. Resume from the evidence above, not from an assumed rendering or login fix.

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
