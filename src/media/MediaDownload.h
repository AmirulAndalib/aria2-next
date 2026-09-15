/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#ifndef D_MEDIA_DOWNLOAD_H
#define D_MEDIA_DOWNLOAD_H

#include <atomic>
#include <algorithm>
#include <cstdint>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "MediaError.h"

namespace aria2 {
class Command;
class DownloadEngine;
class Option;
class RequestGroup;

namespace media {
struct Track {
  std::string id, type, language, codec;
  int width = 0, height = 0;
  int64_t bandwidth = 0;
  bool selected = false;
  double frameRate = 0;
};

struct Snapshot {
  std::string state = "waiting";
  std::string protocol, path, error;
  FailureCode errorCode = FailureCode::None;
  std::vector<Track> tracks;
  int64_t duration = 0;
  int64_t completedDuration = 0;
  int64_t completedLength = 0;
  int64_t downloadedLength = 0;
  int64_t totalLength = 0;
  int64_t received = 0;
  int connections = 0;
  bool live = false;
  double progress() const
  {
    if (live || duration <= 0)
      return 0;
    return state == "complete"
               ? 1.0
               : std::clamp(static_cast<double>(completedDuration) / duration,
                            0.0, 1.0);
  }
};

struct Control {
  std::atomic<bool> cancel{false};
  std::atomic<bool> finish{false};
  std::atomic<bool> done{false};
  std::atomic<int64_t> received{0};
  std::atomic<int> connections{0};
  std::atomic<int64_t> downloadLimit{0};
  std::mutex mutex;
  std::condition_variable wake;
  Snapshot snapshot;
  void requestCancel()
  {
    {
      std::lock_guard<std::mutex> lock(mutex);
      cancel = true;
    }
    wake.notify_all();
  }
  void requestFinish()
  {
    {
      std::lock_guard<std::mutex> lock(mutex);
      finish = true;
    }
    wake.notify_all();
  }
};

class Session;
class Download : public std::enable_shared_from_this<Download> {
public:
  explicit Download(std::string uri);
  ~Download();
  std::unique_ptr<Command> start(RequestGroup* group, DownloadEngine* engine);
  void poll(RequestGroup* group);
  void stop(bool retainState);
  void restore(RequestGroup* group);
  bool finishRecording();
  bool stopped() const;
  const Snapshot& snapshot() const { return snapshot_; }
  static bool handles(const std::string& uri, const Option* option);
  static bool manifestMime(const std::string& mime);

private:
  std::string uri_, gid_, directory_;
  Snapshot snapshot_;
  std::unique_ptr<Session> session_;
  std::shared_ptr<Control> control_;
  bool retainState_ = true;
  bool finishRequested_ = false;
  int64_t accounted_ = 0;
  DownloadEngine* engine_ = nullptr;
};
} // namespace media
} // namespace aria2
#endif
