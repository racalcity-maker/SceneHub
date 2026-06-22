# Audio Pipeline Refactor Plan

This plan tracks the audio refactor needed to remove intermittent pops/clicks
when starting background audio, switching background tracks, and starting MP3
effects over an active background track.

## Problem

Observed behavior:

- starting any background track can pop;
- switching background tracks reliably pops;
- starting an MP3 effect over background can pop, especially after the effect
  path has been idle for a while;
- quickly repeating an effect may not pop.

This points to code-level discontinuities, not just bad source files:

- I2S/output startup is coupled to writes instead of an explicit mixer-owned
  state transition;
- the mixer is not a guaranteed continuous clock/PCM owner across idle and
  priming boundaries;
- background WAV and MP3 effect readers can compete for SD access while the
  mixer is trying to keep the live output fed;
- current asset preparation validates headers/metadata but does not prepare
  the first PCM frames needed for pop-free playback;
- MP3 effects rely on mixer fade-in only; they do not have a predecoded start
  window before they become audible.

## External Baseline

Use the same shape used by mature embedded audio stacks:

- one output writer owns I2S;
- reader/decoder stages feed ring buffers or stream buffers;
- the output writer is fed continuously with mixed PCM or zeros;
- a new source is decoded and buffered before it is connected to the audible
  mix;
- idle-to-running output startup is a mixer-owned transition, not hidden inside
  every low-level write;
- I2S DMA is primed before enabling or exposing real PCM to the amplifier.

Relevant reference points:

- ESP-IDF I2S supports TX preload before channel start through
  `i2s_channel_preload_data()`.
- ESP-ADF audio pipelines separate input, decoder, ringbuffer, and I2S writer
  ownership instead of letting a track start directly block the output writer.

## Non-Negotiable Invariants

- Do not inject silence from `audio_player_output_write()` or generic
  `audio_player_output_enable()` into a live mix.
- The mixer task is the only normal owner that writes mixed PCM to I2S.
- `audio_player_output_write()` should become a low-level write primitive, not
  a lifecycle owner.
- Output lifecycle transitions are explicit: `idle`, `priming`, `running`,
  `draining`.
- While `running`, the output writer must keep writing frame-aligned PCM
  continuously. If sources are temporarily empty, it writes zeros through the
  mixer, not through hidden output-layer behavior.
- A source is not audible until its first PCM window is ready.
- A source that has reached decoder EOF must remain a mixer-owned source while
  any buffered audible tail is drained/faded; buffered tail PCM must not be
  read later as an anonymous inactive stream.
- Background switching must not stop the current background before the next
  background is opened, decoded, and primed.
- MP3 effect startup must not force cold SD/open/decode work into the moment
  the effect becomes audible.
- No dynamic allocation in mixer/output/write loops.
- Large audio buffers stay static/fixed-pool PSRAM; DMA buffers stay
  DMA-capable internal storage.
- Do not hold audio runtime locks while doing blocking SD reads, MP3 decode, or
  I2S writes.

## Policy Alignment

Memory policy:

- Mixer, output and write loops are `audio-critical`; they must not allocate,
  parse JSON, or call into storage/admin helpers.
- Any MP3/SFX first-window cache must be fixed-size or fixed-pool PSRAM with a
  documented byte budget before implementation.
- DMA-facing buffers must remain static DMA-capable internal storage.
- Track/source start may use only bounded static/fixed-pool state. If a new
  allocation class is proposed, update `../policies/MEMORY_ALLOCATION_POLICY.md` before
  accepting it.

Locking policy:

- The mixer task owns output lifecycle mutation.
- Reader/decoder tasks may block on SD/decode, but must not hold mixer/output
  locks while doing that work.
- Mixer/output locks must not be held while posting events, updating broad
  read models, calling HTTP/admin code, or waiting on another owner task.
- Audio callbacks from `event_bus` must remain adapter-only and enqueue work
  to the audio owner instead of doing decode/output work inline.
- No GM/session or command-executor locks may be held while calling
  `audio_player_*`.

HTTP/API policy:

- Audio warmup/priming must not run inside frequent runtime `GET` handlers.
- Scenario/profile asset scans may report metadata/header readiness, but must
  not imply PCM warmup unless a bounded warmup owner actually performed it.
- Any operator/admin command that starts, stops, primes or flushes audio remains
  a side-effecting `POST`.

