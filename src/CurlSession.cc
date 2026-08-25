/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2026 aria2-next contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
/* copyright --> */
#include "CurlSession.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <limits>
#include <sstream>
#include <utility>

#include "ApplicationStatePath.h"
#include "ChecksumCheckIntegrityEntry.h"
#include "CurlCheckIntegrityEntry.h"
#include "CurlDownload.h"
#include "CurlDownloadCommand.h"
#include "CurlDownloadImpl.h"
#include "Command.h"
#include "DownloadContext.h"
#include "DownloadEngine.h"
#include "DiskAdaptor.h"
#include "File.h"
#include "FileEntry.h"
#include "GroupId.h"
#include "Log.h"
#include "Option.h"
#include "PieceStorage.h"
#include "Request.h"
#include "RequestGroup.h"
#include "RequestGroupMan.h"
#include "TimeA2.h"
#include "a2io.h"
#include "fmt.h"
#include "prefs.h"
#include "uri.h"
#include "util.h"
#include "wallclock.h"

namespace aria2 {

class CurlSocketCommand final : public Command {
public:
  CurlSocketCommand(cuid_t cuid, curl_socket_t socket, CurlSession* session,
                    DownloadEngine* engine)
      : Command(cuid), socket_(socket), session_(session), engine_(engine)
  {
  }

  bool execute() override
  {
    if (removed_) {
      return true;
    }
    int events = 0;
    if (readEventEnabled()) {
      events |= CURL_CSELECT_IN;
    }
    if (writeEventEnabled()) {
      events |= CURL_CSELECT_OUT;
    }
    if (errorEventEnabled() || hupEventEnabled()) {
      events |= CURL_CSELECT_ERR;
    }
    session_->socketAction(socket_, events);
    if (removed_) {
      return true;
    }
    setStatusInactive();
    engine_->addCommand(std::unique_ptr<Command>(this));
    return false;
  }

  void update(int action)
  {
    const bool nextRead = action == CURL_POLL_IN || action == CURL_POLL_INOUT;
    const bool nextWrite =
        action == CURL_POLL_OUT || action == CURL_POLL_INOUT;
    if (read_ && !nextRead) {
      engine_->deleteSocketForReadCheck(socket_, this);
    }
    if (write_ && !nextWrite) {
      engine_->deleteSocketForWriteCheck(socket_, this);
    }
    if (!read_ && nextRead) {
      engine_->addSocketForReadCheck(socket_, this);
    }
    if (!write_ && nextWrite) {
      engine_->addSocketForWriteCheck(socket_, this);
    }
    read_ = nextRead;
    write_ = nextWrite;
  }

