#include "a2doctest.h"
#include "media/MediaMuxer.h"
#include "media/MediaFiles.h"
#include "media/MediaStore.h"
#include "media/MediaTransport.h"

extern "C" {
#include <libavformat/avformat.h>
}
#include <sqlite3.h>
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
