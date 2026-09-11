/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "FileEntry.h"
#include "MediaDownload.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <curl/urlapi.h>
#include <exception>
#include <memory>
#include <mutex>
#include <utility>
#include "MediaFiles.h"
#include "MediaSession.h"
#include "MediaStore.h"
#include "ApplicationStatePath.h"
#include "Command.h"
#include "DownloadContext.h"
#include "DownloadEngine.h"
#include "CurlSession.h"
#include "RequestGroupMan.h"
#include "Log.h"
#include "fmt.h"
#include "NetStat.h"
#include "Option.h"
#include "RequestGroup.h"
#include "error_code.h"
#include "prefs.h"
#include "support/FilePath.h"
#include <curl/curl.h>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace aria2 {
namespace media {
namespace {
class DownloadCommand final : public Command {
public:
  DownloadCommand(cuid_t id, std::shared_ptr<Download> download,
                  RequestGroup* group, DownloadEngine* engine)
      : Command(id),
        download_(std::move(download)),
        group_(group),
        engine_(engine)
  {
    setStatusRealtime();
    group_->increaseNumCommand();
  }
  ~DownloadCommand() override { group_->decreaseNumCommand(); }
  bool execute() override
  {
    try {
      if (group_->isHaltRequested() && !download_->stopped())
        download_->stop(group_->isPauseRequested() ||
                        group_->isShutdownRequested());
      download_->poll(group_);
      if (download_->snapshot().state == "awaiting-selection")
        group_->setPauseRequested(true);
      if (download_->snapshot().state == "error") {
        A2_LOG_ERROR(
            fmt("component=media event=download_failed gid=%s message=%s",
                group_->getGroupId()->toHex().c_str(),
                logging::sanitizeText(download_->snapshot().error).c_str()));
        group_->setLastErrorCode(error_code::NETWORK_PROBLEM,
                                 download_->snapshot().error.c_str());
      }
      if (download_->stopped())
        return true;
      engine_->addCommand(std::unique_ptr<Command>(this));
      engine_->setRefreshInterval(std::chrono::milliseconds(100));
      return false;
    }
    catch (const std::exception& error) {
      group_->setLastErrorCode(error_code::FILE_IO_ERROR,
                               failureMessage(error).c_str());
      try {
        download_->stop(true);
      }
      catch (...) {
      }
      return true;
    }
  }

private:
  std::shared_ptr<Download> download_;
  RequestGroup* group_;
  DownloadEngine* engine_;
};
std::string urlPath(const std::string& uri)
{
  auto url = curl_url();
  char* raw = nullptr;
  if (!url)
    throw std::bad_alloc();
  std::string path;
  if (curl_url_set(url, CURLUPART_URL, uri.c_str(), 0) == CURLUE_OK &&
      curl_url_get(url, CURLUPART_PATH, &raw, 0) == CURLUE_OK)
    path = raw;
  curl_free(raw);
  curl_url_cleanup(url);
  return path;
}
} // namespace
Download::Download(std::string uri) : uri_(std::move(uri)) {}
Download::~Download() = default;
bool Download::handles(const std::string& uri, const Option* option)
{
  const auto& mode = option->get(PREF_MEDIA);
  if (mode == "file")
    return false;
  if (mode == "hls" || mode == "dash")
    return true;
  if (uri.rfind("http://", 0) != 0 && uri.rfind("https://", 0) != 0)
    return false;
  auto path = urlPath(uri);
  std::transform(path.begin(), path.end(), path.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return (path.size() >= 5 && path.compare(path.size() - 5, 5, ".m3u8") == 0) ||
         (path.size() >= 4 && path.compare(path.size() - 4, 4, ".mpd") == 0);
}
bool Download::manifestMime(const std::string& mime)
{
  auto type = mime.substr(0, mime.find(';'));
  return curl_strequal(type.c_str(), "application/vnd.apple.mpegurl") ||
         curl_strequal(type.c_str(), "application/x-mpegurl") ||
         curl_strequal(type.c_str(), "audio/mpegurl") ||
         curl_strequal(type.c_str(), "audio/x-mpegurl") ||
         curl_strequal(type.c_str(), "application/dash+xml");
}
void Download::restore(RequestGroup* group)
{
  if (snapshot_.state != "waiting")
    return;
  gid_ = group->getGroupId()->toHex();
  directory_ = state::mediaDirectory(group->getOption().get());
  if (directory_.empty())
    throw std::runtime_error("Media recovery requires a state directory");
  if (std::filesystem::exists(nativePath(directory_) / "state.db")) {
    try {
      Store store(directory_, gid_);
      store.load(snapshot_);
    }
    catch (const std::exception& error) {
      snapshot_.error = failureMessage(error);
    }
  }
  snapshot_.state = "paused";
  if (!snapshot_.path.empty())
    group->getDownloadContext()->getFirstFileEntry()->setPath(snapshot_.path);
}
std::unique_ptr<Command> Download::start(RequestGroup* group,
                                         DownloadEngine* engine)
try {
  restore(group);
  const auto& option = group->getOption();
  if (snapshot_.protocol.empty()) {
    const auto path = urlPath(uri_);
    snapshot_.protocol =
        option->get(PREF_MEDIA) == "dash" ||
                (path.size() >= 4 &&
                 curl_strequal(path.c_str() + path.size() - 4, ".mpd"))
            ? "dash"
            : "hls";
  }
  if (option->getAsBool(PREF_DRY_RUN))
    throw std::runtime_error(
        "Use media-pause-after-probe for media inspection");
  if (!option->blank(PREF_CHECKSUM))
    throw std::runtime_error(
        "A source checksum cannot validate a remuxed presentation; use "
        "media=file to download the source unchanged");
  {
    auto name = option->get(PREF_OUT);
    if (name.empty()) {
      auto path = urlPath(uri_);
      auto slash = path.find_last_of('/');
      name = path.substr(slash == std::string::npos ? 0 : slash + 1);
      int length = 0;
      auto decoded = curl_easy_unescape(nullptr, name.c_str(),
                                        static_cast<int>(name.size()), &length);
      if (decoded) {
        name.assign(decoded, length);
        curl_free(decoded);
      }
      name = util::createSafePath(name);
      auto dot = name.find_last_of('.');
      if (dot != std::string::npos)
        name.resize(dot);
      if (name.empty() || name == "." || name == "..")
        name = "media-" + gid_;
      name += "." + option->get(PREF_MEDIA_FORMAT);
    }
    snapshot_.path = std::filesystem::absolute(
                         std::filesystem::u8path(option->get(PREF_DIR)) /
                         std::filesystem::u8path(name))
                         .u8string();
  }
  if (std::filesystem::exists(nativePath(snapshot_.path)) &&
      !option->getAsBool(PREF_ALLOW_OVERWRITE)) {
    Store store(directory_, gid_);
    const auto publication = store.publication();
    if (!publication || publication->output != snapshot_.path)
      throw std::runtime_error("Media output already exists; choose another "
                               "path or allow-overwrite");
  }
  std::filesystem::create_directories(nativePath(snapshot_.path).parent_path());
  group->getDownloadContext()->getFirstFileEntry()->setPath(snapshot_.path);
  group->getDownloadContext()->setBasePath(snapshot_.path);
  if (engine->getRequestGroupMan()->isSameFileBeingDownloaded(group))
    throw std::runtime_error("Another task is writing the media output path");
  snapshot_.state = "probing";
  snapshot_.error.clear();
  snapshot_.received = 0;
  control_ = std::make_shared<Control>();
  control_->snapshot = snapshot_;
  control_->finish = finishRequested_;
  finishRequested_ = false;
  engine_ = engine;
  control_->downloadLimit = option->getAsLLInt(PREF_MAX_DOWNLOAD_LIMIT);
  auto frozen = std::make_shared<Option>();
  for (size_t i = 1; i < option::countOption(); ++i) {
    auto pref = option::i2p(i);
    if (option->defined(pref))
      frozen->put(pref, option->get(pref));
  }
  session_ = std::make_unique<Session>(std::move(frozen), uri_, gid_,
                                       directory_, control_);
  retainState_ = true;
  accounted_ = 0;
  group->getDownloadContext()->getNetStat().downloadStart();
  return std::make_unique<DownloadCommand>(engine->newCUID(),
                                           shared_from_this(), group, engine);
}
catch (const std::exception& error) {
  if (control_) {
    control_->requestCancel();
  }
  snapshot_.state = "error";
  snapshot_.error = failureMessage(error);
  throw std::runtime_error(snapshot_.error);
}
void Download::poll(RequestGroup* group)
{
  if (!control_)
    return;
  {
    std::lock_guard<std::mutex> lock(control_->mutex);
    snapshot_ = control_->snapshot;
  }
  snapshot_.received = control_->received.load();
  snapshot_.connections = control_->connections.load();
  size_t mediaCount = 0;
  for (const auto& task : engine_->getRequestGroupMan()->getRequestGroups())
    if (task->getMediaDownload() && !task->getMediaDownload()->stopped())
      ++mediaCount;
  auto curl = engine_->getCurlSession();
  if (curl)
    curl->setExternalDownloadCount(mediaCount);
  const auto consumers =
      std::max<size_t>(1, mediaCount + (curl ? curl->activeCount() : 0));
  const auto globalLimit =
      engine_->getOption()->getAsLLInt(PREF_MAX_OVERALL_DOWNLOAD_LIMIT);
  auto limit = group->getMaxDownloadSpeedLimit();
  if (globalLimit > 0) {
    auto share =
        std::max<int64_t>(1, globalLimit / static_cast<int64_t>(consumers));
    limit = limit > 0 ? std::min<int64_t>(limit, share) : share;
  }
  control_->downloadLimit = limit;
  if (snapshot_.received > accounted_) {
    group->getDownloadContext()->getNetStat().updateDownload(
        snapshot_.received - accounted_);
    accounted_ = snapshot_.received;
  }
  if (snapshot_.totalLength > 0) {
    group->getDownloadContext()->getFirstFileEntry()->setLength(
        snapshot_.totalLength);
    group->getDownloadContext()->markTotalLengthIsKnown();
  }
  if (stopped()) {
    session_->join();
    group->getDownloadContext()->getNetStat().downloadStop();
    if (!retainState_) {
      Store::discard(directory_, gid_);
      snapshot_.state = "removed";
    }
  }
}
void Download::stop(bool retainState)
{
  retainState_ = retainState;
  if (control_) {
    control_->requestCancel();
  }
  if (control_ && !control_->done)
    return;
  if (session_)
    session_->join();
  if (!retainState && !gid_.empty()) {
    Store::discard(directory_, gid_);
  }
  snapshot_.state = retainState ? "paused" : "removed";
}
bool Download::finishRecording()
{
  if (!snapshot_.live || snapshot_.state == "complete" ||
      snapshot_.state == "removed")
    return false;
  if (!control_ || stopped()) {
    finishRequested_ = true;
    return true;
  }
  control_->requestFinish();
  return true;
}
bool Download::stopped() const
{
  if (control_ && !control_->done)
    return false;
  return snapshot_.state == "paused" || snapshot_.state == "removed" ||
         snapshot_.state == "complete" || snapshot_.state == "error" ||
         snapshot_.state == "awaiting-selection";
}
} // namespace media
} // namespace aria2