  void remove()
  {
    if (removed_) {
      return;
    }
    if (read_) {
      engine_->deleteSocketForReadCheck(socket_, this);
    }
    if (write_) {
      engine_->deleteSocketForWriteCheck(socket_, this);
    }
    read_ = false;
    write_ = false;
    removed_ = true;
  }

private:
  curl_socket_t socket_;
  CurlSession* session_;
  DownloadEngine* engine_;
  bool read_ = false;
  bool write_ = false;
  bool removed_ = false;
};

namespace {

std::string trimHeader(std::string value)
{
  while (!value.empty() && (value.back() == '\r' || value.back() == '\n' ||
                            value.back() == ' ' || value.back() == '\t')) {
    value.pop_back();
  }
  const auto first = value.find_first_not_of(" \t");
  return first == std::string::npos ? std::string() : value.substr(first);
}

bool startsWithHeader(const std::string& line, const char* name)
{
  const auto length = std::strlen(name);
  return line.size() >= length &&
         std::equal(line.begin(), line.begin() + length, name,
                    [](char lhs, char rhs) {
                      return std::tolower(static_cast<unsigned char>(lhs)) ==
                             std::tolower(static_cast<unsigned char>(rhs));
                    });
}

std::string outputPath(RequestGroup* group, const std::string& uriValue)
{
  const auto option = group->getOption();
  std::string name = option->get(PREF_OUT);
  if (name.empty()) {
    const auto& configured =
        group->getDownloadContext()->getFirstFileEntry()->getPath();
    if (!configured.empty()) {
      return configured;
    }
    uri::UriStruct parsed;
    if (uri::parse(parsed, uriValue)) {
      name = util::fixTaintedBasename(parsed.file);
    }
    if (name.empty()) {
      name = Request::DEFAULT_FILE;
    }
  }
  return util::applyDir(option->get(PREF_DIR), name);
}

std::string proxyFor(const Option* option, const std::string& uriValue)
{
  uri::UriStruct parsed;
  if (uri::parse(parsed, uriValue)) {
    if (util::strieq(parsed.protocol, "https") &&
        !option->blank(PREF_HTTPS_PROXY)) {
      return option->get(PREF_HTTPS_PROXY);
    }
    if (util::strieq(parsed.protocol, "http") &&
        !option->blank(PREF_HTTP_PROXY)) {
      return option->get(PREF_HTTP_PROXY);
    }
  }
  return option->get(PREF_ALL_PROXY);
}

std::string proxyUserFor(const Option* option, const std::string& protocol)
{
  const auto pref = util::strieq(protocol, "https")  ? PREF_HTTPS_PROXY_USER
                    : util::strieq(protocol, "http") ? PREF_HTTP_PROXY_USER
                                                     : PrefPtr();
  return !pref || option->blank(pref) ? option->get(PREF_ALL_PROXY_USER)
                                      : option->get(pref);
}

std::string proxyPasswordFor(const Option* option, const std::string& protocol)
{
  const auto pref = util::strieq(protocol, "https")  ? PREF_HTTPS_PROXY_PASSWD
                    : util::strieq(protocol, "http") ? PREF_HTTP_PROXY_PASSWD
                                                     : PrefPtr();
  return !pref || option->blank(pref) ? option->get(PREF_ALL_PROXY_PASSWD)
                                      : option->get(pref);
}

void markUriUsed(RequestGroup* group, const std::string& uriValue)
{
  auto file = group->getDownloadContext()->getFirstFileEntry();
  auto& remaining = file->getRemainingUris();
  remaining.erase(std::remove(remaining.begin(), remaining.end(), uriValue),
                  remaining.end());
  auto& spent = file->getSpentUris();
  if (std::find(spent.begin(), spent.end(), uriValue) == spent.end()) {
    spent.push_back(uriValue);
  }
}

void addCompletedRange(CurlDownloadImpl& impl, int64_t first, int64_t last)
{
  if (last <= first) {
    return;
  }
  impl.completedRanges.emplace_back(first, last);
  std::sort(impl.completedRanges.begin(), impl.completedRanges.end());
  std::vector<std::pair<int64_t, int64_t>> merged;
  for (const auto& range : impl.completedRanges) {
    if (merged.empty() || merged.back().second < range.first) {
      merged.push_back(range);
    }
    else {
      merged.back().second = std::max(merged.back().second, range.second);
    }
  }
  impl.completedRanges = std::move(merged);
}

int64_t completedLength(const CurlDownloadImpl& impl)
{
  int64_t result = 0;
  for (const auto& range : impl.completedRanges) {
    result += range.second - range.first;
  }
  return result;
}

int64_t contiguousLength(const CurlDownloadImpl& impl)
{
  return !impl.completedRanges.empty() &&
                 impl.completedRanges.front().first == 0
             ? impl.completedRanges.front().second
             : 0;
}

bool allRangesComplete(const CurlDownloadImpl& impl, int64_t totalLength)
{
  return totalLength > 0 && impl.completedRanges.size() == 1 &&
         impl.completedRanges.front().first == 0 &&
         impl.completedRanges.front().second >= totalLength;
}

int64_t nextMissingOffset(const CurlDownloadImpl& impl, int64_t offset)
{
  for (const auto& range : impl.completedRanges) {
    if (offset < range.first) {
      return offset;
    }
    if (offset < range.second) {
      offset = range.second;
    }
  }
  return offset;
}

int64_t missingRangeEnd(const CurlDownloadImpl& impl, int64_t first,
                        int64_t proposedLast)
{
  for (const auto& range : impl.completedRanges) {
    if (range.first > first && range.first <= proposedLast) {
      return range.first - 1;
    }
  }
  return proposedLast;
}

void cleanupHandle(CurlHandle& handle)
{
  if (handle.headers) {
    curl_slist_free_all(handle.headers);
    handle.headers = nullptr;
  }
  if (handle.value) {
    curl_easy_cleanup(handle.value);
    handle.value = nullptr;
  }
}

void eraseHandle(CurlDownloadImpl& impl, CurlHandle* handle)
{
  impl.handles.erase(
      std::remove_if(impl.handles.begin(), impl.handles.end(),
                     [handle](const std::unique_ptr<CurlHandle>& entry) {
                       return entry.get() == handle;
                     }),
      impl.handles.end());
}

bool retryableFailure(CURLcode result, long responseCode,
                      int fileNotFoundCount, int maxFileNotFound)
{
  if (result == CURLE_REMOTE_FILE_NOT_FOUND) {
    return maxFileNotFound > 0 && fileNotFoundCount < maxFileNotFound;
  }
  if (result == CURLE_HTTP_RETURNED_ERROR) {
    if (responseCode == 404 || result == CURLE_REMOTE_FILE_NOT_FOUND) {
      return maxFileNotFound > 0 && fileNotFoundCount < maxFileNotFound;
    }
    return responseCode == 408 || responseCode == 425 ||
           responseCode == 429 || responseCode == 500 ||
           responseCode == 502 || responseCode == 503 ||
           responseCode == 504;
  }
  switch (result) {
  case CURLE_COULDNT_RESOLVE_HOST:
  case CURLE_COULDNT_CONNECT:
  case CURLE_OPERATION_TIMEDOUT:
  case CURLE_PARTIAL_FILE:
  case CURLE_SEND_ERROR:
  case CURLE_RECV_ERROR:
  case CURLE_GOT_NOTHING:
  case CURLE_HTTP2:
  case CURLE_HTTP2_STREAM:
  case CURLE_AGAIN:
    return true;
  default:
    return false;
  }
}

} // namespace

size_t CurlSession::writeData(char* data, size_t size, size_t count,
                              void* userData)
{
  auto* handle = static_cast<CurlHandle*>(userData);
  auto* download = handle->download;
  auto& impl = *download->impl_;
  const auto length = size * count;
  if (handle->validatorMismatch) {
    return 0;
  }
  if (handle->ranged && handle->headersComplete &&
      !handle->rangeAccepted && handle->rangeStart > 0) {
    return 0;
  }
  if (!impl.file ||
      a2fseek(impl.file, handle->writeOffset, SEEK_SET) != 0 ||
      fwrite(data, 1, length, impl.file) != length) {
    return 0;
  }
  const auto first = handle->writeOffset;
  handle->writeOffset += static_cast<int64_t>(length);
  handle->downloaded += static_cast<int64_t>(length);
  addCompletedRange(impl, first, handle->writeOffset);
  download->snapshot_.completedLength = completedLength(impl);
  download->snapshot_.sessionDownloadLength += static_cast<int64_t>(length);
  if (impl.group) {
    impl.group->getDownloadContext()->updateDownload(length);
  }
  return length;
}

size_t CurlSession::receiveHeader(char* data, size_t size, size_t count,
                                  void* userData)
{
  auto* handle = static_cast<CurlHandle*>(userData);
  auto* download = handle->download;
  auto& impl = *download->impl_;
  const auto length = size * count;
  std::string line(data, length);
  if (startsWithHeader(line, "http/")) {
    std::istringstream status(line);
    std::string version;
    status >> version >> handle->responseCode;
    handle->rangeAccepted = false;
  }
  else if (line == "\r\n" || line == "\n") {
    handle->headersComplete = true;
    if (handle->responseCode == 200 && handle->ranged &&
        !handle->rangeAccepted &&
        handle->rangeStart == 0) {
      handle->ranged = false;
      impl.segmented = false;
      impl.scheduleRanges = false;
    }
  }
  else if (startsWithHeader(line, "etag:")) {
    const auto value = trimHeader(line.substr(5));
    if (!impl.etag.empty() && impl.etag != value) {
      handle->validatorMismatch = true;
    }
    else {
      impl.etag = value;
    }
  }
  else if (startsWithHeader(line, "last-modified:")) {
    const auto value = trimHeader(line.substr(14));
    if (impl.etag.empty() && !impl.lastModified.empty() &&
        impl.lastModified != value) {
      handle->validatorMismatch = true;
    }
    else {
      impl.lastModified = value;
    }
  }
  else if (startsWithHeader(line, "content-range:")) {
    long long first = -1;
    long long last = -1;
    long long total = -1;
    const auto value = trimHeader(line.substr(14));
    if (std::sscanf(value.c_str(), "bytes %lld-%lld/%lld", &first, &last,
                    &total) == 3 &&
        first == handle->rangeStart && last >= first && total > last &&
        (handle->rangeEnd == std::numeric_limits<int64_t>::max() ||
         last <= handle->rangeEnd)) {
      handle->rangeAccepted = true;
      download->snapshot_.totalLength = total;
      if (handle->primary && impl.maxConnections > 1) {
        impl.scheduleRanges = true;
        impl.nextRangeOffset =
            nextMissingOffset(impl, std::min<int64_t>(
                                        total, handle->rangeEnd + 1));
      }
    }
    else {
      handle->validatorMismatch = true;
    }
  }
  return length;
}

int CurlSession::updateProgress(void* userData, curl_off_t downloadTotal,
                                curl_off_t downloaded, curl_off_t, curl_off_t)
{
  auto* handle = static_cast<CurlHandle*>(userData);
  auto* download = handle->download;
  auto& impl = *download->impl_;
  const auto total = handle->ranged
                         ? 0
                         : std::max<curl_off_t>(0, downloadTotal);
  if (total > 0 && download->snapshot_.totalLength == 0) {
    download->snapshot_.totalLength = total;
  }
  (void)downloaded;
  curl_off_t speed = 0;
  if (handle->value &&
      curl_easy_getinfo(handle->value, CURLINFO_SPEED_DOWNLOAD_T, &speed) ==
          CURLE_OK) {
    handle->speed = speed;
    int64_t aggregate = 0;
    for (const auto& entry : impl.handles) {
      aggregate += entry->speed;
    }
    download->snapshot_.downloadSpeed = static_cast<int>(
        std::min<int64_t>(aggregate, std::numeric_limits<int>::max()));
  }
  return impl.stopRequested ? 1 : 0;
}

CurlSession::CurlSession(const Option* option)
    : option_(option),
      globalDownloadLimit_(option->getAsLLInt(PREF_MAX_OVERALL_DOWNLOAD_LIMIT)),
      store_(state::streamDatabaseFile(option))
{
  if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
    return;
  }
  multi_ = curl_multi_init();
  share_ = curl_share_init();
  if (share_) {
    curl_share_setopt(share_, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
    curl_share_setopt(share_, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
    curl_share_setopt(share_, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
  }
  store_.open();
  if (multi_) {
    const auto maxTasks = option_->getAsInt(PREF_MAX_CONCURRENT_DOWNLOADS);
    const auto perTask = option_->getAsInt(PREF_STREAM_MAX_CONNECTIONS);
    const auto maxConnections = maxTasks * perTask;
    curl_multi_setopt(multi_, CURLMOPT_MAX_TOTAL_CONNECTIONS, maxConnections);
    curl_multi_setopt(multi_, CURLMOPT_MAX_HOST_CONNECTIONS, maxConnections);
    curl_multi_setopt(multi_, CURLMOPT_MAXCONNECTS, maxConnections * 2L);
    curl_multi_setopt(multi_, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
    curl_multi_setopt(multi_, CURLMOPT_MAX_CONCURRENT_STREAMS, 100L);
    curl_multi_setopt(multi_, CURLMOPT_SOCKETFUNCTION, socketCallback);
    curl_multi_setopt(multi_, CURLMOPT_SOCKETDATA, this);
    curl_multi_setopt(multi_, CURLMOPT_TIMERFUNCTION, timerCallback);
    curl_multi_setopt(multi_, CURLMOPT_TIMERDATA, this);
  }
}

CurlSession::~CurlSession()
{
  shuttingDown_ = true;
  for (auto& entry : sockets_) {
    entry.second->remove();
  }
  sockets_.clear();
  for (auto& entry : downloads_) {
    checkpoint(entry.second.first, true);
    auto& impl = *entry.second.first->impl_;
    curl_multi_remove_handle(multi_, entry.first);
    cleanupHandle(*entry.second.second);
    if (impl.file) {
      fflush(impl.file);
      fclose(impl.file);
      impl.file = nullptr;
    }
  }
  downloads_.clear();
  if (multi_) {
    curl_multi_cleanup(multi_);
  }
  if (share_) {
    curl_share_cleanup(share_);
  }
  curl_global_cleanup();
}

bool CurlSession::prepare(const std::shared_ptr<CurlDownload>& download,
                          RequestGroup* group)
{
  if (!multi_ || download->impl_->uris.empty()) {
    download->snapshot_.state = CurlSnapshot::State::Error;
    download->snapshot_.error = "The curl transfer session is unavailable";
    return false;
  }
  auto& impl = *download->impl_;
  for (auto& handle : impl.handles) {
    cleanupHandle(*handle);
  }
  impl.handles.clear();
  if (impl.file) {
    fclose(impl.file);
    impl.file = nullptr;
  }
  impl.group = group;
  impl.completedRanges.clear();
  impl.dryRun = group->getOption()->getAsBool(PREF_DRY_RUN);
  impl.stopRequested = false;
  impl.scheduleRanges = false;
  impl.segmented = false;
  impl.restartAttempted = false;
  impl.attempts = 0;
  impl.maxConnections =
      group->getOption()->getAsInt(PREF_STREAM_MAX_CONNECTIONS);
  const auto& uriValue = impl.uris[impl.uriIndex % impl.uris.size()];
  impl.currentUri = uriValue;
  impl.path = outputPath(group, uriValue);
  File directory(File(impl.path).getDirname());
  if (!directory.isDir() && !directory.mkdirs()) {
    download->snapshot_.state = CurlSnapshot::State::Error;
    download->snapshot_.error = "Unable to create the output directory";
    return false;
  }

  auto context = group->getDownloadContext();
  context->setBasePath(impl.path);
  context->getFirstFileEntry()->setPath(impl.path);

  StreamState state;
  const auto gid = GroupId::toHex(group->getGID());
  const auto hasState = store_.load(state, gid, impl.path) &&
                        std::find(impl.uris.begin(), impl.uris.end(),
                                  state.uri) != impl.uris.end() &&
                        state.path == impl.path;
  if (hasState) {
    const auto restored =
        std::find(impl.uris.begin(), impl.uris.end(), state.uri);
    impl.uriIndex = static_cast<size_t>(restored - impl.uris.begin());
    impl.currentUri = *restored;
  }
  uri::UriStruct parsed;
  impl.http = uri::parse(parsed, impl.currentUri) &&
              (util::strieq(parsed.protocol, "http") ||
               util::strieq(parsed.protocol, "https"));
  File output(impl.path);
  auto existingLength = output.isFile() ? output.size() : 0;
  if (!hasState && existingLength > 0 &&
      !group->getOption()->getAsBool(PREF_CONTINUE) &&
      !group->getOption()->getAsBool(PREF_ALLOW_OVERWRITE)) {
    group->shouldCancelDownloadForSafety();
    impl.path = group->getFirstFilePath();
    output = File(impl.path);
    existingLength = output.isFile() ? output.size() : 0;
  }
  impl.resumeOffset = 0;
  if (hasState && existingLength >= state.completedLength) {
    impl.completedRanges = state.completedRanges;
    if (impl.completedRanges.empty() && state.completedLength > 0) {
      addCompletedRange(impl, 0, state.completedLength);
    }
    if (!impl.completedRanges.empty() &&
        impl.completedRanges.front().first == 0) {
      impl.resumeOffset = impl.completedRanges.front().second;
    }
    impl.etag = state.etag;
    impl.lastModified = state.lastModified;
    download->snapshot_.totalLength = state.totalLength;
  }
  else if (output.isFile() && group->getOption()->getAsBool(PREF_CONTINUE)) {
    impl.resumeOffset = existingLength;
  }
  if (impl.dryRun) {
    impl.resumeOffset = 0;
    download->snapshot_.completedLength = 0;
    download->snapshot_.sessionDownloadLength = 0;
    download->snapshot_.downloadSpeed = 0;
    download->snapshot_.error.clear();
    download->snapshot_.state = CurlSnapshot::State::Active;
    return true;
  }

  impl.file = fopen(impl.path.c_str(), impl.resumeOffset > 0 ? "r+b" : "wb");
  if (!impl.file ||
      (impl.resumeOffset > 0 &&
       ((!hasState &&
         a2ftruncate(fileno(impl.file), impl.resumeOffset) != 0) ||
        a2fseek(impl.file, impl.resumeOffset, SEEK_SET) != 0))) {
    if (impl.file) {
      fclose(impl.file);
      impl.file = nullptr;
    }
    download->snapshot_.state = CurlSnapshot::State::Error;
    download->snapshot_.error = "Unable to open the output file";
    return false;
  }
  impl.resumed = impl.resumeOffset > 0;
  if (impl.resumeOffset > 0 && impl.completedRanges.empty()) {
    addCompletedRange(impl, 0, impl.resumeOffset);
  }
  download->snapshot_.completedLength = completedLength(impl);
  download->snapshot_.sessionDownloadLength = 0;
  download->snapshot_.downloadSpeed = 0;
  download->snapshot_.error.clear();
  download->snapshot_.state = CurlSnapshot::State::Active;
  return true;
}

bool CurlSession::createHandle(const std::shared_ptr<CurlDownload>& download,
                               int64_t rangeStart, int64_t rangeEnd,
                               bool primary)
{
  auto& impl = *download->impl_;
  const auto taskOption = impl.group->getOption().get();
  auto transfer = std::unique_ptr<CurlHandle>(new CurlHandle());
  transfer->download = download.get();
  transfer->rangeStart = rangeStart;
  transfer->rangeEnd = rangeEnd;
  transfer->writeOffset = rangeStart;
  transfer->primary = primary;
  transfer->ranged =
      impl.http && rangeEnd >= rangeStart && !impl.dryRun;
  auto* easy = curl_easy_init();
  if (!easy) {
    return false;
  }
  transfer->value = easy;
  auto*& headers = transfer->headers;
  const auto& uriValue = impl.currentUri;
  download->snapshot_.currentUri = uriValue;
  markUriUsed(impl.group, uriValue);
  curl_easy_setopt(easy, CURLOPT_URL, uriValue.c_str());
  if (share_) {
    curl_easy_setopt(easy, CURLOPT_SHARE, share_);
  }
  curl_easy_setopt(easy, CURLOPT_PRIVATE, transfer.get());
  curl_easy_setopt(easy, CURLOPT_PROTOCOLS_STR, "http,https,sftp");
  curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS_STR, "http,https,sftp");
  curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(easy, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(easy, CURLOPT_MAXREDIRS, 10L);
  curl_easy_setopt(easy, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
  curl_easy_setopt(easy, CURLOPT_PIPEWAIT, primary ? 0L : 1L);
  curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(easy, CURLOPT_BUFFERSIZE, 1024L * 1024L);
  curl_easy_setopt(easy, CURLOPT_TCP_NODELAY, 1L);
  curl_easy_setopt(easy, CURLOPT_TCP_KEEPALIVE, 1L);
  curl_easy_setopt(easy, CURLOPT_DNS_CACHE_TIMEOUT, 300L);
  curl_easy_setopt(easy, CURLOPT_UPKEEP_INTERVAL_MS, 30000L);
  curl_easy_setopt(easy, CURLOPT_SOCKOPTFUNCTION, socketOptionCallback);
  curl_easy_setopt(easy, CURLOPT_SOCKOPTDATA,
                   const_cast<Option*>(taskOption));
  curl_easy_setopt(easy, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
  curl_easy_setopt(easy, CURLOPT_NOBODY, impl.dryRun ? 1L : 0L);
  curl_easy_setopt(easy, CURLOPT_FILETIME,
                   taskOption->getAsBool(PREF_REMOTE_TIME) ? 1L : 0L);
  curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, writeData);
  curl_easy_setopt(easy, CURLOPT_WRITEDATA, transfer.get());
  curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, receiveHeader);
  curl_easy_setopt(easy, CURLOPT_HEADERDATA, transfer.get());
  curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, updateProgress);
  curl_easy_setopt(easy, CURLOPT_XFERINFODATA, transfer.get());
  curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT,
                   taskOption->getAsInt(PREF_CONNECT_TIMEOUT));
  curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT,
                   std::max(1, taskOption->getAsInt(PREF_LOWEST_SPEED_LIMIT)));
  curl_easy_setopt(easy, CURLOPT_LOW_SPEED_TIME,
                   taskOption->getAsInt(PREF_TIMEOUT));
  curl_easy_setopt(easy, CURLOPT_USERAGENT,
                   taskOption->get(PREF_USER_AGENT).c_str());
  curl_easy_setopt(easy, CURLOPT_FORBID_REUSE,
                   taskOption->getAsBool(PREF_ENABLE_HTTP_KEEP_ALIVE) ? 0L
                                                                      : 1L);
  if (taskOption->getAsBool(PREF_HTTP_ACCEPT_GZIP) && !transfer->ranged) {
    curl_easy_setopt(easy, CURLOPT_ACCEPT_ENCODING, "");
  }
  if (taskOption->getAsBool(PREF_HTTP_NO_CACHE)) {
    headers = curl_slist_append(headers, "Cache-Control: no-cache");
    headers = curl_slist_append(headers, "Pragma: no-cache");
  }
  curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER,
                   taskOption->getAsBool(PREF_CHECK_CERTIFICATE) ? 1L : 0L);
  curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST,
                   taskOption->getAsBool(PREF_CHECK_CERTIFICATE) ? 2L : 0L);
  curl_easy_setopt(easy, CURLOPT_PROXY_SSL_VERIFYPEER,
                   taskOption->getAsBool(PREF_CHECK_CERTIFICATE) ? 1L : 0L);
  curl_easy_setopt(easy, CURLOPT_PROXY_SSL_VERIFYHOST,
                   taskOption->getAsBool(PREF_CHECK_CERTIFICATE) ? 2L : 0L);
  const auto& minimumTls = taskOption->get(PREF_MIN_TLS_VERSION);
  const long sslVersion =
      minimumTls == A2_V_TLS13 ? CURL_SSLVERSION_TLSv1_3
      : minimumTls == A2_V_TLS12 ? CURL_SSLVERSION_TLSv1_2
                                 : CURL_SSLVERSION_TLSv1_1;
  curl_easy_setopt(easy, CURLOPT_SSLVERSION, sslVersion);
  if (!taskOption->blank(PREF_INTERFACE)) {
    curl_easy_setopt(easy, CURLOPT_INTERFACE,
                     taskOption->get(PREF_INTERFACE).c_str());
  }
  if (taskOption->getAsBool(PREF_DISABLE_IPV6)) {
    curl_easy_setopt(easy, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
  }
  if (!taskOption->blank(PREF_CA_CERTIFICATE)) {
    curl_easy_setopt(easy, CURLOPT_CAINFO,
                     taskOption->get(PREF_CA_CERTIFICATE).c_str());
  }
  if (!taskOption->blank(PREF_CERTIFICATE)) {
    curl_easy_setopt(easy, CURLOPT_SSLCERT,
                     taskOption->get(PREF_CERTIFICATE).c_str());
  }
  if (!taskOption->blank(PREF_PRIVATE_KEY)) {
    curl_easy_setopt(easy, CURLOPT_SSLKEY,
                     taskOption->get(PREF_PRIVATE_KEY).c_str());
    curl_easy_setopt(easy, CURLOPT_SSH_PRIVATE_KEYFILE,
                     taskOption->get(PREF_PRIVATE_KEY).c_str());
  }
  if (impl.http && !taskOption->blank(PREF_HTTP_USER)) {
    curl_easy_setopt(easy, CURLOPT_USERNAME,
                     taskOption->get(PREF_HTTP_USER).c_str());
    curl_easy_setopt(easy, CURLOPT_PASSWORD,
                     taskOption->get(PREF_HTTP_PASSWD).c_str());
  }
  else if (!impl.http && !taskOption->blank(PREF_SFTP_USER)) {
    curl_easy_setopt(easy, CURLOPT_USERNAME,
                     taskOption->get(PREF_SFTP_USER).c_str());
    curl_easy_setopt(easy, CURLOPT_PASSWORD,
                     taskOption->get(PREF_SFTP_PASSWD).c_str());
  }
  else if (!taskOption->getAsBool(PREF_NO_NETRC)) {
    curl_easy_setopt(easy, CURLOPT_NETRC, CURL_NETRC_OPTIONAL);
    if (!taskOption->blank(PREF_NETRC_PATH)) {
      curl_easy_setopt(easy, CURLOPT_NETRC_FILE,
                       taskOption->get(PREF_NETRC_PATH).c_str());
    }
  }
  if (!impl.http && !taskOption->blank(PREF_SSH_HOST_KEY_SHA256)) {
    curl_easy_setopt(easy, CURLOPT_SSH_HOST_PUBLIC_KEY_SHA256,
                     taskOption->get(PREF_SSH_HOST_KEY_SHA256).c_str());
  }
  if (!taskOption->blank(PREF_REFERER)) {
    curl_easy_setopt(easy, CURLOPT_REFERER,
                     taskOption->get(PREF_REFERER).c_str());
  }
  const auto proxy = proxyFor(taskOption, uriValue);
  if (!proxy.empty()) {
    curl_easy_setopt(easy, CURLOPT_PROXY, proxy.c_str());
    uri::UriStruct parsed;
    uri::parse(parsed, uriValue);
    const auto proxyUser = proxyUserFor(taskOption, parsed.protocol);
    const auto proxyPassword = proxyPasswordFor(taskOption, parsed.protocol);
    if (!proxyUser.empty()) {
      curl_easy_setopt(easy, CURLOPT_PROXYUSERNAME, proxyUser.c_str());
      curl_easy_setopt(easy, CURLOPT_PROXYPASSWORD,
                       proxyPassword.c_str());
    }
  }
  if (!taskOption->blank(PREF_NO_PROXY)) {
    curl_easy_setopt(easy, CURLOPT_NOPROXY,
                     taskOption->get(PREF_NO_PROXY).c_str());
  }
  if (impl.http) {
    curl_easy_setopt(easy, CURLOPT_COOKIEFILE, "");
  }
  if (!taskOption->blank(PREF_LOAD_COOKIES)) {
    curl_easy_setopt(easy, CURLOPT_COOKIEFILE,
                     taskOption->get(PREF_LOAD_COOKIES).c_str());
  }
  if (!taskOption->blank(PREF_SAVE_COOKIES)) {
    curl_easy_setopt(easy, CURLOPT_COOKIEJAR,
                     taskOption->get(PREF_SAVE_COOKIES).c_str());
  }
  std::istringstream configuredHeaders(taskOption->get(PREF_HEADER));
  std::string header;
  while (std::getline(configuredHeaders, header)) {
    if (!header.empty()) {
      headers = curl_slist_append(headers, header.c_str());
    }
  }
  if (impl.http && rangeEnd >= rangeStart && !impl.dryRun) {
    transfer->range =
        rangeEnd == std::numeric_limits<int64_t>::max()
            ? std::to_string(rangeStart) + '-'
            : std::to_string(rangeStart) + '-' + std::to_string(rangeEnd);
    curl_easy_setopt(easy, CURLOPT_RANGE, transfer->range.c_str());
    const auto& validator = !impl.etag.empty() ? impl.etag : impl.lastModified;
    if (rangeStart > 0 && !validator.empty()) {
      const auto ifRange = "If-Range: " + validator;
      headers = curl_slist_append(headers, ifRange.c_str());
    }
  }
  else if (impl.resumeOffset > 0) {
    curl_easy_setopt(easy, CURLOPT_RESUME_FROM_LARGE,
                     static_cast<curl_off_t>(impl.resumeOffset));
    transfer->rangeStart = impl.resumeOffset;
    transfer->writeOffset = impl.resumeOffset;
  }
  if (headers) {
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers);
  }
  const auto result = curl_multi_add_handle(multi_, easy) == CURLM_OK;
  if (result) {
    impl.handles.push_back(std::move(transfer));
    download->snapshot_.connections =
        static_cast<int>(impl.handles.size());
  }
  else {
    cleanupHandle(*transfer);
  }
  return result;
}

