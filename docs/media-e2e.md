# Public media E2E validation

Validate the shipping engine against public developer streams over HTTPS. Use
the CLI and JSON-RPC contracts, fixed representations, complete VOD downloads,
and bounded live recordings. Discovery and website extraction are outside scope.

## Sources and coverage

| Source | Coverage | Duration | Approximate payload |
| --- | --- | --- | --- |
| [Mux Turntable](https://test-streams.mux.dev/pts_shift/master.m3u8) | HLS TS, AVC/AAC, shifted timestamps, CLI | 165 s | 5 MB |
| [Shaka Angel One HLS](https://storage.googleapis.com/shaka-demo-assets/angel-one-hls/hls.m3u8) | fMP4, separate audio, language selection, WebVTT | 60 s | 8 MB |
| [Apple Bip Bop](https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_adv_example_hevc/master.m3u8) | HLS byte ranges, initialization, paused process restart | 600 s | 20 MB |
| [Sintel TS AES-128](https://storage.googleapis.com/shaka-demo-assets/sintel-ts-aes-key-rotation/master.m3u8) | AES-128 key rotation | 888 s | 30 MB |
| [Sintel fMP4 AES-128](https://storage.googleapis.com/shaka-demo-assets/sintel-fmp4-aes/master.m3u8) | Encrypted initialization and fragments | 888 s | 30 MB |
| [Shaka Angel One DASH](https://storage.googleapis.com/shaka-demo-assets/angel-one/dash.mpd) | SegmentBase, AVC/AAC, VP9/Opus, audio-only | 60 s | 3–10 MB |
| [DASH-IF AV1](https://livesim2.dashif.org/vod/testpic_2s_av1/Manifest.mpd) | SegmentTemplate, AV1/AAC | 10 s | <1 MB |
| [Apple HEVC](https://devstreaming-cdn.apple.com/videos/streaming/examples/adv_dv_atmos/main.m3u8) / [AV1](https://devstreaming-cdn.apple.com/videos/streaming/examples/av1-sample/av1-sample.m3u8) | SDR video, native remuxing | 98 s each | 4 MB each |
| [Axinom clear multi-Period](https://media.axprod.net/TestVectors/v7-Clear/Manifest_MultiPeriod.mpd) | Complete Period transitions | 1,468 s | 95 MB |
| [Shaka live HLS](https://storage.googleapis.com/shaka-live-assets/player-source.m3u8) | Refresh, recovery, finish, AAC/Opus/FLAC | Record 20–600 s | 1–120 MB |
| [DASH-IF live DASH](https://livesim2.dashif.org/livesim2/testpic_2s/Manifest.mpd) | Number/Time addressing, rolling Periods, ISO WebVTT, recording controls | Record 30–600 s | 1–20 MB |

Select modest resolutions through `media-pause-after-probe` and the returned
track IDs. Do not rewrite manifests to manufacture a passing case. Public assets
may change; record the selected tracks and source responses with each run.

Apple's audio renditions additionally cover AC-3, E-AC-3, HE-AAC and HE-AAC v2
with restart and independent sample comparisons. Native local fixtures cover
MP3 and Vorbis. Supported output containers are MP4 and Matroska; no transcoding
is introduced to make an unsupported combination pass.

## Operation coverage

| Operation | Required evidence |
| --- | --- |
| Probe and select | Discover tracks, choose fixed IDs/languages, continue and verify the chosen content |
| Pause and continue | HLS and DASH; ordinary and force pause; counters remain unchanged while paused |
| Process recovery | Same GID after saved restart; hard termination only after new media has committed |
| Compound recovery | Encryption, subtitles, WebM, audio-only, and a VOD restart after a Period boundary |
| Live Periods and subtitles | Both Number and Time templates; periodic ISO WebVTT cues survive pause, restart and Period changes in Matroska |
| Live finish | Both protocols: active finish, paused finish, automatic duration limit, MP4 and Matroska |
| Removal | Both protocols, VOD/live, active/paused tasks, including force removal; no output or owned cache remains |
| Long recording | Ten minutes per protocol with repeated pause, restart, process termination and paused finish; DASH crosses rolling Periods |
| Source lifecycle | Local native producers finish naturally; expired windows and permanent loss fail explicitly |
| Request context | Origin credentials and Referer work; credentials do not reach a foreign origin |
| Availability | Native bounded handling of a briefly unavailable live segment; permanent loss cannot publish a partial success |

Use the case catalogue in `media/public.py` as the source of truth. Every planned
operation must actually run; finishing a download before its operation is a
failure. Live outputs must start near zero and contain continuous packet clocks.
Whole-resource controls use active transfer time, excluding discovery. A run
fails after 60 seconds without media or network progress on these known sources.

## Acceptance

1. Record the Git revision, dirty state, executable SHA-256 and tool versions.
2. Require successful RPC completion, the requested tracks, and matching output
   byte counts. Check media progress independently of unknown output length.
3. Inspect every output with ffprobe and decode every audio/video stream with
   FFmpeg. Compare selected VOD content with independently retrieved source
   tracks using decoded video hashes and audio sample hashes. Preserve every
   decoded video frame; normalize decoded audio timestamps before comparing the
   advertised sample window. Compare subtitle text and merged cue spans, allowing
   up to 50 ms for encoder priming and container rounding. Container hashes are
   artifact identities only.
4. Validate each track's actual timeline, including encoder delay and source
   offsets. Subtitle playlists can extend beyond the audio/video presentation.
   Live audio/video must share a recording window even when their playlists
   have different live edges. Cover both directions of startup skew locally in
   MP4 and Matroska; preserve decoder dependencies at the recording boundary.
   A live reference must cover the same recording window before hashes can be
   compared; otherwise validate segment continuity and the recorded timeline.
   DASH-IF's periodic live subtitles must cover the recording, including its tail;
   sparse subtitle packets do not participate in audio/video gap detection.
5. Pause a real download, save the session, restart the process, and complete the
   same GID without losing progress or duplicating content. Verify live pause and
   explicit finish separately from automatic recording limits.
6. Retain outputs, logs, source evidence and per-case results. Failures and tool
   limitations remain visible; skips never count as passes.

## Execution and repair loop

Use Python 3.11+, curl, FFmpeg and ffprobe as developer tools. The engine retains
its native GPAC/libcurl/FFmpeg architecture. Public validation supplements the
local integration suite under `tools/transfer_validation`: retain native test
servers, generated fixtures and fault injection for reproducible regressions.
Remove duplicated implementation and redundant assertions, not distinct protocol
or recovery coverage. Public validation requires no additional engine runtime.

Run `python3 tools/transfer_validation/media/public.py`; use `--case NAME` for a
focused rerun, `--suite soak` for the ten-minute recordings, or `--suite all` for
the complete catalogue. The executable is copied into the run directory so a Windows build
can continue without changing or locking the binary under test. Sintel's published
clear MP4 tracks provide the reference for both encrypted presentations; their
decoded video and audio have been verified to match. Download complete reference
files with curl before decoding to avoid unnecessary remote demuxer seeks.

Run short assets first, then ranges/encryption/Period transitions, then live
recording. On failure, reproduce the exact source and selected representation,
separate source/tool failures from engine failures, consult upstream interfaces,
and fix the responsible layer. Add a focused regression only when it protects
the root cause. Rebuild, rerun affected cases, and finish with the complete suite
and CTest after shared engine changes.

Record discovery, transfer and finalization time and retained payload bytes.
Investigate measured stalls or unnecessary work. Compare changes on the same
asset and representation; CDN variability prevents a universal throughput target.
Do not add speculative retries, protocol implementations, or dependency upgrades.

The suite is manual and network-dependent. CTest owns deterministic unit
regressions. Budget approximately 1 GB of traffic and 2 GB of disk space for an
initial run with source comparisons and recovery checks.

References: [Apple examples](https://developer.apple.com/streaming/examples/),
[Shaka assets](https://github.com/shaka-project/shaka-player/blob/main/demo/common/assets.js),
[Mux streams](https://test-streams.mux.dev/),
[DASH-IF livesim2](https://livesim2.dashif.org/),
[ffprobe](https://ffmpeg.org/ffprobe.html),
[FFmpeg framehash](https://ffmpeg.org/ffmpeg-formats.html#framehash).
HLS synchronization follows [RFC 8216](https://www.rfc-editor.org/rfc/rfc8216.html#section-6.3.2).
DASH timing and availability follow the
[DASH-IF timing model](https://dashif.org/Guidelines-TimingModel/).
