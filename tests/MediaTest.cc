extern "C" {
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/intreadwrite.h>
#include <libavutil/mathematics.h>
}

#include "media/MediaDownload.h"
#include <cstddef>
#include <cstdint>
#include <gpac/list.h>
#include <gpac/setup.h>
#include <gpac/tools.h>
#include <ios>
#include <memory>
#include <string>
#include <system_error>
#include <vector>
#include "a2doctest.h"
#include "media/MediaMuxer.h"
#include "media/MediaFiles.h"
#include "media/MediaStore.h"
#include "media/MediaTransport.h"

extern "C" {
#include <gpac/mpd.h>
#include <gpac/isomedia.h>
}
#include <sqlite3.h>
#include <array>
#include <filesystem>
#include <fstream>

namespace aria2 {
namespace {
struct MediaFixture {
  std::filesystem::path root;
  explicit MediaFixture(const std::string& name)
      : root(std::filesystem::absolute(A2_TEST_OUT_DIR) / name)
  {
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
  }
  ~MediaFixture()
  {
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }
  std::string write(const std::string& name, const std::string& body)
  {
    auto path = root / std::filesystem::u8path(name);
    std::ofstream file(path, std::ios::binary);
    file << body;
    file.close();
    REQUIRE(file.good());
    return path.u8string();
  }
  media::Segment subtitle(int index)
  {
    auto path = write(std::to_string(index) + ".vtt",
                      "WEBVTT\nX-TIMESTAMP-MAP=LOCAL:00:00:00.000,MPEGTS:" +
                          std::to_string(index * 180000) +
                          "\n\n00:00:00.000 --> 00:00:01.000\nCaption " +
                          std::to_string(index) + "\n\n");
    media::Segment segment;
    segment.track = "subtitle:en";
    segment.number = index;
    segment.start = index * 2000;
    segment.duration = 2000;
    segment.type = "subtitle";
    segment.path = path;
    segment.hls = true;
    segment.bytes = std::filesystem::file_size(std::filesystem::u8path(path));
    segment.digest = media::Transport::digest(path);
    return segment;
  }
};

std::vector<int64_t> timestamps(const std::string& path)
{
  AVFormatContext* context = nullptr;
  REQUIRE(avformat_open_input(&context, path.c_str(), nullptr, nullptr) >= 0);
  struct Close {
    AVFormatContext*& context;
    ~Close() { avformat_close_input(&context); }
  } close{context};
  REQUIRE(avformat_find_stream_info(context, nullptr) >= 0);
  std::unique_ptr<AVPacket, void (*)(AVPacket*)> packet(
      av_packet_alloc(), [](AVPacket* value) { av_packet_free(&value); });
  REQUIRE(packet);
  std::vector<int64_t> result;
  while (av_read_frame(context, packet.get()) >= 0) {
    result.push_back(av_rescale_q(
        packet->pts, context->streams[packet->stream_index]->time_base,
        AVRational{1, 1000}));
    av_packet_unref(packet.get());
  }
  return result;
}
} // namespace

TEST_CASE("Media preserves HLS subtitle timestamp maps")
{
  MediaFixture fixture("media-subtitle-timing");
  const auto output = (fixture.root / "output.mkv").u8string();
  auto control = std::make_shared<media::Control>();
  auto staging = media::Muxer::stage({fixture.subtitle(0), fixture.subtitle(1)},
                                     output, fixture.root.u8string(), "mkv",
                                     false, false, true, control);
  REQUIRE(!std::filesystem::exists(std::filesystem::u8path(output)));
  media::Muxer::publish(staging, output, false);
  REQUIRE(timestamps(output) == std::vector<int64_t>{0, 2000});
}

TEST_CASE("Media preserves ISO WebVTT cues and skips empty samples")
{
  MediaFixture fixture("media-iso-webvtt");
  const auto path = (fixture.root / "input.mp4").u8string();
  std::unique_ptr<GF_ISOFile, decltype(&gf_isom_delete)> file(
      gf_isom_open(path.c_str(), GF_ISOM_OPEN_WRITE, nullptr), gf_isom_delete);
  REQUIRE(file);
  const auto track = gf_isom_new_track(file.get(), 0, GF_ISOM_MEDIA_TEXT, 1000);
  REQUIRE(track != 0);
  REQUIRE(gf_isom_set_track_enabled(file.get(), track, GF_TRUE) == GF_OK);
  u32 description = 0;
  REQUIRE(gf_isom_new_webvtt_description(file.get(), track, nullptr, nullptr,
                                         &description, "WEBVTT\n") == GF_OK);
  const auto box = [](const char* type, const std::string& payload) {
    std::string value(4, '\0');
    AV_WB32(value.data(), static_cast<uint32_t>(payload.size() + 8));
    return value + std::string(type, 4) + payload;
  };
  const auto cues =
      box("vttc", box("iden", "first") + box("sttg", "align:start") +
                      box("payl", "Alpha")) +
      box("vttc", box("payl", "Beta"));
  const std::array<std::string, 3> samples{cues, box("vtte", ""),
                                           box("vttc", box("payl", "Gamma"))};
  for (size_t i = 0; i < samples.size(); ++i) {
    GF_ISOSample sample{};
    sample.data =
        const_cast<u8*>(reinterpret_cast<const u8*>(samples[i].data()));
    sample.dataLength = static_cast<u32>(samples[i].size());
    sample.DTS = i * 1000;
    sample.IsRAP = RAP;
    REQUIRE(gf_isom_add_sample(file.get(), track, description, &sample) ==
            GF_OK);
  }
  REQUIRE(gf_isom_set_last_sample_duration(file.get(), track, 1000) == GF_OK);
  REQUIRE(gf_isom_close(file.release()) == GF_OK);
  REQUIRE(timestamps(path) == std::vector<int64_t>{0, 1000, 2000});
  media::Segment segment;
  segment.track = "subtitle:en";
  segment.type = "subtitle";
  segment.path = path;
  segment.duration = 3000;
  segment.digest = media::Transport::digest(path);
  const auto output = (fixture.root / "output.mkv").u8string();
  const auto staging = media::Muxer::stage(
      {segment}, output, fixture.root.u8string(), "mkv", false, false, true,
      std::make_shared<media::Control>());
  media::Muxer::publish(staging, output, false);
  REQUIRE(timestamps(output) == std::vector<int64_t>{0, 0, 2000});
  AVFormatContext* context = nullptr;
  REQUIRE(avformat_open_input(&context, output.c_str(), nullptr, nullptr) >= 0);
  std::unique_ptr<AVFormatContext, void (*)(AVFormatContext*)> input(
      context, [](AVFormatContext* value) { avformat_close_input(&value); });
  std::unique_ptr<AVPacket, void (*)(AVPacket*)> packet(
      av_packet_alloc(), [](AVPacket* value) { av_packet_free(&value); });
  REQUIRE(packet);
  for (const auto* text : {"Alpha", "Beta", "Gamma"}) {
    REQUIRE(av_read_frame(context, packet.get()) >= 0);
    REQUIRE(std::string(reinterpret_cast<char*>(packet->data), packet->size) ==
            text);
    if (std::string(text) == "Alpha") {
      size_t length = 0;
      const auto id = av_packet_get_side_data(
          packet.get(), AV_PKT_DATA_WEBVTT_IDENTIFIER, &length);
      REQUIRE(id);
      REQUIRE(std::string(reinterpret_cast<const char*>(id), length) ==
              "first");
      const auto settings = av_packet_get_side_data(
          packet.get(), AV_PKT_DATA_WEBVTT_SETTINGS, &length);
      REQUIRE(settings);
      REQUIRE(std::string(reinterpret_cast<const char*>(settings), length) ==
              "align:start");
    }
    av_packet_unref(packet.get());
  }
}

TEST_CASE("Native DASH seeking respects a trimmed timeline and its end")
{
  GF_MPD_Period period{};
  GF_MPD_AdaptationSet set{};
  GF_MPD_Representation representation{};
  GF_MPD_SegmentTemplate segmentTemplate{};
  GF_MPD_SegmentTimeline timeline{};
  GF_MPD_SegmentTimelineEntry entry{};
  std::unique_ptr<GF_List, decltype(&gf_list_del)> entries(gf_list_new(),
                                                           gf_list_del);
  REQUIRE(entries);
  entry.start_time = 48000;
  entry.duration = 2000;
  entry.repeat_count = 5;
  REQUIRE(gf_list_add(entries.get(), &entry) == GF_OK);
  timeline.entries = entries.get();
  segmentTemplate.timescale = 1000;
  segmentTemplate.segment_timeline = &timeline;
  representation.segment_template = &segmentTemplate;
  u32 index = 0;
  Double start = 0, duration = 0;
  REQUIRE(gf_mpd_seek_in_period(54, MPD_SEEK_PREV, &period, &set,
                                &representation, &index, &start,
                                &duration) == GF_OK);
  REQUIRE(index == 3);
  REQUIRE(start == 54);
  REQUIRE(duration == 2);
  REQUIRE(gf_mpd_seek_in_period(64, MPD_SEEK_PREV, &period, &set,
                                &representation, &index, &start,
                                &duration) == GF_EOS);
  segmentTemplate.presentation_time_offset = 48000;
  REQUIRE(gf_mpd_seek_in_period(6, MPD_SEEK_PREV, &period, &set,
                                &representation, &index, &start,
                                &duration) == GF_OK);
  REQUIRE(index == 3);
  gf_list_reset(entries.get());
  u64 timestamp = 1, segmentDuration = 1;
  u32 scale = 0;
  REQUIRE(gf_mpd_get_segment_start_time_with_timescale(
              0, &period, &set, &representation, &timestamp, &segmentDuration,
              &scale) == GF_NOT_READY);
  REQUIRE(timestamp == 0);
  REQUIRE(segmentDuration == 0);
  REQUIRE(scale == 1000);
}

TEST_CASE("Media unwraps the HLS subtitle MPEG clock at 33 bits")
{
  MediaFixture fixture("media-subtitle-clock-wrap");
  std::vector<media::Segment> segments;
  for (int i = 0; i < 2; ++i) {
    auto segment = fixture.subtitle(i);
    const auto clock = i ? 90000 : (int64_t{1} << 33) - 90000;
    fixture.write(std::to_string(i) + ".vtt",
                  "WEBVTT\nX-TIMESTAMP-MAP=LOCAL:00:00:00.000,MPEGTS:" +
                      std::to_string(clock) +
                      "\n\n00:00:01.000 --> 00:00:02.000\nCaption\n\n");
    segment.digest = media::Transport::digest(segment.path);
    segments.push_back(segment);
  }
  const auto output = (fixture.root / "output.mkv").u8string();
  auto staging = media::Muxer::stage(segments, output, fixture.root.u8string(),
                                     "mkv", false, false, true,
                                     std::make_shared<media::Control>());
  media::Muxer::publish(staging, output, false);
  REQUIRE(timestamps(output) == std::vector<int64_t>{0, 2000});
}

TEST_CASE("Media applies presentation offsets before writing timestamps")
{
  MediaFixture fixture("media-presentation-offset");
  auto segment = fixture.subtitle(0);
  segment.hls = false;
  segment.timeOffset = 5000000;
  fixture.write("0.vtt",
                "WEBVTT\n\n00:00:05.000 --> 00:00:06.000\nCaption\n\n");
  segment.digest = media::Transport::digest(segment.path);
  const auto output = (fixture.root / "output.mkv").u8string();
  auto staging = media::Muxer::stage({segment}, output, fixture.root.u8string(),
                                     "mkv", false, false, true,
                                     std::make_shared<media::Control>());
  media::Muxer::publish(staging, output, false);
  REQUIRE(timestamps(output) == std::vector<int64_t>{0});
}

TEST_CASE("Media publishes long destination paths through native file APIs")
{
  MediaFixture fixture("media-long-destination");
  const auto directory =
      fixture.root / std::string(90, 'a') / std::string(90, 'b');
  const auto output = (directory / "output.mkv").u8string();
  std::filesystem::create_directories(media::nativePath(directory.u8string()));
  auto staging = media::Muxer::stage(
      {fixture.subtitle(0)}, output, fixture.root.u8string(), "mkv", false,
      false, true, std::make_shared<media::Control>());
  media::Muxer::publish(staging, output, false);
  REQUIRE(std::filesystem::file_size(media::nativePath(output)) > 0);
  std::filesystem::remove_all(
      media::nativePath(directory.parent_path().u8string()));
}

TEST_CASE("Media refuses damaged recovery data and protects existing output")
{
  MediaFixture fixture("media-publication-integrity");
  auto segment = fixture.subtitle(0);
  const auto output = fixture.write("existing.mkv", "existing output");
  fixture.write("0.vtt", "damaged");
  REQUIRE_THROWS(media::Muxer::stage({segment}, output, fixture.root.u8string(),
                                     "mkv", false, false, true,
                                     std::make_shared<media::Control>()));
  REQUIRE(std::filesystem::file_size(std::filesystem::u8path(output)) == 15);
  auto staging = fixture.write("staging", "replacement");
  REQUIRE_THROWS(media::Muxer::publish(staging, output, false));
  REQUIRE(std::filesystem::exists(std::filesystem::u8path(staging)));
  REQUIRE(std::filesystem::file_size(std::filesystem::u8path(output)) == 15);
  media::Muxer::publish(staging, output, true);
  REQUIRE(std::filesystem::file_size(std::filesystem::u8path(output)) == 11);
}

TEST_CASE("Media identity invalidation rolls back with its segment deletion")
{
  MediaFixture fixture("media-state-transaction");
  media::Store store(fixture.root.u8string(), "0123456789abcdef");
  REQUIRE(!store.identity("source"));
  REQUIRE(!store.manifest("playlist", "old"));
  store.commit(fixture.subtitle(0));
  sqlite3* db = nullptr;
  REQUIRE(sqlite3_open((fixture.root / "state.db").u8string().c_str(), &db) ==
          SQLITE_OK);
  struct Close {
    sqlite3* db;
    ~Close() { sqlite3_close(db); }
  } close{db};
  REQUIRE(
      sqlite3_exec(
          db,
          "CREATE TRIGGER reject_invalidation BEFORE DELETE ON media_segments "
          "BEGIN SELECT RAISE(ABORT,'injected failure'); END",
          nullptr, nullptr, nullptr) == SQLITE_OK);
  REQUIRE_THROWS(store.manifest("playlist", "new"));
  REQUIRE(store.segments().size() == 1);
  REQUIRE(sqlite3_exec(db, "DROP TRIGGER reject_invalidation", nullptr, nullptr,
                       nullptr) == SQLITE_OK);
  REQUIRE(store.manifest("playlist", "new"));
  REQUIRE(store.segments().empty());
}

TEST_CASE("Media retains publication evidence across restart and removes only "
          "staging")
{
  MediaFixture fixture("media-publication-restart");
  const std::string gid = "0123456789abcdef";
  const auto output = fixture.write("output.mkv", "published");
  const auto staging =
      fixture.write("output.mkv." + gid + ".media-partial", "staged");
  {
    media::Store store(fixture.root.u8string(), gid);
    store.identity("source");
    store.select(0, "video", "representation");
    REQUIRE_THROWS(store.select(0, "video", "different-representation"));
    store.preparePublication({output, staging, "digest", 9});
  }
  {
    media::Store store(fixture.root.u8string(), gid);
    auto publication = store.publication();
    REQUIRE(publication.has_value());
    REQUIRE(publication->output == output);
    REQUIRE(publication->staging == staging);
    REQUIRE(publication->bytes == 9);
    REQUIRE_THROWS(store.identity("changed-selection"));
    REQUIRE(!store.identity("source"));
  }
  media::Store::discard(fixture.root.u8string(), gid);
  REQUIRE(std::filesystem::exists(std::filesystem::u8path(output)));
  REQUIRE(!std::filesystem::exists(std::filesystem::u8path(staging)));
}
} // namespace aria2
