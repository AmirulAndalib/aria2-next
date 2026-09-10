/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "MediaMuxer.h"
#include "MediaFiles.h"
#include "MediaTransport.h"
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
#include <libavutil/intreadwrite.h>
#include <libavutil/parseutils.h>
}
#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string_view>
#include <stdexcept>
#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#endif

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
int64_t subtitleOffset(const std::string& path)
{
  std::ifstream input(nativePath(path));
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (line.empty())
      break;
    constexpr const char* prefix = "X-TIMESTAMP-MAP=";
    if (line.compare(0, std::char_traits<char>::length(prefix), prefix))
      continue;
    auto local = line.find("LOCAL:");
    auto mpeg = line.find("MPEGTS:");
    if (local == std::string::npos || mpeg == std::string::npos)
      throw std::runtime_error("Invalid HLS subtitle timestamp map");
    auto cue = line.substr(local + 6, line.find(',', local) - local - 6);
    auto clock = line.substr(mpeg + 7, line.find(',', mpeg) - mpeg - 7);
    int64_t localTime = 0, timestamp = 0;
    const auto parsed =
        std::from_chars(clock.data(), clock.data() + clock.size(), timestamp);
    if (av_parse_time(&localTime, cue.c_str(), 1) < 0 ||
        parsed.ec != std::errc() || parsed.ptr != clock.data() + clock.size() ||
        timestamp < 0 || timestamp >= (int64_t{1} << 33))
      throw std::runtime_error("Invalid HLS subtitle timestamp map");
    return av_rescale_q(timestamp, AVRational{1, 90000}, AV_TIME_BASE_Q) -
           localTime;
  }
  return 0;
}
bool sameCodec(const AVCodecParameters* a, const AVCodecParameters* b)
{
  return a->codec_type == b->codec_type && a->codec_id == b->codec_id &&
         a->width == b->width && a->height == b->height &&
         a->sample_rate == b->sample_rate &&
         !av_channel_layout_compare(&a->ch_layout, &b->ch_layout) &&
         a->extradata_size == b->extradata_size &&
         (!a->extradata_size ||
          !std::memcmp(a->extradata, b->extradata, a->extradata_size));
}
int64_t packedAudioClock(AVFormatContext* context)
{
  // FFmpeg parses ID3 PRIV frames and exposes their bytes as escaped metadata.
  const auto tag = av_dict_get(
      context->metadata,
      "id3v2_priv.com.apple.streaming.transportStreamTimestamp", nullptr, 0);
  if (!tag)
    throw std::runtime_error("Packed HLS audio has no transport timestamp");
  std::string_view value(tag->value);
  std::array<unsigned char, 8> bytes{};
  for (auto& byte : bytes) {
    if (value.empty())
      throw std::runtime_error("Invalid packed HLS audio timestamp");
    if (value.front() == '\\') {
      unsigned number = 0;
      if (value.size() < 4 || value[1] != 'x')
        throw std::runtime_error("Invalid packed HLS audio timestamp");
      const auto parsed =
          std::from_chars(value.data() + 2, value.data() + 4, number, 16);
      if (parsed.ec != std::errc() || parsed.ptr != value.data() + 4)
        throw std::runtime_error("Invalid packed HLS audio timestamp");
      byte = static_cast<unsigned char>(number);
      value.remove_prefix(4);
    }
    else {
      byte = static_cast<unsigned char>(value.front());
      value.remove_prefix(1);
    }
  }
  const auto clock = AV_RB64(bytes.data());
  if (!value.empty() || clock >= (uint64_t{1} << 33))
    throw std::runtime_error("Invalid packed HLS audio timestamp");
  return static_cast<int64_t>(clock);
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
  bool prefetched = false;
  std::string type;
  Control* control = nullptr;
  std::vector<Segment> segments;
  size_t nextSegment = 0;
  int64_t shift = 0, presentationStart = 0, clockOffset = 0;
  int64_t clipStart = 0, clipEnd = 0;
  int64_t boundary = 0;
  int64_t end = INT64_MAX;
  bool webvtt = false;
  std::optional<int64_t> transportClock;
  bool allowPreroll = false;
  bool trimAudio = false;
  std::vector<AVCodecParameters*> parameters;
  ~Input()
  {
    av_packet_free(&packet);
    avformat_close_input(&context);
    if (io) {
      av_freep(&io->buffer);
      avio_context_free(&io);
    }
    for (auto parameter : parameters)
      avcodec_parameters_free(&parameter);
  }
  static int read(void* opaque, uint8_t* data, int length) noexcept
  {
    auto& s = *static_cast<Input*>(opaque);
    if (s.control->cancel)
      return AVERROR_EXIT;
    try {
      while (s.index < s.files.size()) {
        if (!s.file.is_open()) {
          s.file.open(nativePath(s.files[s.index]), std::ios::binary);
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
      size += std::filesystem::file_size(nativePath(path));
    }
    auto buffer = static_cast<unsigned char*>(av_malloc(65536));
    if (!buffer)
      throw std::bad_alloc();
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
  bool openRun()
  {
    if (nextSegment == segments.size())
      return false;
    avformat_close_input(&context);
    if (io) {
      av_freep(&io->buffer);
      avio_context_free(&io);
    }
    file.close();
    file.clear();
    files.clear();
    starts.clear();
    position = size = 0;
    index = 0;
    const auto& first = segments[nextSegment];
    if (!first.init.empty())
      files.push_back(first.init);
    files.push_back(first.path);
    ++nextSegment;
    // A WebVTT header applies only to its own segment. Other compatible
    // fragments share one native demuxer so timestamp unwrapping is retained.
    if (type != "subtitle") {
      while (nextSegment < segments.size() &&
             segments[nextSegment].init == first.init &&
             segments[nextSegment].timeOffset == first.timeOffset) {
        files.push_back(segments[nextSegment++].path);
      }
    }
    open();
    webvtt = context->nb_streams == 1 &&
             context->streams[0]->codecpar->codec_id == AV_CODEC_ID_WEBVTT;
    clockOffset = first.hls ? (webvtt ? subtitleOffset(first.path) : 0)
                            : first.period * 1000 - first.timeOffset;
    if (first.hls && std::strcmp(context->iformat->name, "aac") == 0)
      transportClock = packedAudioClock(context);
    if (!first.hls)
      boundary = first.period * 1000;
    clipStart = first.hls ? (first.start - presentationStart) * 1000 : 0;
    clipEnd = clipStart + first.duration * 1000;
    if (!parameters.empty()) {
      if (parameters.size() != context->nb_streams)
        throw std::runtime_error("Media track layout changed during download");
      for (unsigned i = 0; i < context->nb_streams; ++i) {
        const auto a = parameters[i];
        const auto b = context->streams[i]->codecpar;
        if (!sameCodec(a, b))
          throw std::runtime_error("Media codec parameters changed; lossless "
                                   "concatenation is unavailable");
      }
    }
    else {
      for (unsigned i = 0; i < context->nb_streams; ++i) {
        auto parameter = avcodec_parameters_alloc();
        if (!parameter)
          throw std::bad_alloc();
        parameters.push_back(parameter);
        check(
            avcodec_parameters_copy(parameter, context->streams[i]->codecpar));
      }
    }
    return true;
  }
  void next()
  {
    if (!prefetched)
      av_packet_unref(packet);
    for (;;) {
      auto result = prefetched ? 0 : av_read_frame(context, packet);
      prefetched = false;
      if (result == AVERROR_EOF) {
        if (openRun())
          continue;
        ready = false;
        return;
      }
      check(result);
      if (packet->stream_index >= 0 &&
          static_cast<size_t>(packet->stream_index) < mapping.size() &&
          mapping[packet->stream_index] >= 0) {
        av_packet_rescale_ts(packet,
                             context->streams[packet->stream_index]->time_base,
                             AV_TIME_BASE_Q);
        const auto offset = shift + clockOffset;
        if (packet->pts != AV_NOPTS_VALUE)
          packet->pts += offset;
        if (packet->dts != AV_NOPTS_VALUE)
          packet->dts += offset;
        const bool preroll =
            allowPreroll &&
            !(trimAudio &&
              context->streams[packet->stream_index]->codecpar->codec_type ==
                  AVMEDIA_TYPE_AUDIO);
        if (!(webvtt && segments.front().hls) &&
            packet->pts != AV_NOPTS_VALUE &&
            ((!preroll && packet->pts + packet->duration <= boundary) ||
             packet->pts >= end)) {
          // Initialization can expose a complete encoder-priming packet before
          // the Period. It must not overlap the preceding Period's audio.
          av_packet_unref(packet);
          continue;
        }
        if (webvtt && segments.front().hls) {
          const AVRational clock{1, 90000};
          const auto timestamp =
              av_rescale_q(packet->pts - shift, AV_TIME_BASE_Q, clock);
          const auto reference =
              av_rescale_q(clipStart - shift, AV_TIME_BASE_Q, clock);
          packet->pts =
              clipStart + av_rescale_q(av_compare_mod(timestamp, reference,
                                                      uint64_t{1} << 33),
                                       clock, AV_TIME_BASE_Q);
          const auto end = std::min(packet->pts + packet->duration, clipEnd);
          packet->pts = packet->dts = std::max(packet->pts, clipStart);
          packet->duration = end - packet->pts;
          if (packet->duration <= 0) {
            av_packet_unref(packet);
            continue;
          }
        }
        ready = true;
        return;
      }
      av_packet_unref(packet);
    }
  }
};
struct Output {
  AVFormatContext* context = nullptr;
  std::map<std::pair<int, unsigned>, unsigned> mapping;
  std::map<int, size_t> written;
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
void remux(Output& out, std::vector<std::unique_ptr<Input>>& inputs,
           const std::string& path, const std::string& format, bool video,
           bool audio, bool subtitles, Control* control)
{
  const bool firstEpoch = !out.context;
  if (firstEpoch) {
    check(avformat_alloc_output_context2(&out.context, nullptr,
                                         format == "mkv" ? "matroska" : "mp4",
                                         path.c_str()));
    if (!out.context)
      throw std::bad_alloc();
    // Native edit lists and codec delay preserve encoder priming; forcing
    // every timestamp to zero would make those samples audible.
  }
  unsigned outputIndex = 0;
  std::map<int, unsigned> streamOrdinals;
  const bool separateAudio =
      std::any_of(inputs.begin(), inputs.end(),
                  [](const auto& i) { return i->type == "audio"; });
  for (auto& input : inputs) {
    input->allowPreroll = firstEpoch;
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
      const auto key =
          std::make_pair(static_cast<int>(type), streamOrdinals[type]++);
      const auto found = out.mapping.find(key);
      auto stream = firstEpoch ? avformat_new_stream(out.context, nullptr)
                               : (found != out.mapping.end()
                                      ? out.context->streams[found->second]
                                      : nullptr);
      if (!stream)
        throw std::runtime_error("Media track layout changed between periods");
      ++outputIndex;
      input->mapping[i] = stream->index;
      if (firstEpoch) {
        out.mapping[key] = stream->index;
        check(avcodec_parameters_copy(stream->codecpar, source->codecpar));
        stream->codecpar->codec_tag = 0;
        stream->time_base = source->time_base;
        av_dict_copy(&stream->metadata, source->metadata, 0);
      }
      else {
        const auto a = stream->codecpar;
        const auto b = source->codecpar;
        if (!sameCodec(a, b))
          throw std::runtime_error(
              "Media codec parameters changed between periods");
      }
    }
  }
  if (!out.context->nb_streams)
    throw std::runtime_error("No selected media tracks can be saved");
  if (outputIndex != out.context->nb_streams)
    throw std::runtime_error("Media track layout changed between periods");
  if (firstEpoch) {
    check(avio_open(&out.context->pb, nativePath(path).u8string().c_str(),
                    AVIO_FLAG_WRITE));
    check(avformat_write_header(out.context, nullptr));
  }
  for (auto& input : inputs)
    input->next();
  for (;;) {
    if (control->cancel)
      throw std::runtime_error("Media remux interrupted");
    Input* next = nullptr;
    for (auto& input : inputs) {
      if (!input->ready)
        continue;
      if (!next || input->packet->dts < next->packet->dts)
        next = input.get();
    }
    if (!next)
      break;
    auto packet = next->packet;
    packet->stream_index = next->mapping[packet->stream_index];
    av_packet_rescale_ts(packet, AV_TIME_BASE_Q,
                         out.context->streams[packet->stream_index]->time_base);
    packet->pos = -1;
    const auto index = packet->stream_index;
    check(av_interleaved_write_frame(out.context, packet));
    ++out.written[index];
    next->next();
  }
}
} // namespace
int64_t Muxer::startTime(const Segment& segment,
                         const std::shared_ptr<Control>& control,
                         std::optional<int64_t> reference)
{
  Input input;
  input.control = control.get();
  input.type = segment.type;
  input.segments = {segment};
  input.openRun();
  const AVRational clock{1, 90000};
  auto timestamp = input.transportClock ? av_rescale_q(*input.transportClock,
                                                       clock, AV_TIME_BASE_Q)
                                        : input.context->start_time;
  if (timestamp == AV_NOPTS_VALUE)
    throw std::runtime_error("Live media has no presentation timestamp");
  if (reference)
    timestamp =
        *reference * 1000 +
        av_rescale_q(
            av_compare_mod(av_rescale_q(timestamp, AV_TIME_BASE_Q, clock),
                           av_rescale_q(*reference, AVRational{1, 1000}, clock),
                           uint64_t{1} << 33),
            clock, AV_TIME_BASE_Q);
  return av_rescale_q(timestamp, AV_TIME_BASE_Q, AVRational{1, 1000});
}
std::string Muxer::stage(const std::vector<Segment>& segments,
                         const std::string& output,
                         const std::string& directory,
                         const std::string& format, bool video, bool audio,
                         bool subtitles,
                         const std::shared_ptr<Control>& control,
                         int64_t presentationDuration, bool live)
{
  if (segments.empty())
    throw std::runtime_error("No complete media segments were received");
  using Epoch = std::pair<int64_t, int64_t>;
  std::map<Epoch, std::map<std::string, std::vector<Segment>>> periods;
  std::map<std::string, std::string> verified;
  int64_t presentationStart = INT64_MAX;
  for (const auto& segment : segments) {
    for (const auto& resource :
         {std::make_pair(segment.path, segment.digest),
          std::make_pair(segment.init, segment.initDigest)}) {
      if (resource.first.empty())
        continue;
      auto found = verified.find(resource.first);
      if (found == verified.end())
        found =
            verified.emplace(resource.first, Transport::digest(resource.first))
                .first;
      if (found->second != resource.second)
        throw std::runtime_error(
            "Media recovery data is damaged; resume to fetch it again");
    }
    periods[{segment.period, segment.discontinuity}][segment.track].push_back(
        segment);
    if (segment.hls)
      presentationStart = std::min(presentationStart, segment.start);
  }
  const auto temporary = output + "." +
                         nativePath(directory).filename().u8string() +
                         ".media-partial";
  struct Cleanup {
    std::string path;
    bool committed = false;
    ~Cleanup()
    {
      if (!committed) {
        std::error_code ignored;
        std::filesystem::remove(nativePath(path), ignored);
      }
    }
  } cleanup{temporary};
  Output out;
  int64_t liveOrigin = INT64_MIN;
  for (const auto& period : periods) {
    std::vector<std::unique_ptr<Input>> inputs;
    int64_t end = 0;
    for (const auto& track : period.second) {
      auto input = std::make_unique<Input>();
      input->control = control.get();
      input->type = track.second.front().type;
      input->segments = track.second;
      input->presentationStart = presentationStart;
      input->openRun();
      for (const auto& segment : track.second)
        end = std::max(end, segment.start + segment.duration);
      inputs.push_back(std::move(input));
    }
    if (inputs.front()->segments.front().hls) {
      int64_t origin = INT64_MAX, beginning = INT64_MAX;
      const AVRational clock{1, 90000};
      int64_t reference = AV_NOPTS_VALUE;
      for (const auto& input : inputs)
        if (!input->webvtt && !input->transportClock &&
            input->context->start_time != AV_NOPTS_VALUE) {
          reference = input->context->start_time;
          break;
        }
      for (const auto& input : inputs) {
        beginning = std::min(beginning, input->segments.front().start);
        if (input->transportClock) {
          input->clockOffset =
              reference == AV_NOPTS_VALUE
                  ? av_rescale_q(*input->transportClock, clock, AV_TIME_BASE_Q)
                  : reference +
                        av_rescale_q(
                            av_compare_mod(
                                *input->transportClock,
                                av_rescale_q(reference, AV_TIME_BASE_Q, clock),
                                uint64_t{1} << 33),
                            clock, AV_TIME_BASE_Q);
          origin = std::min(origin, input->clockOffset);
        }
        else if (!input->webvtt && input->context->start_time != AV_NOPTS_VALUE)
          origin = std::min(origin, input->context->start_time);
      }
      if (origin == INT64_MAX)
        origin = 0;
      int64_t commonEnd = INT64_MAX;
      if (live) {
        for (const auto& input : inputs) {
          if (input->webvtt)
            continue;
          if (period.first == periods.begin()->first)
            liveOrigin = std::max(liveOrigin, input->segments.front().start);
          const auto& last = input->segments.back();
          commonEnd = std::min(commonEnd, last.start + last.duration);
        }
        if (liveOrigin == INT64_MIN)
          throw std::runtime_error("Live media has no audio or video timeline");
        if (format == "mkv") {
          for (auto& input : inputs) {
            if (input->type == "audio" || input->webvtt ||
                input->segments.front().start >= liveOrigin)
              continue;
            const auto target =
                (liveOrigin - beginning) * 1000 + origin - input->clockOffset;
            check(avformat_seek_file(input->context, -1, target, target,
                                     INT64_MAX, 0));
            for (;;) {
              check(av_read_frame(input->context, input->packet));
              const auto stream =
                  input->context->streams[input->packet->stream_index];
              if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                const auto timestamp = av_rescale_q(
                    input->packet->pts, stream->time_base, AV_TIME_BASE_Q);
                liveOrigin = beginning +
                             (timestamp + input->clockOffset - origin) / 1000;
                input->prefetched = true;
                break;
              }
              av_packet_unref(input->packet);
            }
          }
        }
        if (commonEnd <= liveOrigin)
          throw std::runtime_error(
              "Live tracks have no common recording window");
      }
      int64_t nextBoundary = INT64_MAX;
      const auto next = periods.upper_bound(period.first);
      if (next != periods.end())
        for (const auto& track : next->second)
          nextBoundary =
              std::min(nextBoundary,
                       (track.second.front().start - presentationStart) * 1000);
      for (auto& input : inputs) {
        input->boundary = (beginning - presentationStart) * 1000;
        // Complete HLS fragments own their packet boundary. Millisecond-rounded
        // playlist durations must not truncate audio after many short segments.
        input->end = nextBoundary;
        input->shift = (beginning - presentationStart) * 1000 - origin;
        if (live) {
          // Native MP4 edit lists retain decoder preroll while exposing only
          // the common recording window. All tracks keep their source clock.
          input->boundary = (beginning - liveOrigin) * 1000;
          input->shift = (beginning - liveOrigin) * 1000 - origin;
          input->end = (commonEnd - liveOrigin) * 1000;
          if (format == "mkv") {
            // Matroska has no edit lists. Start at the native demuxer's next
            // video keyframe and discard preceding audio.
            input->trimAudio = true;
            input->boundary = 0;
          }
        }
      }
    }
    else {
      if (live && liveOrigin == INT64_MIN) {
        int64_t origin = INT64_MAX;
        for (const auto& input : inputs)
          if (!input->webvtt && input->context->start_time != AV_NOPTS_VALUE)
            origin = std::min(origin,
                              input->context->start_time + input->clockOffset);
        if (origin == INT64_MAX)
          throw std::runtime_error("Live media has no presentation timestamp");
        liveOrigin = av_rescale_q(origin, AV_TIME_BASE_Q, AVRational{1, 1000});
      }
      const auto next = periods.upper_bound({period.first.first, INT64_MAX});
      const auto boundary =
          next == periods.end() ? presentationDuration : next->first.first;
      for (auto& input : inputs) {
        input->end =
            (boundary > 0 ? std::min(period.first.first + end, boundary)
                          : period.first.first + end) *
            1000;
        if (live) {
          input->shift = -liveOrigin * 1000;
          input->boundary -= liveOrigin * 1000;
          input->end -= liveOrigin * 1000;
        }
      }
    }
    remux(out, inputs, temporary, format, video, audio, subtitles,
          control.get());
  }
  if (out.written.empty())
    throw std::runtime_error("The selected media contained no samples");
  for (unsigned i = 0; i < out.context->nb_streams; ++i) {
    const auto type = out.context->streams[i]->codecpar->codec_type;
    if ((type == AVMEDIA_TYPE_AUDIO || type == AVMEDIA_TYPE_VIDEO) &&
        !out.written[i])
      throw std::runtime_error(
          "A selected audio or video track contained no samples");
  }
  check(av_write_trailer(out.context));
  check(avio_closep(&out.context->pb));
  syncFile(temporary);
  syncParent(temporary);
  if (control->cancel)
    throw std::runtime_error("Media finalization interrupted");
  cleanup.committed = true;
  return temporary;
}
void Muxer::publish(const std::string& staging, const std::string& output,
                    bool overwrite)
{
  const auto source = nativePath(staging);
  const auto destination = nativePath(output);
#ifdef _WIN32
  if (!MoveFileExW(source.c_str(), destination.c_str(),
                   overwrite
                       ? MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
                       : MOVEFILE_WRITE_THROUGH))
    throw std::system_error(GetLastError(), std::system_category(),
                            "Cannot publish media output");
#else
  if (overwrite)
    std::filesystem::rename(source, destination);
  else {
    std::filesystem::create_hard_link(source, destination);
    std::filesystem::remove(source);
  }
#endif
}
} // namespace media
} // namespace aria2