Architecture policy:

- `audio_player` remains the audio service boundary. Do not move decode,
  mixer, I2S lifecycle or SD-read scheduling into `gm_core`,
  `command_executor`, or `scenehub_control`.
- `scenehub_config` may provide compile-time audio budgets/defaults, but must
  not become a runtime audio state owner.

## Current Tactical Changes Under Test

These are small mitigations, not the final architecture:

- `audio_player_mixer.c`: `receive_pcm()` should report the actual bytes read
  from the stream buffer, not the requested chunk size.
- `audio_player_mixer.c`: source fade-in is longer than the old 12 ms value.
- `audio_player_decode.c`: WAV decoder edge fades were removed so decoder
  stays a PCM conversion stage; source edge behavior belongs to mixer/source
  lifecycle.
- `audio_player_output.c` / `audio_player_mixer.c`: low-level writes no
  longer enable I2S implicitly. The mixer now owns the explicit
  `idle -> priming -> running` transition and preloads I2S DMA before enabling
  output on idle start.
- `audio_player_mixer.c`: decoder EOF now leaves any audible buffered tail
  under mixer source ownership. If the buffered tail is larger than the fade
  window, the mixer drains it normally and applies the bounded tail fade only
  when the remaining buffered PCM reaches the final fade window.
- `audio_player_mixer.c`: source-primed logging is now tied to the one-time
  transition from unprimed to primed, not to every mixer loop where buffer
  occupancy remains above the preroll threshold.
- `audio_player_mixer.c`: stream-buffer writes are now bounded by frame-aligned
  free space before calling `xStreamBufferSend()`. This prevents a muted
  priming source from filling the stream buffer and accepting a partial
  non-frame-aligned write that would shift all following PCM samples.
- `audio_player_mixer.c`: source fade-in is now signal-aware. If the first
  decoded PCM window is silence or near-silence, the fade-in budget is not
  consumed until a real signal appears. This avoids wasting the MP3 effect
  ramp on encoder delay/padding and then exposing the first audible transient
  without a ramp.

These changes do not solve the root MP3 effect and output lifecycle problem by
themselves.

## Phase 0 - Prove The Failure Mode

- Add targeted logs/counters for:
  - output state transitions;
  - first write after idle;
  - source primed/active transitions;
  - background/effect stream starvation;
  - first audible MP3 effect block before mixer fade-in;
  - slow SD reads;
  - slow I2S writes and I2S timeouts.
- Test matrix on the board:
  - cold background start after 20-30 seconds idle;
  - background switch;
  - MP3 effect over active background after 20-30 seconds effect idle;
  - repeated MP3 effect immediately after the first effect;
  - stop all, then background start;
  - pause/resume if still used by operators.
- Acceptance:
  - every audible pop has a nearby state/log explanation, or the plan is
    updated with the missing instrumentation.

## Phase 1 - Make Output Lifecycle Explicit

Goal: remove hidden output start behavior from generic writes.

- Introduce mixer-owned output states:
  - `IDLE`;
  - `PRIMING`;
  - `RUNNING`;
  - `DRAINING`.
- Move idle-to-running output startup out of `audio_player_output_write()`.
- Add a mixer-owned start path that can:
  - preload I2S DMA with zeros or the first mixed PCM frame;
  - enable I2S only after preload succeeds;
  - unmute `MAX98357A` only as part of an explicit idle-start transition.
- Keep normal live writes free of startup silence injection.
- Keep output running during source changes when any source is live or being
  crossfaded.
- Current status:
  - low-level write no longer owns output enable;
  - mixer performs explicit `PRIMING` before `RUNNING`;
  - I2S DMA is preloaded with bounded silence before idle-start enable;
  - normal stop paths fade sources through the mixer instead of resetting
    I2S/MAX98357A;
  - next cleanup is to make the start path preload first mixed PCM when
    available instead of silence.
- Acceptance:
  - no low-level write path can insert silence into an already-running mix;
  - idle background start does not pop;
  - output state logs are understandable and bounded.

## Phase 2 - Source Priming And Background Switch

Goal: a source becomes audible only after its first PCM window is ready.

- Add source states:
  - `EMPTY`;
  - `OPENING`;
  - `PRIMING`;
  - `ACTIVE`;
  - `FADING_OUT`;
  - `DONE`;
  - `ERROR`.
