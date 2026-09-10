# Media downloads

Aria2 Next downloads HLS and MPEG-DASH presentations and records live streams.
GPAC owns manifest parsing, representation selection, segment addressing, and
playlist updates. libcurl owns HTTP(S), TLS, cookies, proxies, and transfers.
FFmpeg's libraries demux and remux the downloaded media without transcoding.
There are no child download tools, player, Python runtime, or .NET runtime in
the engine. Webpage extraction and browser resource discovery are not provided.

## CLI

```sh
aria2-next 'https://example.org/video/index.m3u8'
aria2-next --media=dash --media-format=mkv 'https://example.org/manifest'
aria2-next --media-record-time=3600 'https://example.org/live/index.m3u8'
```

| Option | Default | Meaning |
| --- | --- | --- |
| `media` | `auto` | `auto`, `file`, `hls`, or `dash`. Auto recognizes manifest URL suffixes and HTTP content types; explicit HLS/DASH also supports extensionless endpoints. File saves the resource unchanged. |
| `media-format` | `mp4` | Output container: `mp4` or `mkv`. |
| `media-video` | `best` | Highest bandwidth video source, `none`, or a `group:quality` ID. |
| `media-audio` | `best` | Audio source: `best`, `none`, a language, or a `group:quality` ID. |
| `media-subtitles` | `none` | Subtitle source: `none`, `best`, a language, or a `group:quality` ID. |
| `media-pause-after-probe` | `false` | Publish the available representations and pause before fetching payload segments. |
| `media-record-time` | `0` | Live media duration limit in seconds; zero records until stopped or the source ends. Stops at a complete segment boundary. |

Only representations supported by the native client can be selected. Multiplexed
HLS sources can contain both video and audio; packet filtering applies the
requested output track types. Download quality is fixed, not dynamically reduced
to follow network speed. DRM and unsupported encryption modes fail explicitly.
Standard HLS AES-128 uses OpenSSL's native cipher implementation. MP4 cannot
represent every subtitle/codec combination; use MKV when required. The engine
does not silently transcode or discard selected unsupported streams.

Initial track selection applies before fetching initialization or media packets.
HLS rendition languages remain distinct even when they share a group ID. AES-128
initialization follows the key active at `EXT-X-MAP`, independently of later media
keys. VOD keys are reused within the task; live keys are refreshed.
Packed AAC uses the transport timestamp parsed by FFmpeg from ID3 metadata.
Complete HLS fragments retain their final samples; rounded playlist durations
do not truncate audio. Discontinuity boundaries still delimit adjacent epochs.

DASH supports indexed MP4 and complete WebM representations. Declared MP4 index
ranges are fetched together and parsed by GPAC. Period boundaries limit retained
packets, including fragments that extend past the advertised presentation.

Track types use GPAC's codec registry rather than relying on a container MIME
type. Explicit track or language requests fail when unavailable. DASH
presentation offsets, HLS subtitle timestamp maps, initialization changes,
and discontinuity boundaries are preserved during remuxing. Codec or track
layout changes that cannot be combined without transcoding fail explicitly.
WebVTT text segments and ISO WebVTT (`wvtt`) tracks can be retained in Matroska.
GPAC parses boxed cues, including simultaneous cues and their identifiers and
settings; FFmpeg preserves their sample timing. Unsupported subtitle inputs fail
explicitly instead of disappearing from the output.

A source `checksum` is not applied to remuxed output. Media tasks reject that
combination; use `media=file` to save and verify the original resource instead.
Native demuxer diagnostics are routed through the engine's debug logger, while
terminal failures remain visible at error level.

Existing HTTP options configure authentication, request headers, timeouts,
proxying, certificate validation, and per-task rate limits. Browser-supplied
Cookie and Authorization headers retain their original origin boundary. Remote
manifests cannot request arbitrary local files or executable protocols.

## RPC

Use `aria2.addUri` and the normal task control methods. A media presentation
keeps one GID throughout discovery, selection, download/recording, and remuxing.
Segments are not exposed as separate tasks.
`changeUri` does not replace a media presentation; submit a new task for a new
source URI.

`tellStatus`, `tellActive`, `tellWaiting`, and stopped task results expose a
`media` object. Example during a finite download:

```json
{
  "status": "active",
  "totalLength": "0",
  "completedLength": "0",
  "media": {
    "state": "downloading",
    "protocol": "hls",
    "live": "false",
    "duration": "60000",
    "completedDuration": "20000",
    "downloadedLength": "4194304",
    "progress": "0.333333",
    "lengthKnown": "false",
    "error": "",
    "tracks": []
  }
}
```