std::unique_ptr<Command>
CurlSession::start(const std::shared_ptr<CurlDownload>& download,
                   RequestGroup* group, DownloadEngine* engine)
{
  engine_ = engine;
  constexpr int64_t probeSize = 4_m;
  if (prepare(download, group)) {
    const auto rangeStart = download->impl_->resumeOffset;
    const auto rangeEnd =
        download->impl_->maxConnections > 1
            ? missingRangeEnd(*download->impl_, rangeStart,
                              rangeStart + probeSize - 1)
            : rangeStart > 0 ? std::numeric_limits<int64_t>::max() : -1;
    if (!createHandle(download, rangeStart, rangeEnd, true)) {
      download->snapshot_.state = CurlSnapshot::State::Error;
      download->snapshot_.error = "Unable to start the curl transfer";
      return std::unique_ptr<Command>(new CurlDownloadCommand(
          engine->newCUID(), download, this, group, engine));
    }
    auto* handle = download->impl_->handles.back().get();
    downloads_[handle->value] = std::make_pair(download, handle);
    rebalanceLimits();
    socketAction(CURL_SOCKET_TIMEOUT, 0);
  }
  else if (!download->failed()) {
    download->snapshot_.state = CurlSnapshot::State::Error;
    download->snapshot_.error = "Unable to start the curl transfer";
  }
  return std::unique_ptr<Command>(new CurlDownloadCommand(
      engine->newCUID(), download, this, group, engine));
}

