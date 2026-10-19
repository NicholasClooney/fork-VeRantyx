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