Durations use milliseconds and integer counters use decimal strings. Media
progress is based on completed media duration. Output byte lengths remain
unknown until remuxing finishes: source segment bytes are not the same quantity
as the final container size. `media.downloadedLength` reports retained media
payload, independently of the standard network speed. Live tasks do not report
a fabricated total duration percentage. Frontends must use the media fields,
not interpret unknown output size as zero download progress.

HLS live progress measures the common audio/video recording window using native
media timestamps. Independently refreshed playlists may start at different
positions; their sequence numbers cannot synchronize different tracks. MP4 edit
lists retain decoder preroll outside the visible recording. Matroska starts at
the next usable video keyframe and trims preceding audio, so its recording
boundary can differ by a keyframe interval.
DASH live clocks are normalized to a zero-based output timeline.

Media phases are `waiting`, `probing`, `awaiting-selection`, `downloading`,
`recording`, `finalizing`, `paused`, `complete`, `error`, and `removed`. Standard
RPC status retains the ordinary task lifecycle. Only successful muxing and
publishing the output file produce a completed task.

To choose tracks, add with `media-pause-after-probe=true`, inspect `media.tracks`,
then use `changeOption` to set the selection and `media-pause-after-probe=false`
before `unpause`. Task options affecting selection or output change while paused.

`aria2.finishMedia(gid)` ends an active or paused live recording and finalizes
its completed media. Pause retains the task; remove cancels it and discards its
recovery state. Finishing and deleting are separate operations.

## Recovery and storage

State is created on demand under `state-dir/media/state.db`. Task-owned cache
files live under `state-dir/media/tasks/<gid>`. SQLite stores stable representation
identities, manifest identity, and committed segment positions. Completed network resources
are content-addressed and verified before reuse. Mutable live manifests and
resources are refreshed rather than permanently served from the resume cache.

Paused tasks restore their saved media progress without network activity.
Resume reopens the native client and reuses verified completed resources. Remuxing
is rebuilt from committed fragments; an old MP4 is never blindly appended to.
Changed presentation/selection identity invalidates incompatible checkpoints.
Live recovery starts within the available server window and checks segment
continuity against committed positions before accepting new content.
HLS checkpoint identity uses the manifest's media sequence, independent of
segment filenames. Resume seeks directly to that sequence instead of downloading
the preceding server window. DASH UTC synchronization uses the clock response's
receive time. Cancellation and terminal playlist refresh errors return
control to the engine without an unbounded native retry loop.
Live recording cannot recover media that has already left the server's window;
gaps and a source disappearing without a proper end signal are reported as errors.

DASH checkpoints use presentation time rather than a rolling playlist index.
Resume restores the MPD Period and each selected track's committed position.
Following Period starts close the preceding Period, and empty future timelines
wait for publication. Completed fragment durations come from their actual native
timeline entries, including updates after an initially empty Period. The native
client's bounded live availability policy handles a
briefly missing segment; exhausted delivery fails instead of skipping content.
Playlist waits honor native timing and wake on pause, removal or finish.

Remuxing writes directly to a staging file on the destination filesystem.
Files are synchronized before their publication record is committed. Recovery
verifies the staged or already-published file before completing an interrupted
publication. Selection and output format cannot change while publication is
pending. Completion/removal clears task-owned recovery data; cleanup failure
does not invalidate a successfully published file. Existing output is protected
unless overwriting was explicitly enabled.

Media state schema 4 stores stable DASH presentation positions and HLS media clocks. It
discards incompatible earlier checkpoints and task caches. Existing downloaded
output is kept.
Windows media files use native extended paths without changing system-wide
long-path settings. No adjacent `.aria2` control files are created.

## Validation

Run `tools/transfer_validation/run media` independently. The module uses local
Caddy and FFmpeg-generated fixtures and checks decoded media, track selection,
byte ranges, encryption, different presentation offsets, initialization changes,
subtitle timestamps, multi-period content, recording, and paused restart.
FFmpeg/ffprobe and OpenSSL executables are developer-only fixture/oracle dependencies.
No public streaming service is used by the local module or CTest. Run the separate
public suite with `python3 tools/transfer_validation/media/public.py`; see the
[public E2E plan](media-e2e.md) for sources and acceptance criteria.
