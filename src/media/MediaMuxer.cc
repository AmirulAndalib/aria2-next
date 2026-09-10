/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "MediaMuxer.h"
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
}
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>

namespace aria2 {
namespace media {
namespace {
void check(int result)
{
  if (result >= 0)
    return;
  char message[AV_ERROR_MAX_STRING_SIZE];
  av_strerror(result, message, sizeof(message));
  throw std::runtime_error(std::string("Media remux failed: ") + message);
}
struct Input {
  AVFormatContext* context = nullptr;
  AVIOContext* io = nullptr;
  AVPacket* packet = av_packet_alloc();
  std::vector<std::string> files;
  std::vector<int64_t> starts;
  std::vector<int> mapping;
  std::ifstream file;
  int64_t position = 0, size = 0;
  size_t index = 0;
  bool ready = false;
  std::string type;
  Control* control = nullptr;
  ~Input()
  {
    av_packet_free(&packet);
    avformat_close_input(&context);
    if (io) {
      av_freep(&io->buffer);
      avio_context_free(&io);
    }
  }
  static int read(void* opaque, uint8_t* data, int length) noexcept
  {
    auto& s = *static_cast<Input*>(opaque);
    if (s.control->cancel)
      return AVERROR_EXIT;
    try {
      while (s.index < s.files.size()) {
        if (!s.file.is_open()) {
          s.file.open(std::filesystem::u8path(s.files[s.index]),
                      std::ios::binary);
          if (!s.file)
            return AVERROR(EIO);
          s.file.seekg(s.position - s.starts[s.index]);
        }
        s.file.read(reinterpret_cast<char*>(data), length);
        const auto count = static_cast<int>(s.file.gcount());
        s.position += count;
        if (count > 0)
          return count;
        if (!s.file.eof())
          return AVERROR(EIO);
        s.file.close();
        s.file.clear();
        ++s.index;
      }
      return AVERROR_EOF;
    }
    catch (...) {
      return AVERROR(EIO);
    }
  }
  static int64_t seek(void* opaque, int64_t offset, int whence) noexcept
  {
    auto& s = *static_cast<Input*>(opaque);
    if (whence == AVSEEK_SIZE)
      return s.size;
    whence &= ~AVSEEK_FORCE;
    if (whence == SEEK_CUR)
      offset += s.position;
    else if (whence == SEEK_END)
      offset += s.size;
    else if (whence != SEEK_SET)
      return AVERROR(EINVAL);
    if (offset < 0 || offset > s.size)
      return AVERROR(EINVAL);
    s.position = offset;
    s.index = static_cast<size_t>(
        std::upper_bound(s.starts.begin(), s.starts.end(), offset) -
        s.starts.begin());
    if (s.index)
      --s.index;
    s.file.close();
    s.file.clear();
    return offset;
  }
  void open()
  {
    if (!packet)
      throw std::bad_alloc();
    for (const auto& path : files) {
      starts.push_back(size);
      size += std::filesystem::file_size(std::filesystem::u8path(path));
    }
    auto buffer = static_cast<unsigned char*>(av_malloc(65536));
    io = avio_alloc_context(buffer, 65536, 0, this, read, nullptr, seek);
    if (!io) {
      av_free(buffer);
      throw std::bad_alloc();
    }
    context = avformat_alloc_context();
    if (!context)
      throw std::bad_alloc();
    context->pb = io;
    context->flags |= AVFMT_FLAG_CUSTOM_IO;
    context->io_open = [](AVFormatContext*, AVIOContext**, const char*, int,
                          AVDictionary**) -> int { return AVERROR(EPERM); };
    context->interrupt_callback = {
        [](void* data) -> int { return static_cast<Control*>(data)->cancel; },
        control};
    check(avformat_open_input(&context, nullptr, nullptr, nullptr));
    check(avformat_find_stream_info(context, nullptr));
  }
  void next()
  {
    av_packet_unref(packet);
    for (;;) {
      auto result = av_read_frame(context, packet);
      if (result == AVERROR_EOF) {
        ready = false;
        return;
      }
      check(result);
      if (packet->stream_index >= 0 &&
          static_cast<size_t>(packet->stream_index) < mapping.size() &&
          mapping[packet->stream_index] >= 0) {
        ready = true;
        return;
      }
      av_packet_unref(packet);
    }
  }
};
struct Output {
  AVFormatContext* context = nullptr;
  ~Output()
  {
    if (context) {
      if (context->pb)
        avio_closep(&context->pb);
      avformat_free_context(context);
    }
  }
};
bool keep(AVMediaType type, bool video, bool audio, bool subtitle)
{
  return (type == AVMEDIA_TYPE_VIDEO && video) ||
         (type == AVMEDIA_TYPE_AUDIO && audio) ||
         (type == AVMEDIA_TYPE_SUBTITLE && subtitle);
}
void remux(std::vector<std::unique_ptr<Input>>& inputs, const std::string& path,
           const std::string& format, bool video, bool audio, bool subtitles,
           Control* control)
{
  Output out;
  check(avformat_alloc_output_context2(&out.context, nullptr,
                                       format == "mkv" ? "matroska" : "mp4",
                                       path.c_str()));
  if (!out.context)
    throw std::bad_alloc();
  out.context->avoid_negative_ts = AVFMT_AVOID_NEG_TS_MAKE_ZERO;
  const bool separateAudio =
      std::any_of(inputs.begin(), inputs.end(),
                  [](const auto& i) { return i->type == "audio"; });
  for (auto& input : inputs) {
    input->mapping.assign(input->context->nb_streams, -1);
    for (unsigned i = 0; i < input->context->nb_streams; ++i) {
      auto source = input->context->streams[i];
      auto type = source->codecpar->codec_type;
      if (!keep(type, video, audio, subtitles) ||
          (separateAudio &&
           (input->type == "video" || input->type == "muxed") &&
           type == AVMEDIA_TYPE_AUDIO))
        continue;
      if (avformat_query_codec(out.context->oformat, source->codecpar->codec_id,
                               FF_COMPLIANCE_NORMAL) == 0)
        throw std::runtime_error(
            "Selected codec is not supported by the output container; choose "
            "another media-format");
      auto stream = avformat_new_stream(out.context, nullptr);
      if (!stream)
        throw std::bad_alloc();
      input->mapping[i] = stream->index;
      check(avcodec_parameters_copy(stream->codecpar, source->codecpar));
      stream->codecpar->codec_tag = 0;
      stream->time_base = source->time_base;
      av_dict_copy(&stream->metadata, source->metadata, 0);
    }
  }
  if (!out.context->nb_streams)
    throw std::runtime_error("No selected media tracks can be saved");
  check(avio_open(&out.context->pb, path.c_str(), AVIO_FLAG_WRITE));
  check(avformat_write_header(out.context, nullptr));
  for (auto& input : inputs)
    input->next();
  for (;;) {
    if (control->cancel)
      throw std::runtime_error("Media remux interrupted");
    Input* next = nullptr;
    for (auto& input : inputs) {
      if (!input->ready)
        continue;
      if (!next ||
          av_compare_ts(
              input->packet->dts,
              input->context->streams[input->packet->stream_index]->time_base,
              next->packet->dts,
              next->context->streams[next->packet->stream_index]->time_base) <
              0)
        next = input.get();
    }
    if (!next)
      break;
    auto packet = next->packet;
    auto source = next->context->streams[packet->stream_index];
    packet->stream_index = next->mapping[packet->stream_index];
    av_packet_rescale_ts(packet, source->time_base,
                         out.context->streams[packet->stream_index]->time_base);
    packet->pos = -1;
    check(av_interleaved_write_frame(out.context, packet));
    next->next();
  }
  check(av_write_trailer(out.context));
  check(avio_closep(&out.context->pb));
}
std::string quoted(const std::string& value)
{
  std::string result = "'";
  for (char ch : value) {
    if (ch == '\'')
      result += "'\\''";
    else
      result += ch;
  }
  return result + "'";
}
} // namespace
void Muxer::write(const std::vector<Segment>& segments,
                  const std::string& output, const std::string& directory,
                  const std::string& format, bool video, bool audio,
                  bool subtitles, bool overwrite,
                  const std::shared_ptr<Control>& control)
{
  if (segments.empty())
    throw std::runtime_error("No complete media segments were received");
  std::map<int64_t, std::map<int, std::vector<Segment>>> periods;
  for (const auto& segment : segments)
    periods[segment.period][segment.track].push_back(segment);
  std::vector<std::string> parts;
  for (const auto& period : periods) {
    std::vector<std::unique_ptr<Input>> inputs;
    for (const auto& track : period.second) {
      auto input = std::make_unique<Input>();
      input->control = control.get();
      input->type = track.second.front().type;
      if (!track.second.front().init.empty())
        input->files.push_back(track.second.front().init);
      for (const auto& segment : track.second)
        input->files.push_back(segment.path);
      input->open();
      inputs.push_back(std::move(input));
    }
    auto path = (std::filesystem::u8path(directory) /
                 ("period-" + std::to_string(period.first) + "." + format))
                    .u8string();
    remux(inputs, path, format, video, audio, subtitles, control.get());
    parts.push_back(path);
  }
  // Native concat demuxing owns the timeline across discontinuities/periods.
  if (parts.size() > 1) {
    auto list =
        (std::filesystem::u8path(directory) / "recording.ffconcat").u8string();
    std::ofstream file(std::filesystem::u8path(list));
    file << "ffconcat version 1.0\n";
    for (const auto& part : parts)
      file << "file " << quoted(part) << "\n";
    file.close();
    if (!file)
      throw std::runtime_error("Cannot write media remux manifest");
    auto input = std::make_unique<Input>();
    input->control = control.get();
    AVDictionary* options = nullptr;
    av_dict_set(&options, "safe", "0", 0);
    auto result = avformat_open_input(&input->context, list.c_str(),
                                      av_find_input_format("concat"), &options);
    av_dict_free(&options);
    check(result);
    check(avformat_find_stream_info(input->context, nullptr));
    std::vector<std::unique_ptr<Input>> inputs;
    inputs.push_back(std::move(input));
    auto merged =
        (std::filesystem::u8path(directory) / ("merged." + format)).u8string();
    remux(inputs, merged, format, video, audio, subtitles, control.get());
    parts = {merged};
  }
  // Stage on the destination filesystem; only a fully finalized file is
  // published.
  const auto destination = std::filesystem::u8path(output);
  const auto temporary = std::filesystem::u8path(
      output + "." + std::filesystem::u8path(directory).filename().u8string() +
      ".media-partial");
  struct Cleanup {
    std::filesystem::path path;
    ~Cleanup()
    {
      std::error_code ignored;
      std::filesystem::remove(path, ignored);
    }
  } cleanup{temporary};
  std::filesystem::copy_file(std::filesystem::u8path(parts.front()), temporary,
                             std::filesystem::copy_options::overwrite_existing);
  if (control->cancel)
    throw std::runtime_error("Media finalization interrupted");
  if (!overwrite && std::filesystem::exists(destination))
    throw std::runtime_error(
        "Media output appeared during download; refusing to overwrite it");
  std::filesystem::rename(temporary, destination);
}
} // namespace media
} // namespace aria2