void CurlSession::checkpoint(const std::shared_ptr<CurlDownload>& download,
                             bool force)
{
  auto& impl = *download->impl_;
  if (!impl.group || impl.dryRun ||
      (!force && !impl.lastCheckpoint.isZero() &&
       impl.lastCheckpoint.difference(global::wallclock()) <
           std::chrono::seconds(1))) {
    return;
  }
  if (impl.file) {
    fflush(impl.file);
  }
  StreamState state;
  state.gid = GroupId::toHex(impl.group->getGID());
  state.uri = impl.currentUri;
  state.path = impl.path;
  state.etag = impl.etag;
  state.lastModified = impl.lastModified;
  state.totalLength = download->snapshot_.totalLength;
  state.completedLength = download->snapshot_.completedLength;
  state.completedRanges = impl.completedRanges;
  if (store_.save(state)) {
    impl.lastCheckpoint = global::wallclock();
  }
}

void CurlSession::scheduleRanges(
    const std::shared_ptr<CurlDownload>& download)
{
  auto& impl = *download->impl_;
  const auto total = download->snapshot_.totalLength;
  if (!impl.scheduleRanges || total <= 0 || impl.maxConnections <= 1 ||
      impl.stopRequested) {
    return;
  }
  if (!impl.segmented) {
    impl.segmented = true;
    if (impl.nextRangeOffset <= impl.resumeOffset) {
      impl.nextRangeOffset =
          nextMissingOffset(impl, std::min<int64_t>(
                                      total, impl.resumeOffset + 4_m));
    }
    const auto target = (total + impl.maxConnections * 4 - 1) /
                        (impl.maxConnections * 4);
    impl.rangeChunkSize =
        std::max<int64_t>(4_m, std::min<int64_t>(32_m, target));
    impl.rangeChunkSize =
        ((impl.rangeChunkSize + 1_m - 1) / 1_m) * 1_m;
    const auto taskOption = impl.group->getOption();
    const bool sizeOutput =
        taskOption->get(PREF_FILE_ALLOCATION) != V_NONE &&
        total >= taskOption->getAsLLInt(PREF_NO_FILE_ALLOCATION_LIMIT);
    if (sizeOutput && impl.file &&
        a2ftruncate(fileno(impl.file), total) != 0) {
      download->snapshot_.state = CurlSnapshot::State::Error;
      download->snapshot_.error = "Unable to size the output file";
      cancelHandles(download);
      return;
    }
  }

  size_t active = 0;
  for (const auto& entry : downloads_) {
    if (entry.second.first.get() == download.get()) {
      ++active;
    }
  }
  while (active < static_cast<size_t>(impl.maxConnections) &&
         impl.nextRangeOffset < total) {
    const auto first = nextMissingOffset(impl, impl.nextRangeOffset);
    if (first >= total) {
      impl.nextRangeOffset = total;
      break;
    }
    const auto last = missingRangeEnd(
        impl, first,
        std::min<int64_t>(total - 1, first + impl.rangeChunkSize - 1));
    if (!createHandle(download, first, last, false)) {
      download->snapshot_.state = CurlSnapshot::State::Error;
      download->snapshot_.error = "Unable to create a ranged transfer";
      cancelHandles(download);
      return;
    }
    auto* handle = impl.handles.back().get();
    downloads_[handle->value] = std::make_pair(download, handle);
    impl.nextRangeOffset = last + 1;
    ++active;
  }
  rebalanceLimits();
}