- Change `audio_player_mixer_start_stream()` semantics so it does not mean
  "audible now"; it should mean "accepting PCM for priming".
- Add an explicit "source primed" transition once minimum PCM is available.
- For background switch:
  - keep old background active;
  - open/decode/prime new background;
  - fade old out and new in only after the new source is ready;
  - if new source fails to prime, keep old background if it is still valid.
- Current status:
  - mixer has two fixed background stream slots plus one effect stream slot;
  - runtime starts the next background on the inactive slot and waits for its
    primed PCM window before fading/stopping the old background;
  - background switch routing now treats a live mixer background source as
    sufficient evidence that the current background is active; it does not
    rely only on the reader task handle;
  - the inactive slot is muted while priming; it becomes audible only after the
    old background fade-out reaches zero and the old slot is stopped;
  - natural decoder finish no longer turns buffered tail PCM into an inactive
    raw stream; the mixer applies a bounded tail fade for audible sources;
  - if the new background does not prime within the bounded timeout, the old
    background remains active;
  - this is fixed static PSRAM, not runtime heap allocation.
- Acceptance:
  - switching background does not create an output gap;
  - no background switch can stop the old track before the new one has a
    primed PCM window.

## Phase 3 - MP3 Effect Startup

Goal: MP3 effects do not disturb active background output.

- Split MP3 effect start into:
  - request accepted;
  - file opened;
  - first frames decoded;
  - source primed;
  - source active/audible.
- Decode enough initial MP3 PCM into the effect stream before setting it
  audible.
- Apply fade-in after the first decoded PCM exists, not before cold SD/decode
  work starts.
- Current status:
  - MP3 effects are buffered before they become readable by the mixer;
  - source fade-in now waits for a non-silent PCM window before consuming the
    fade budget, so encoder delay/padding does not eat the whole ramp;
  - the mixer emits bounded first-block and first-non-silent-block diagnostics
    per effect start so field tests can correlate remaining pops with source
    PCM or timing;
  - field logs showed identical first non-silent gong PCM on popping and
    non-popping runs; these diagnostics are now debug-level to avoid blocking
    the audio-critical mixer loop during normal INFO logging;
  - starting a new effect fades the current effect stream briefly before
    replacing it, avoiding a hard cut of buffered effect tail PCM.
- Add optional fixed-size PSRAM first-window cache for frequently used MP3 SFX.
  This should be a bounded cache, not general heap allocation.
- If field behavior still shows SD contention, add a small audio SD-read owner
  or scheduling rule so effect priming cannot starve background reads.
- Acceptance:
  - first MP3 effect after idle over active background does not pop;
  - repeated MP3 effects behave the same as the first one;
  - background starvation logs do not appear at MP3 effect start.

## Phase 4 - Asset Preparation Semantics

Goal: stop treating metadata validation as audio warmup.

- Rename or split readiness concepts:
  - metadata/header ready;
  - playable;
  - first PCM primed, if implemented for selected assets.
- Keep scenario/profile asset scans lightweight and bounded.
- Do not decode entire long background tracks during HTTP/profile selection.
- Optional: allow operator-triggered or scenario-start warmup for short SFX
  packs using fixed PSRAM limits.
- Acceptance:
  - UI/read-model wording does not imply that audio is PCM-warmed when only
    headers were checked;
  - warmup work cannot block frequent runtime HTTP endpoints.

## Phase 5 - Cleanup And Tests

- Add unit-level tests for mixer state transitions where possible.
- Add fake source tests for:
  - partial stream reads;
  - starvation;
  - background switch while old background is active;
  - effect prime before active.
- Keep hardware validation explicit in `docs/TESTING.md`.
- Update `README.md`, `../ARCHITECTURE.md`, and `../policies/MEMORY_ALLOCATION_POLICY.md`
  after the refactor lands.
- Acceptance:
  - no known pop/click reproduction remains in the field test matrix;
  - docs describe the implemented model, not this plan.

## Open Questions

- Should I2S remain enabled indefinitely while the audio service is alive, or
  only while the mixer is in `RUNNING`/`DRAINING`?
- How much PSRAM can be reserved for SFX first-window cache without hurting GM
  scenario/read-model memory budgets?
- Should short operator SFX be converted to WAV/PCM assets as a product policy,
  or should MP3 SFX remain first-class with predecode support?
- Should background MP3 support stay unsupported, or should the same source
  priming model allow it later?
