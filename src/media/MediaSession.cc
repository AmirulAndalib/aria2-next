/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "MediaSession.h"
#include "MediaStore.h"
#include "MediaTransport.h"
#include "MediaMuxer.h"
#include "Option.h"
#include "prefs.h"
#include "Log.h"
#include "fmt.h"

#include <gpac/dash.h>
extern "C" {
#include <libavutil/log.h>
}
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace aria2 {
namespace media {
namespace {
std::once_flag initialization;
void nativeLog(void* context, int level, const char* format,
               va_list args) noexcept
{
  if (level > AV_LOG_WARNING || !A2_LOG_ENABLED(spdlog::level::debug))
    return;
  try {
    char line[2048];
    int prefix = 1;
    av_log_format_line2(context, level, format, args, line, sizeof(line),
                        &prefix);
    logging::tryWrite(spdlog::level::debug, __FILE__, __LINE__,
                      "component=media library=ffmpeg " +
                          logging::sanitizeText(line));
  }
  catch (...) {
  }
}
void initialize()
{
  // GPAC's null profile disables its independent on-disk application settings.
  if (gf_sys_init(GF_MemTrackerNone, "n") != GF_OK)
    throw std::runtime_error("Cannot initialize GPAC");
  gf_log_set_tools_levels("all@quiet", GF_TRUE);
  av_log_set_level(AV_LOG_WARNING);
  av_log_set_callback(nativeLog);
}
struct Job;
struct Io {
  Job* job;
  std::string url;
  int group;
  int64_t begin = 0, end = -1;
  bool fetched = false;
  Resource resource;
  GF_Err result = GF_OK;
};
struct Job {
  std::shared_ptr<Option> option;
  std::shared_ptr<Control> control;
  std::string uri, gid, directory, taskDirectory;
  Store store;
  Transport transport;
  GF_DASHFileIO io{};
  GF_DashClient* dash = nullptr;
  std::vector<int> selected;
  std::map<int, std::string> types, initializationFiles;
  bool completed = false, awaiting = false;
  bool live = false;
  std::string failure;

  Job(std::shared_ptr<Option> op, std::string source, std::string id,
      std::string root, std::shared_ptr<Control> ctl)
      : option(std::move(op)),
        control(std::move(ctl)),
        uri(std::move(source)),
        gid(std::move(id)),
        directory(std::move(root)),
        taskDirectory(
            (std::filesystem::u8path(directory) / "tasks" / gid).u8string()),
        store(directory, gid),
        transport(option.get(), uri, taskDirectory, control)
  {
    live = control->snapshot.live;
    auto identity =
        Transport::fingerprint(uri + "\n" + option->get(PREF_MEDIA_VIDEO) +
                               "\n" + option->get(PREF_MEDIA_AUDIO) + "\n" +
                               option->get(PREF_MEDIA_SUBTITLES));
    if (store.identity(identity)) {
      std::lock_guard<std::mutex> lock(control->mutex);
      control->snapshot.downloadedLength = 0;
      control->snapshot.completedDuration = 0;
    }
    for (const auto& segment : store.segments()) {
      transport.retain(segment.path);
      transport.retain(segment.init);
    }
    std::filesystem::permissions(std::filesystem::u8path(taskDirectory),
                                 std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace);
    io.udta = this;
    io.create = [](GF_DASHFileIO* io, Bool, const char* url,
                   s32 group) -> void* {
      try {
        return new Io{static_cast<Job*>(io->udta), url ? url : "", group};
      }
      catch (...) {
        return nullptr;
      }
    };
    io.del = [](GF_DASHFileIO*, void* handle) {
      delete static_cast<Io*>(handle);
    };
    io.abort = [](GF_DASHFileIO*, void*) {};
    io.delete_cache_file = [](GF_DASHFileIO*, void*, const char*) {};
    io.setup_from_url = [](GF_DASHFileIO*, void* handle, const char* url,
                           s32 group) -> GF_Err {
      auto& h = *static_cast<Io*>(handle);
      try {
        h.url = url ? url : "";
        h.group = group;
        h.fetched = false;
        h.begin = 0;
        h.end = -1;
        h.result = GF_OK;
        return GF_OK;
      }
      catch (...) {
        return GF_OUT_OF_MEM;
      }
    };
    io.set_range = [](GF_DASHFileIO*, void* handle, u64 first, u64 last,
                      Bool) -> GF_Err {
      if (first > static_cast<u64>(INT64_MAX) ||
          last > static_cast<u64>(INT64_MAX) || last < first)
        return GF_BAD_PARAM;
      auto& h = *static_cast<Io*>(handle);
      h.begin = first;
      h.end = last;
      h.fetched = false;
      return GF_OK;
    };
    io.init = [](GF_DASHFileIO*, void* handle) -> GF_Err {
      auto& h = *static_cast<Io*>(handle);
      if (h.job->awaiting || h.job->control->cancel)
        return GF_SERVICE_ERROR;
      if (h.fetched)
        return h.result;
      try {
        h.resource = h.job->transport.get(h.url, h.begin, h.end,
                                          h.group >= 0 && !h.job->live);
        h.fetched = true;
        h.result = GF_OK;
      }
      catch (const std::exception& e) {
        h.job->failure = e.what();
        h.result = GF_IO_ERR;
      }
      return h.result;
    };
    io.run = [](GF_DASHFileIO*, void* handle) -> GF_Err {
      return static_cast<Io*>(handle)->result;
    };
    io.get_status = io.run;
    io.get_url = [](GF_DASHFileIO*, void* handle) -> const char* {
      auto& h = *static_cast<Io*>(handle);
      return h.resource.url.empty() ? h.url.c_str() : h.resource.url.c_str();
    };
    io.get_cache_name = [](GF_DASHFileIO*, void* handle) -> const char* {
      auto& h = *static_cast<Io*>(handle);
      return h.resource.path.empty() ? nullptr : h.resource.path.c_str();
    };
    io.get_mime = [](GF_DASHFileIO*, void* handle) -> const char* {
      return static_cast<Io*>(handle)->resource.mime.c_str();
    };
    io.get_header_value = [](GF_DASHFileIO*, void* handle,
                             const char* name) -> const char* {
      for (const auto& header : static_cast<Io*>(handle)->resource.headers)
        if (curl_strequal(name, header.first.c_str()))
          return header.second.c_str();
      return nullptr;
    };
    io.manifest_updated = [](GF_DASHFileIO* io, const char* name,
                             const char* path, s32 group) {
      auto& job = *static_cast<Job*>(io->udta);
      try {
        if (gf_dash_is_dynamic_mpd(job.dash)) {
          job.live = true;
          return;
        }
        std::ifstream input(std::filesystem::u8path(path), std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(input)), {});
        if (job.store.manifest(std::to_string(group) + ":" + (name ? name : ""),
                               Transport::fingerprint(text))) {
          job.transport.invalidate();
          auto value = job.snapshot();
          value.downloadedLength = 0;
          value.completedDuration = 0;
          job.publish(value);
        }
      }
      catch (const std::exception& error) {
        job.failure = error.what();
      }
    };
    io.get_utc_start_time = [](GF_DASHFileIO*, void* handle) -> u64 {
      return static_cast<Io*>(handle)->resource.utcStart;
    };
    io.get_total_size = [](GF_DASHFileIO*, void* handle) -> u32 {
      return static_cast<u32>(std::min<int64_t>(
          UINT32_MAX, static_cast<Io*>(handle)->resource.size));
    };
    io.get_bytes_done = io.get_total_size;
    io.get_bytes_per_sec = [](GF_DASHFileIO*, void*) -> u32 { return 0; };
    io.on_dash_event = [](GF_DASHFileIO* io, GF_DASHEventType event, s32 group,
                          GF_Err error) -> GF_Err {
      auto& job = *static_cast<Job*>(io->udta);
      try {
        return job.event(event, group, error);
      }
      catch (const std::exception& e) {
        job.failure = e.what();
        return GF_IO_ERR;
      }
      catch (...) {
        job.failure = "Media client callback failed";
        return GF_IO_ERR;
      }
    };
    dash = gf_dash_new(&io, 1000, 0, GF_TRUE, GF_TRUE,
                       GF_DASH_SELECT_BANDWIDTH_HIGHEST, 0);
    if (!dash)
      throw std::runtime_error("Cannot create native HLS/DASH client");
    gf_dash_set_algo(dash, GF_DASH_ALGO_NONE);
    gf_dash_disable_speed_adaptation(dash, GF_TRUE);
  }
  ~Job()
  {
    if (dash)
      gf_dash_del(dash);
  }
  Snapshot snapshot()
  {
    std::lock_guard<std::mutex> lock(control->mutex);
    return control->snapshot;
  }
  void publish(Snapshot value)
  {
    store.save(value);
    std::lock_guard<std::mutex> lock(control->mutex);
    control->snapshot = std::move(value);
  }
  std::string localResource(const char* url, int64_t begin = 0,
                            int64_t end = -1)
  {
    if (!url || !*url)
      return {};
    std::string value(url);
    if (value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0)
      return transport.get(value, begin, end, !live).path;
    const auto path =
        std::filesystem::weakly_canonical(std::filesystem::u8path(value));
    const auto root = std::filesystem::weakly_canonical(
        std::filesystem::u8path(taskDirectory));
    if (path.parent_path() != root || !std::filesystem::is_regular_file(path))
      throw std::runtime_error(
          "Manifest requested a file outside its media cache");
    if (end >= 0 && std::filesystem::file_size(path) !=
                        static_cast<uint64_t>(end - begin + 1)) {
      if (begin < 0 || end < begin ||
          static_cast<uint64_t>(end) >= std::filesystem::file_size(path))
        throw std::runtime_error(
            "Cached media resource does not contain the required byte range");
      auto slice = root / Transport::fingerprint(path.u8string() + ":" +
                                                 std::to_string(begin) + ":" +
                                                 std::to_string(end));
      std::ifstream input(path, std::ios::binary);
      input.seekg(begin);
      std::ofstream output(slice, std::ios::binary | std::ios::trunc);
      std::array<char, 65536> buffer{};
      int64_t remaining = end - begin + 1;
      while (remaining > 0) {
        auto count = static_cast<std::streamsize>(
            std::min<int64_t>(remaining, buffer.size()));
        input.read(buffer.data(), count);
        output.write(buffer.data(), input.gcount());
        if (input.gcount() != count || !output)
          throw std::runtime_error("Cannot materialize media byte range");
        remaining -= count;
      }
      output.close();
      return slice.u8string();
    }
    return path.u8string();
  }
  GF_Err event(GF_DASHEventType event, int detail, GF_Err error)
  {
    if (error < 0) {
      if (failure.empty())
        failure = gf_error_to_string(error);
      return error;
    }
    if (event == GF_DASH_EVENT_SELECT_GROUPS) {
      selected.clear();
      types.clear();
      initializationFiles.clear();
      auto value = snapshot();
      value.tracks.clear();
      value.protocol = gf_dash_is_m3u8(dash) ? "hls" : "dash";
      value.live = gf_dash_is_dynamic_mpd(dash);
      live = live || value.live;
      value.duration = static_cast<int64_t>(gf_dash_get_duration(dash) * 1000);
      if (gf_dash_is_smooth_streaming(dash))
        throw std::runtime_error("Smooth Streaming is not supported");
      std::map<std::string, Track> chosen;
      for (u32 group = 0; group < gf_dash_get_group_count(dash); ++group) {
        gf_dash_group_select(dash, group, GF_FALSE);
        if (!gf_dash_is_group_selectable(dash, group))
          continue;
        const auto language = gf_dash_group_get_language(dash, group);
        for (u32 quality = 0;
             quality < gf_dash_group_get_num_qualities(dash, group);
             ++quality) {
          GF_DASHQualityInfo info{};
          if (gf_dash_group_get_quality_info(dash, group, quality, &info) !=
                  GF_OK ||
              info.disabled)
            continue;
          const std::string mime = info.mime ? info.mime : "";
          std::string type = (info.width || mime.rfind("video", 0) == 0)
                                 ? "video"
                             : (info.sample_rate || info.nb_channels ||
                                mime.rfind("audio", 0) == 0)
                                 ? "audio"
                                 : "subtitle";
          if (gf_dash_is_m3u8(dash) && type == "video" &&
              (!info.codec || !*info.codec || std::strchr(info.codec, ',')))
            type = "muxed";
          Track track{std::to_string(group) + ":" + std::to_string(quality),
                      type,
                      language ? language : "",
                      info.codec ? info.codec : "",
                      static_cast<int>(info.width),
                      static_cast<int>(info.height),
                      info.bandwidth,
                      false};
          value.tracks.push_back(track);
          auto pref =
              type == "video" || (type == "muxed" &&
                                  option->get(PREF_MEDIA_VIDEO) != "none")
                  ? PREF_MEDIA_VIDEO
              : type == "audio" || type == "muxed" ? PREF_MEDIA_AUDIO
                                                   : PREF_MEDIA_SUBTITLES;
          const auto& selection = option->get(pref);
          if (selection == "none")
            continue;
          if (selection != "best" && selection != track.id &&
              selection != track.language)
            continue;
          if (!chosen.count(type) || chosen[type].bandwidth < track.bandwidth)
            chosen[type] = track;
        }
      }
      for (const auto& type : {std::string("video"), std::string("audio"),
                               std::string("subtitle")}) {
        const auto pref = type == "video"   ? PREF_MEDIA_VIDEO
                          : type == "audio" ? PREF_MEDIA_AUDIO
                                            : PREF_MEDIA_SUBTITLES;
        const auto& selection = option->get(pref);
        if (selection != "best" && selection != "none" && !chosen.count(type) &&
            !chosen.count("muxed"))
          throw std::runtime_error(
              "Requested media track or language is unavailable: " + selection);
      }
      for (auto& track : value.tracks) {
        auto choice = chosen.find(track.type);
        track.selected =
            choice != chosen.end() && choice->second.id == track.id;
        if (!track.selected)
          continue;
        auto separator = track.id.find(':');
        auto group = std::stoi(track.id.substr(0, separator));
        auto quality = std::stoi(track.id.substr(separator + 1));
        const char* descriptor = nullptr;
        if (gf_dash_group_enum_descriptor(dash, group,
                                          GF_MPD_DESC_CONTENT_PROTECTION, 0,
                                          nullptr, &descriptor, nullptr))
          throw std::runtime_error("DRM-protected media is not supported");
        gf_dash_group_select(dash, group, GF_TRUE);
        if (gf_dash_group_select_quality(dash, group, nullptr, quality) !=
            GF_OK)
          throw std::runtime_error("Cannot select the requested media quality");
        selected.push_back(group);
        types[group] = track.type;
      }
      if (selected.empty())
        throw std::runtime_error(
            "No media track matches the requested selection");
      if (option->getAsBool(PREF_MEDIA_PAUSE_AFTER_PROBE)) {
        for (int group : selected)
          gf_dash_group_select(dash, group, GF_FALSE);
        value.state = "awaiting-selection";
        awaiting = true;
        publish(value);
        return GF_SERVICE_ERROR;
      }
      value.state = value.live ? "recording" : "downloading";
      publish(value);
    }
    if (event == GF_DASH_EVENT_CREATE_PLAYBACK) {
      for (int group : selected) {
        u64 first = 0, last = 0;
        auto init = gf_dash_group_get_segment_init_url(dash, group, &first,
                                                       &last, nullptr);
        u32 crypto = 0;
        bin128 iv{};
        auto key =
            gf_dash_group_get_segment_init_keys(dash, group, &crypto, &iv);
        if (crypto > 1)
          throw std::runtime_error("Encrypted sample media is not supported");
        auto path =
            localResource(init, first, last ? static_cast<int64_t>(last) : -1);
        if (crypto == 1 && key)
          path = transport.decrypt(path, key, iv);
        if (gf_dash_group_init_segment_is_media(dash, group)) {
          u32 number = 0, duration = 0;
          GF_Fraction64 start{};
          const char* initName = nullptr;
          if (gf_dash_group_next_seg_info(dash, group, 0, nullptr, &number,
                                          &start, &duration, &initName) < 0)
            throw std::runtime_error(
                "Cannot read initial media segment timing");
          const auto period =
              static_cast<int64_t>(gf_dash_get_period_start(dash)) * 1000000;
          store.commit({period, group, number,
                        static_cast<int64_t>(
                            start.den ? start.num * 1000 / start.den : 0),
                        duration, path, "", types[group]});
        }
        else
          initializationFiles[group] = path;
        transport.retain(path);
        // GPAC queues the initialization resource as the first cache entry.
        // It has now been consumed, exactly as in the native dashin filter.
        gf_dash_group_discard_segment(dash, group);
      }
      progress();
    }
    // GPAC also reports -1 when the next segment is ahead of live. That is
    // an ordinary availability wait, not lost recording data. Joining at the
    // live edge may skip old segments before recording has started.
    if (event == GF_DASH_EVENT_TIMESHIFT_OVERFLOW && detail > 0 &&
        snapshot().completedDuration > 0)
      throw std::runtime_error("Live media left the server's retention window; "
                               "the recording has a gap");
    return GF_OK;
  }
  void progress()
  {
    auto rows = store.segments();
    std::map<int64_t, std::map<int, int64_t>> coverage;
    std::set<std::string> paths;
    auto value = snapshot();
    value.downloadedLength = 0;
    value.completedDuration = 0;
    for (const auto& row : rows) {
      if (!std::filesystem::is_regular_file(std::filesystem::u8path(row.path)))
        continue;
      coverage[row.period][row.track] += row.duration;
      if (paths.insert(row.path).second)
        value.downloadedLength +=
            std::filesystem::file_size(std::filesystem::u8path(row.path));
    }
    for (const auto& period : coverage) {
      int64_t minimum = INT64_MAX;
      for (const auto& track : period.second)
        minimum = std::min(minimum, track.second);
      if (minimum != INT64_MAX)
        value.completedDuration += minimum;
    }
    publish(value);
  }
  void run()
  {
    auto result = control->finish ? GF_OK : gf_dash_open(dash, uri.c_str());
    if (result < 0)
      throw std::runtime_error(failure.empty() ? gf_error_to_string(result)
                                               : failure);
    while (!control->cancel && !control->finish && !completed && !awaiting) {
      result = gf_dash_process(dash);
      if (awaiting)
        return;
      if (!failure.empty())
        throw std::runtime_error(failure);
      if (result < 0 && result != GF_IP_NETWORK_EMPTY && result != GF_NOT_READY)
        throw std::runtime_error(gf_error_to_string(result));
      if (result == GF_EOS) {
        completed = true;
        break;
      }
      if (gf_dash_is_in_setup(dash)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }
      bool allDone = !selected.empty(), advanced = false;
      for (int group : selected) {
        const char* url = nullptr;
        const char* key = nullptr;
        const char* name = nullptr;
        u64 first = 0, last = 0;
        bin128 iv{};
        u32 discontinuity = 0;
        auto status = gf_dash_group_get_next_segment_location(
            dash, group, 0, &url, &first, &last, nullptr, nullptr, nullptr,
            nullptr, nullptr, nullptr, &key, &iv, nullptr, &discontinuity);
        if (status == GF_EOS) {
          gf_dash_set_group_done(dash, group, GF_TRUE);
          continue;
        }
        allDone = false;
        if (status == GF_BUFFER_TOO_SMALL || status == GF_NOT_READY)
          continue;
        if (status < 0)
          throw std::runtime_error(gf_error_to_string(status));
        u32 number = 0, duration = 0;
        GF_Fraction64 start{};
        auto info = gf_dash_group_next_seg_info(dash, group, 0, &name, &number,
                                                &start, &duration, nullptr);
        if (info < 0)
          throw std::runtime_error(gf_error_to_string(info));
        auto path =
            localResource(url, first, last ? static_cast<int64_t>(last) : -1);
        if (!path.empty()) {
          if (key && *key)
            path = transport.decrypt(path, key, iv);
          const auto period =
              static_cast<int64_t>(gf_dash_get_period_start(dash)) * 1000000 +
              discontinuity;
          store.commit(
              {period, group, number,
               static_cast<int64_t>(start.den ? start.num * 1000 / start.den
                                              : 0),
               duration, path, initializationFiles[group], types[group]});
          transport.retain(path);
          transport.retain(initializationFiles[group]);
          gf_dash_group_discard_segment(dash, group);
          advanced = true;
        }
      }
      if (advanced)
        progress();
      const auto limit = option->getAsLLInt(PREF_MEDIA_RECORD_TIME);
      if (limit > 0 && snapshot().live &&
          snapshot().completedDuration >= limit * 1000)
        control->finish = true;
      if (allDone) {
        if (gf_dash_in_last_period(dash, GF_TRUE) &&
            gf_dash_is_dynamic_mpd(dash) && !control->finish)
          throw std::runtime_error("Live source stopped updating without "
                                   "signaling the end of the recording");
        if (gf_dash_in_last_period(dash, GF_TRUE))
          completed = true;
        else
          gf_dash_request_period_switch(dash);
      }
      if (!advanced && !completed)
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min<u32>(
            100, std::max<u32>(10, gf_dash_get_min_wait_ms(dash)))));
    }
    if (control->cancel || awaiting)
      return;
    auto value = snapshot();
    value.state = "finalizing";
    publish(value);
    Muxer::write(store.segments(), value.path, taskDirectory,
                 option->get(PREF_MEDIA_FORMAT),
                 option->get(PREF_MEDIA_VIDEO) != "none",
                 option->get(PREF_MEDIA_AUDIO) != "none",
                 option->get(PREF_MEDIA_SUBTITLES) != "none",
                 option->getAsBool(PREF_ALLOW_OVERWRITE), control);
    value.state = "complete";
    value.completedLength =
        std::filesystem::file_size(std::filesystem::u8path(value.path));
    value.totalLength = value.completedLength;
    publish(value);
    store.remove();
    std::filesystem::remove_all(std::filesystem::u8path(taskDirectory));
  }
};
} // namespace
Session::Session(std::shared_ptr<Option> option, std::string uri,
                 std::string gid, std::string directory,
                 std::shared_ptr<Control> control)
    : control_(std::move(control))
{
  worker_ = std::thread([option = std::move(option), uri = std::move(uri),
                         gid = std::move(gid), directory = std::move(directory),
                         control = control_] {
    try {
      std::call_once(initialization, initialize);
      Job job(option, uri, gid, directory, control);
      job.run();
    }
    catch (const std::exception& error) {
      std::lock_guard<std::mutex> lock(control->mutex);
      if (!control->cancel && !control->finish &&
          control->snapshot.state != "awaiting-selection") {
        control->snapshot.error = error.what();
        control->snapshot.state = "error";
      }
    }
    std::lock_guard<std::mutex> lock(control->mutex);
    if (control->cancel)
      control->snapshot.state = "paused";
    else if (control->finish && control->snapshot.state != "complete") {
      control->snapshot.state = "error";
      control->snapshot.error = "Recording stopped before its current segment "
                                "completed; resume to retry";
    }
    control->connections = 0;
    control->done = true;
  });
}
Session::~Session()
{
  control_->cancel = true;
  join();
}
void Session::join()
{
  if (worker_.joinable())
    worker_.join();
}
} // namespace media
} // namespace aria2