void CurlSession::cancelHandles(
    const std::shared_ptr<CurlDownload>& download)
{
  auto& impl = *download->impl_;
  for (auto& handle : impl.handles) {
    if (handle->value) {
      auto found = downloads_.find(handle->value);
      if (found != downloads_.end()) {
        curl_multi_remove_handle(multi_, handle->value);
        downloads_.erase(found);
      }
      cleanupHandle(*handle);
    }
  }
  impl.handles.clear();
  download->snapshot_.connections = 0;
}

void CurlSession::finalize(const std::shared_ptr<CurlDownload>& download,
                           curl_off_t reportedFileTime)
{
  auto& impl = *download->impl_;
  if (impl.file) {
    fflush(impl.file);
    fclose(impl.file);
    impl.file = nullptr;
  }
  auto length = File(impl.path).size();
  if (download->snapshot_.totalLength > 0) {
    length = download->snapshot_.totalLength;
  }
  if (reportedFileTime >= 0 &&
      impl.group->getOption()->getAsBool(PREF_REMOTE_TIME)) {
    File(impl.path).utime(Time(), Time(static_cast<time_t>(reportedFileTime)));
  }
  download->snapshot_.completedLength = length;
  download->snapshot_.totalLength = length;
  auto context = impl.group->getDownloadContext();
  context->getFirstFileEntry()->setLength(length);
  context->markTotalLengthIsKnown();
  if (!impl.group->getPieceStorage()) {
    impl.group->initPieceStorage();
    impl.group->getPieceStorage()->getDiskAdaptor()->openExistingFile();
  }
  impl.group->getPieceStorage()->markAllPiecesDone();
  context->resetDownloadStopTime();
  store_.removePath(impl.path);
  download->snapshot_.state = CurlSnapshot::State::Complete;
  download->snapshot_.downloadSpeed = 0;
  if (context->isPieceHashVerificationAvailable()) {
    auto entry = std::unique_ptr<CurlCheckIntegrityEntry>(
        new CurlCheckIntegrityEntry(impl.group));
    entry->initValidator();
    engine_->getCheckIntegrityMan()->pushEntry(std::move(entry));
  }
  else if (context->isChecksumVerificationAvailable()) {
    auto entry = std::unique_ptr<ChecksumCheckIntegrityEntry>(
        new ChecksumCheckIntegrityEntry(impl.group));
    entry->initValidator();
    engine_->getCheckIntegrityMan()->pushEntry(std::move(entry));
  }
  engine_->setNoWait(true);
  engine_->setRefreshInterval(std::chrono::milliseconds(0));
}

