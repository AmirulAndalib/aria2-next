/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "Option.h"
#include "media/MediaStore.h"
#include "media/MediaTransport.h"
#include <cstdint>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <utility>
#include "MediaJob.h"
#include "MediaFiles.h"
#include "MediaMuxer.h"
#include "prefs.h"
#include "Log.h"
#include <filesystem>

namespace aria2::media {

void MediaJob::complete(const Publication& publication)
{
  syncParent(publication.output);
  auto value = snapshot();
  value.state = "complete";
  value.completedLength = value.totalLength = publication.bytes;
  {
    std::lock_guard<std::mutex> lock(control->mutex);
    control->snapshot = std::move(value);
  }
  // Once publication succeeded, cleanup errors cannot turn valid output
  // into a failed download. The publication record makes cleanup retryable.
  try {
    std::filesystem::remove_all(nativePath(taskDirectory));
    store.remove();
  }
  catch (const std::exception& error) {
    A2_LOG_WARN(std::string("Media recovery cleanup failed: ") +
                failureMessage(error));
  }
}

bool MediaJob::recoverPublication()
{
  auto publication = store.publication();
  if (!publication)
    return false;
  if (publication->output != snapshot().path ||
      publication->staging !=
          publication->output + "." + gid + ".media-partial")
    throw std::runtime_error(
        "Media output options changed during finalization");
  auto matches = [&](const std::string& path) {
    return std::filesystem::is_regular_file(nativePath(path)) &&
           std::filesystem::file_size(nativePath(path)) ==
               static_cast<uint64_t>(publication->bytes) &&
           Transport::digest(path) == publication->digest;
  };
  if (matches(publication->output)) {
    std::filesystem::remove(nativePath(publication->staging));
    complete(*publication);
    return true;
  }
  if (matches(publication->staging)) {
    Muxer::publish(publication->staging, publication->output,
                   option->getAsBool(PREF_ALLOW_OVERWRITE));
    complete(*publication);
    return true;
  }
  std::filesystem::remove(nativePath(publication->staging));
  store.clearPublication();
  return false;
}

void MediaJob::finalize()
{
  auto value = snapshot();
  value.state = "finalizing";
  publish(value);
  auto staging = Muxer::stage(store.segments(), value.path, taskDirectory,
                              option->get(PREF_MEDIA_FORMAT),
                              option->get(PREF_MEDIA_VIDEO) != "none",
                              option->get(PREF_MEDIA_AUDIO) != "none",
                              option->get(PREF_MEDIA_SUBTITLES) != "none",
                              control, value.duration, value.live);
  Publication publication{
      value.path, staging, Transport::digest(staging),
      static_cast<int64_t>(std::filesystem::file_size(nativePath(staging)))};
  store.preparePublication(publication);
  if (control->cancel)
    return;
  Muxer::publish(staging, value.path, option->getAsBool(PREF_ALLOW_OVERWRITE));
  complete(publication);
}

} // namespace aria2::media
