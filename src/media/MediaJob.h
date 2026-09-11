/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#ifndef D_MEDIA_JOB_H
#define D_MEDIA_JOB_H

#include "MediaSession.h"
#include "MediaStore.h"
#include "MediaTransport.h"
#include "DashFileIo.h"
#include <map>
#include <tuple>

namespace aria2::media {
// One worker owns the native client, cache and publication transaction.
// Only Control crosses threads; access to its snapshot requires its mutex.
class MediaJob {
public:
  MediaJob(std::shared_ptr<Option>, std::string source, std::string gid,
           std::string directory, std::shared_ptr<Control>);
  ~MediaJob();
  MediaJob(const MediaJob&) = delete;
  MediaJob& operator=(const MediaJob&) = delete;
  void run();

private:
  friend class DashFileIo;
  enum class SegmentResult { Complete, Waiting, Advanced };
  struct Group {
    std::string type, identity, init;
    // Microseconds; segment start/duration and period use milliseconds.
    int64_t timeOffset = 0;
  };
  using SegmentKey = std::tuple<int64_t, std::string, int64_t>;
  std::shared_ptr<Option> option;
  std::shared_ptr<Control> control;
  std::string uri, gid, directory, taskDirectory;
  Store store;
  Transport transport;
  DashFileIo io;
  GF_DashClient* dash = nullptr;
  std::vector<int> selected;
  std::map<int, Group> groups;
  std::map<SegmentKey, Segment> retained;
  std::map<int64_t, std::map<std::string, int64_t>> coverage;
  std::map<std::pair<int64_t, std::string>, Segment> lastSegments;
  std::map<std::string, std::pair<int, int64_t>> retainedPaths;
  std::map<std::string, std::string> digests;
  int64_t retainedBytes = 0;
  bool completed = false, awaiting = false;
  bool live = false;
  std::string failure;

  Snapshot snapshot();
  void publish(Snapshot);
  void waitForNext();
  void remember(const Segment&);
  std::string digest(const std::string& path);
  void commit(Segment);
  Segment describe(int group, u32 number, const GF_Fraction64& start,
                   u32 duration, const std::string& path, u32 discontinuity);
  std::string localResource(const char* url, int64_t begin = 0,
                            int64_t end = -1);
  void manifestUpdated(const char* name, const char* path, int group);
  GF_Err event(GF_DASHEventType event, int detail, GF_Err error);
  GF_Err selectGroups();
  std::map<std::string, Track> chooseTracks(Snapshot&);
  void selectTrack(const Track&);
  void createPlayback();
  void progress();
  void complete(const Publication&);
  bool recoverPublication();
  void resumeLive();
  SegmentResult consumeSegment(int group);
  void finalize();
};
} // namespace aria2::media

#endif // D_MEDIA_JOB_H