bool CurlSession::restartWithoutResume(
    const std::shared_ptr<CurlDownload>& download)
{
  auto& impl = *download->impl_;
  if (impl.restartAttempted) {
    return false;
  }
  impl.restartAttempted = true;
  if (impl.file) {
    fclose(impl.file);
    impl.file = nullptr;
  }
  cancelHandles(download);
  impl.resumeOffset = 0;
  impl.resumed = false;
  impl.completedRanges.clear();
  impl.scheduleRanges = false;
  impl.segmented = false;
  impl.etag.clear();
  impl.lastModified.clear();
  impl.file = fopen(impl.path.c_str(), "wb");
  download->snapshot_.totalLength = 0;
  download->snapshot_.completedLength = 0;
  download->snapshot_.sessionDownloadLength = 0;
  store_.removePath(impl.path);
  const auto rangeEnd =
      impl.maxConnections > 1 ? 4_m - 1 : -1;
  if (!impl.file || !createHandle(download, 0, rangeEnd, true)) {
    return false;
  }
  auto* handle = impl.handles.back().get();
  downloads_[handle->value] = std::make_pair(download, handle);
  return true;
}

bool CurlSession::retry(const std::shared_ptr<CurlDownload>& download)
{
  auto& impl = *download->impl_;
  const auto maxTries = impl.group->getOption()->getAsInt(PREF_MAX_TRIES);
  const auto limit = maxTries > 0 ? static_cast<size_t>(maxTries)
                                  : std::numeric_limits<size_t>::max();
  if (++impl.attempts >= limit) {
    return false;
  }
  if (impl.uris.empty()) {
    return false;
  }
  if (impl.file) {
    fflush(impl.file);
    fclose(impl.file);
    impl.file = nullptr;
  }
  cancelHandles(download);
  impl.uriIndex = (impl.uriIndex + 1) % impl.uris.size();
  impl.currentUri = impl.uris[impl.uriIndex];
  impl.resumeOffset = contiguousLength(impl);
  impl.resumed = impl.resumeOffset > 0;
  impl.scheduleRanges = false;
  impl.segmented = false;
  const auto configured =
      impl.group->getOption()->getAsInt(PREF_RETRY_WAIT);
  const auto wait = std::max<long>(configured, impl.pendingRetryAfter);
  impl.pendingRetryAfter = 0;
  impl.retryPending = true;
  impl.retryDeadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(wait);
  download->snapshot_.downloadSpeed = 0;
  download->snapshot_.connections = 0;
  engine_->setNoWait(true);
  return true;
}

