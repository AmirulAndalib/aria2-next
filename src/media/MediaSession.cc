/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
extern "C" {
#include <libavutil/log.h>
}

#include "MediaSession.h"
#include "media/MediaDownload.h"
#include "spdlog/common.h"
#include <cstdarg>
#include <exception>
#include <gpac/setup.h>
#include <gpac/tools.h>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include "MediaJob.h"
#include "MediaFiles.h"
#include "Log.h"

namespace aria2::media {
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
      MediaJob job(option, uri, gid, directory, control);
      job.run();
    }
    catch (const std::exception& error) {
      std::lock_guard<std::mutex> lock(control->mutex);
      if (!control->cancel && control->snapshot.state != "awaiting-selection") {
        control->snapshot.error = failureMessage(error);
        control->snapshot.state = "error";
      }
    }
    std::lock_guard<std::mutex> lock(control->mutex);
    if (control->cancel && control->snapshot.state != "complete")
      control->snapshot.state = "paused";
    else if (control->finish && control->snapshot.state != "complete" &&
             control->snapshot.state != "error") {
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
  control_->requestCancel();
  join();
}
void Session::join()
{
  if (worker_.joinable())
    worker_.join();
}

} // namespace aria2::media