void CurlSession::processRetry(
    const std::shared_ptr<CurlDownload>& download)
{
  auto& impl = *download->impl_;
  if (!impl.retryPending) {
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  if (now < impl.retryDeadline) {
    engine_->setRefreshInterval(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            impl.retryDeadline - now));
    return;
  }
  impl.retryPending = false;
  if (impl.dryRun) {
    if (!createHandle(download, 0, -1, true)) {
      download->snapshot_.state = CurlSnapshot::State::Error;
      download->snapshot_.error = "Unable to restart the curl transfer";
      return;
    }
    auto* handle = impl.handles.back().get();
    downloads_[handle->value] = std::make_pair(download, handle);
    socketAction(CURL_SOCKET_TIMEOUT, 0);
    return;
  }
  impl.file = fopen(impl.path.c_str(), impl.resumed ? "r+b" : "wb");
  if (!impl.file ||
      (impl.resumed &&
       a2fseek(impl.file, impl.resumeOffset, SEEK_SET) != 0)) {
    download->snapshot_.state = CurlSnapshot::State::Error;
    download->snapshot_.error = "Unable to reopen the output file";
    return;
  }
  const auto rangeEnd =
      impl.maxConnections > 1
          ? missingRangeEnd(impl, impl.resumeOffset,
                            impl.resumeOffset + 4_m - 1)
          : impl.resumeOffset > 0
                ? std::numeric_limits<int64_t>::max()
                : -1;
  if (!createHandle(download, impl.resumeOffset, rangeEnd, true)) {
    download->snapshot_.state = CurlSnapshot::State::Error;
    download->snapshot_.error = "Unable to restart the curl transfer";
    return;
  }
  auto* handle = impl.handles.back().get();
  downloads_[handle->value] = std::make_pair(download, handle);
  rebalanceLimits();
  socketAction(CURL_SOCKET_TIMEOUT, 0);
}

void CurlSession::advance(const std::shared_ptr<CurlDownload>& download)
{
  processRetry(download);
  scheduleRanges(download);
}

void CurlSession::finish(const std::shared_ptr<CurlDownload>& download,
                         CurlHandle* handle, CURLcode result)
{
  auto& impl = *download->impl_;
  const auto responseCode = handle->responseCode;
  curl_off_t retryAfter = 0;
  if (handle->value) {
    curl_easy_getinfo(handle->value, CURLINFO_RETRY_AFTER, &retryAfter);
  }
  const bool rejectedResume =
      handle->rangeStart > 0 && impl.http && !handle->rangeAccepted;
  const bool rangeUnsupported =
      responseCode == 416 && handle->rangeStart == 0;
  if (rangeUnsupported) {
    impl.maxConnections = 1;
  }
  if ((result == CURLE_RANGE_ERROR || rangeUnsupported || rejectedResume ||
       handle->validatorMismatch) &&
      restartWithoutResume(download)) {
    rebalanceLimits();
    return;
  }
  if (result != CURLE_OK) {
    if (responseCode == 404) {
      ++impl.fileNotFoundCount;
    }
    impl.pendingRetryAfter = std::max<curl_off_t>(0, retryAfter);
    const auto maxFileNotFound =
        impl.group->getOption()->getAsInt(PREF_MAX_FILE_NOT_FOUND);
    const bool alternateMirror =
        impl.uris.size() > 1 && result != CURLE_WRITE_ERROR &&
        result != CURLE_OUT_OF_MEMORY && result != CURLE_ABORTED_BY_CALLBACK;
    if ((alternateMirror ||
         retryableFailure(result, responseCode, impl.fileNotFoundCount,
                          maxFileNotFound)) &&
        retry(download)) {
      rebalanceLimits();
      return;
    }
    cancelHandles(download);
    checkpoint(download, true);
    download->snapshot_.state = CurlSnapshot::State::Error;
    download->snapshot_.error =
        responseCode > 0
            ? "HTTP " + std::to_string(responseCode) + ": " +
                  curl_easy_strerror(result)
            : curl_easy_strerror(result);
    download->snapshot_.downloadSpeed = 0;
    return;
  }

  curl_off_t reportedLength = 0;
  curl_off_t reportedFileTime = -1;
  if (handle->value) {
    curl_easy_getinfo(handle->value, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T,
                      &reportedLength);
    curl_easy_getinfo(handle->value, CURLINFO_FILETIME_T, &reportedFileTime);
  }

  const bool ranged = handle->ranged;
  const bool rangeAccepted = handle->rangeAccepted;
  cleanupHandle(*handle);
  eraseHandle(impl, handle);
  download->snapshot_.connections =
      static_cast<int>(impl.handles.size());
  if (impl.dryRun) {
    download->snapshot_.totalLength = std::max<curl_off_t>(0, reportedLength);
    download->snapshot_.state = CurlSnapshot::State::Complete;
    download->snapshot_.downloadSpeed = 0;
    store_.removePath(impl.path);
    return;
  }

  if (ranged && rangeAccepted) {
    scheduleRanges(download);
    if (!allRangesComplete(impl, download->snapshot_.totalLength)) {
      return;
    }
    cancelHandles(download);
  }
  finalize(download, reportedFileTime);
}

void CurlSession::poll()
{
  if (!multi_) {
    return;
  }
  rebalanceLimits();
  for (const auto& entry : downloads_) {
    checkpoint(entry.second.first, false);
  }
  if (timeoutArmed_ &&
      std::chrono::steady_clock::now() >= timeoutDeadline_) {
    timeoutArmed_ = false;
    socketAction(CURL_SOCKET_TIMEOUT, 0);
  }
}

void CurlSession::armTimeout()
{
  if (!timeoutArmed_ || !engine_) {
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
      timeoutDeadline_ - now);
  if (remaining.count() <= 0) {
    engine_->setNoWait(true);
    remaining = std::chrono::milliseconds(0);
  }
  engine_->setRefreshInterval(remaining);
}

void CurlSession::socketAction(curl_socket_t socket, int events)
{
  if (!multi_) {
    return;
  }
  int running = 0;
  const auto result = curl_multi_socket_action(multi_, socket, events, &running);
  if (result != CURLM_OK && result != CURLM_BAD_SOCKET) {
    A2_LOG_ERROR(fmt("libcurl multi socket action failed: %s",
                     curl_multi_strerror(result)));
  }
  processMessages();
}

void CurlSession::processMessages()
{
  int remaining = 0;
  while (auto* message = curl_multi_info_read(multi_, &remaining)) {
    if (message->msg != CURLMSG_DONE) {
      continue;
    }
    auto found = downloads_.find(message->easy_handle);
    if (found == downloads_.end()) {
      continue;
    }
    auto download = found->second.first;
    auto* handle = found->second.second;
    curl_multi_remove_handle(multi_, message->easy_handle);
    downloads_.erase(found);
    rebalanceLimits();
    finish(download, handle, message->data.result);
  }
}

void CurlSession::updateSocket(curl_socket_t socket, int action,
                               CurlSocketCommand* command)
{
  if (!engine_ || shuttingDown_) {
    return;
  }
  if (!command) {
    auto next = std::unique_ptr<CurlSocketCommand>(new CurlSocketCommand(
        engine_->newCUID(), socket, this, engine_));
    command = next.get();
    sockets_[socket] = command;
    curl_multi_assign(multi_, socket, command);
    command->update(action);
    engine_->addCommand(std::move(next));
    return;
  }
  command->update(action);
}

void CurlSession::removeSocket(curl_socket_t socket,
                               CurlSocketCommand* command)
{
  if (command) {
    command->remove();
  }
  sockets_.erase(socket);
  if (multi_) {
    curl_multi_assign(multi_, socket, nullptr);
  }
}

void CurlSession::updateTimeout(long timeoutMs)
{
  if (timeoutMs < 0) {
    timeoutArmed_ = false;
    return;
  }
  timeoutArmed_ = true;
  timeoutDeadline_ = std::chrono::steady_clock::now() +
                     std::chrono::milliseconds(timeoutMs);
  armTimeout();
}

int CurlSession::socketCallback(CURL*, curl_socket_t socket, int action,
                                void* userData, void* socketData)
{
  auto* session = static_cast<CurlSession*>(userData);
  auto* command = static_cast<CurlSocketCommand*>(socketData);
  if (action == CURL_POLL_REMOVE) {
    session->removeSocket(socket, command);
  }
  else {
    session->updateSocket(socket, action, command);
  }
  return 0;
}

int CurlSession::timerCallback(CURLM*, long timeoutMs, void* userData)
{
  static_cast<CurlSession*>(userData)->updateTimeout(timeoutMs);
  return 0;
}

int CurlSession::socketOptionCallback(void* userData, curl_socket_t socket,
                                      curlsocktype)
{
  const auto* option = static_cast<const Option*>(userData);
  const auto receiveBuffer = option->getAsInt(PREF_SOCKET_RECV_BUFFER_SIZE);
  if (receiveBuffer > 0) {
    setsockopt(socket, SOL_SOCKET, SO_RCVBUF,
               reinterpret_cast<const char*>(&receiveBuffer),
               sizeof(receiveBuffer));
  }
  const auto trafficClass = option->getAsInt(PREF_DSCP) << 2;
  if (trafficClass > 0) {
    setsockopt(socket, IPPROTO_IP, IP_TOS,
               reinterpret_cast<const char*>(&trafficClass),
               sizeof(trafficClass));
#ifdef IPV6_TCLASS
    setsockopt(socket, IPPROTO_IPV6, IPV6_TCLASS,
               reinterpret_cast<const char*>(&trafficClass),
               sizeof(trafficClass));
#endif
  }
  return CURL_SOCKOPT_OK;
}

void CurlSession::stop(const std::shared_ptr<CurlDownload>& download,
                       bool retainState)
{
  auto& impl = *download->impl_;
  impl.stopRequested = true;
  checkpoint(download, true);
  cancelHandles(download);
  rebalanceLimits();
  if (impl.file) {
    fflush(impl.file);
    fclose(impl.file);
    impl.file = nullptr;
  }
  if (retainState) {
    download->snapshot_.state = CurlSnapshot::State::Paused;
  }
  else {
    if (impl.group) {
      store_.removePath(impl.path);
    }
    download->snapshot_.state = CurlSnapshot::State::Stopped;
  }
  download->snapshot_.downloadSpeed = 0;
}

void CurlSession::rebalanceLimits()
{
  if (downloads_.empty()) {
    return;
  }
  std::map<CurlDownload*, size_t> taskHandles;
  for (const auto& entry : downloads_) {
    ++taskHandles[entry.second.first.get()];
  }
  const auto taskShare =
      globalDownloadLimit_ > 0
          ? std::max<int64_t>(
                1, globalDownloadLimit_ /
                       static_cast<int64_t>(taskHandles.size()))
          : 0;
  for (const auto& entry : downloads_) {
    const auto task =
        entry.second.first->impl_->group->getMaxDownloadSpeedLimit();
    const auto taskLimit =
        task > 0 && taskShare > 0 ? std::min<int64_t>(task, taskShare)
        : task > 0                ? static_cast<int64_t>(task)
                                  : taskShare;
    const auto count = taskHandles[entry.second.first.get()];
    const auto limit =
        taskLimit > 0
            ? std::max<int64_t>(1, taskLimit / static_cast<int64_t>(count))
            : 0;
    auto* handle = entry.second.second;
    if (handle->appliedLimit != limit) {
      curl_easy_setopt(entry.first, CURLOPT_MAX_RECV_SPEED_LARGE,
                       static_cast<curl_off_t>(limit));
      handle->appliedLimit = limit;
    }
  }
}

void CurlSession::setGlobalDownloadLimit(int64_t limit)
{
  limit = std::max<int64_t>(0, limit);
  if (globalDownloadLimit_ == limit) {
    return;
  }
  globalDownloadLimit_ = limit;
  rebalanceLimits();
}

} // namespace aria2
