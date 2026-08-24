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

} // namespace

size_t CurlSession::writeData(char* data, size_t size, size_t count,
                              void* userData)
{
  auto* download = static_cast<CurlDownload*>(userData);
  auto& impl = *download->impl_;
  const auto length = size * count;
  if (impl.resumed && impl.http && impl.headersComplete &&
      !impl.rangeAccepted) {
    return 0;
  }
  if (!impl.file || fwrite(data, 1, length, impl.file) != length) {
    return 0;
  }
  download->snapshot_.completedLength += static_cast<int64_t>(length);
  download->snapshot_.sessionDownloadLength += static_cast<int64_t>(length);
  if (impl.group) {
    impl.group->getDownloadContext()->updateDownload(length);
  }
  return length;
}

size_t CurlSession::receiveHeader(char* data, size_t size, size_t count,
                                  void* userData)
{
  auto* download = static_cast<CurlDownload*>(userData);
  auto& impl = *download->impl_;
  const auto length = size * count;
  std::string line(data, length);
  if (line == "\r\n" || line == "\n") {
    impl.headersComplete = true;
  }
  else if (startsWithHeader(line, "etag:")) {
    impl.etag = trimHeader(line.substr(5));
  }
  else if (startsWithHeader(line, "last-modified:")) {
    impl.lastModified = trimHeader(line.substr(14));
  }
  else if (startsWithHeader(line, "content-range:")) {
    impl.rangeAccepted = true;
    const auto slash = line.rfind('/');
    if (slash != std::string::npos) {
      try {
        const auto total = std::stoll(trimHeader(line.substr(slash + 1)));
        if (total > 0) {
          download->snapshot_.totalLength = total;
        }
      }
      catch (const std::exception&) {
      }
    }
  }
  return length;
}

int CurlSession::updateProgress(void* userData, curl_off_t downloadTotal,
                                curl_off_t downloaded, curl_off_t, curl_off_t)
{
  auto* download = static_cast<CurlDownload*>(userData);
  auto& impl = *download->impl_;
  const auto total = impl.resumeOffset + std::max<curl_off_t>(0, downloadTotal);
  if (total > 0 && download->snapshot_.totalLength == 0) {
    download->snapshot_.totalLength = total;
  }
  download->snapshot_.completedLength =
      impl.resumeOffset + std::max<curl_off_t>(0, downloaded);
  curl_off_t speed = 0;
  if (impl.handle && curl_easy_getinfo(impl.handle, CURLINFO_SPEED_DOWNLOAD_T,
                                       &speed) == CURLE_OK) {
    download->snapshot_.downloadSpeed = static_cast<int>(
        std::min<curl_off_t>(speed, std::numeric_limits<int>::max()));
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
  store_.open();
  if (multi_) {
    curl_multi_setopt(multi_, CURLMOPT_MAX_TOTAL_CONNECTIONS,
                      option_->getAsInt(PREF_MAX_CONCURRENT_DOWNLOADS));
  }
}

CurlSession::~CurlSession()
{
  for (auto& entry : downloads_) {
    checkpoint(entry.second, true);
    auto& impl = *entry.second->impl_;
    curl_multi_remove_handle(multi_, entry.first);
    if (impl.file) {
      fflush(impl.file);
      fclose(impl.file);
      impl.file = nullptr;
    }
    if (impl.headers) {
      curl_slist_free_all(impl.headers);
      impl.headers = nullptr;
    }
    curl_easy_cleanup(entry.first);
    impl.handle = nullptr;
  }
  downloads_.clear();
  if (multi_) {
    curl_multi_cleanup(multi_);
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
  if (impl.headers) {
    curl_slist_free_all(impl.headers);
    impl.headers = nullptr;
  }
  if (impl.handle) {
    curl_easy_cleanup(impl.handle);
    impl.handle = nullptr;
  }
  if (impl.file) {
    fclose(impl.file);
    impl.file = nullptr;
  }
  impl.group = group;
  impl.dryRun = group->getOption()->getAsBool(PREF_DRY_RUN);
  impl.stopRequested = false;
  impl.headersComplete = false;
  impl.rangeAccepted = false;
  impl.restartAttempted = false;
  impl.attempts = 0;
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
    impl.resumeOffset = state.completedLength;
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
  if (!impl.file || (impl.resumeOffset > 0 &&
                     (a2ftruncate(fileno(impl.file), impl.resumeOffset) != 0 ||
                      fseeko(impl.file, impl.resumeOffset, SEEK_SET) != 0))) {
    if (impl.file) {
      fclose(impl.file);
      impl.file = nullptr;
    }
    download->snapshot_.state = CurlSnapshot::State::Error;
    download->snapshot_.error = "Unable to open the output file";
    return false;
  }
  impl.resumed = impl.resumeOffset > 0;
  download->snapshot_.completedLength = impl.resumeOffset;
  download->snapshot_.sessionDownloadLength = 0;
  download->snapshot_.downloadSpeed = 0;
  download->snapshot_.error.clear();
  download->snapshot_.state = CurlSnapshot::State::Active;
  return true;
}

bool CurlSession::createHandle(const std::shared_ptr<CurlDownload>& download)
{
  auto& impl = *download->impl_;
  const auto taskOption = impl.group->getOption().get();
  impl.handle = curl_easy_init();
  if (!impl.handle) {
    return false;
  }
  const auto& uriValue = impl.currentUri;
  download->snapshot_.currentUri = uriValue;
  markUriUsed(impl.group, uriValue);
  curl_easy_setopt(impl.handle, CURLOPT_URL, uriValue.c_str());
  curl_easy_setopt(impl.handle, CURLOPT_PRIVATE, download.get());
  curl_easy_setopt(impl.handle, CURLOPT_PROTOCOLS_STR, "http,https,sftp");
  curl_easy_setopt(impl.handle, CURLOPT_REDIR_PROTOCOLS_STR, "http,https,sftp");
  curl_easy_setopt(impl.handle, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(impl.handle, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(impl.handle, CURLOPT_MAXREDIRS, 10L);
  curl_easy_setopt(impl.handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
  curl_easy_setopt(impl.handle, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
  curl_easy_setopt(impl.handle, CURLOPT_NOBODY, impl.dryRun ? 1L : 0L);
  curl_easy_setopt(impl.handle, CURLOPT_FILETIME,
                   taskOption->getAsBool(PREF_REMOTE_TIME) ? 1L : 0L);
  curl_easy_setopt(impl.handle, CURLOPT_WRITEFUNCTION, writeData);
  curl_easy_setopt(impl.handle, CURLOPT_WRITEDATA, download.get());
  curl_easy_setopt(impl.handle, CURLOPT_HEADERFUNCTION, receiveHeader);
  curl_easy_setopt(impl.handle, CURLOPT_HEADERDATA, download.get());
  curl_easy_setopt(impl.handle, CURLOPT_XFERINFOFUNCTION, updateProgress);
  curl_easy_setopt(impl.handle, CURLOPT_XFERINFODATA, download.get());
  curl_easy_setopt(impl.handle, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(impl.handle, CURLOPT_CONNECTTIMEOUT,
                   taskOption->getAsInt(PREF_CONNECT_TIMEOUT));
  curl_easy_setopt(impl.handle, CURLOPT_TIMEOUT,
                   taskOption->getAsInt(PREF_TIMEOUT));
  curl_easy_setopt(impl.handle, CURLOPT_LOW_SPEED_LIMIT,
                   taskOption->getAsInt(PREF_LOWEST_SPEED_LIMIT));
  curl_easy_setopt(impl.handle, CURLOPT_LOW_SPEED_TIME,
                   taskOption->getAsInt(PREF_STARTUP_IDLE_TIME));
  impl.appliedLimit = -1;
  curl_easy_setopt(impl.handle, CURLOPT_USERAGENT,
                   taskOption->get(PREF_USER_AGENT).c_str());
  curl_easy_setopt(impl.handle, CURLOPT_FORBID_REUSE,
                   taskOption->getAsBool(PREF_ENABLE_HTTP_KEEP_ALIVE) ? 0L
                                                                      : 1L);
  if (taskOption->getAsBool(PREF_HTTP_ACCEPT_GZIP)) {
    curl_easy_setopt(impl.handle, CURLOPT_ACCEPT_ENCODING, "");
  }
  if (taskOption->getAsBool(PREF_HTTP_NO_CACHE)) {
    impl.headers = curl_slist_append(impl.headers, "Cache-Control: no-cache");
    impl.headers = curl_slist_append(impl.headers, "Pragma: no-cache");
  }
  curl_easy_setopt(impl.handle, CURLOPT_SSL_VERIFYPEER,
                   taskOption->getAsBool(PREF_CHECK_CERTIFICATE) ? 1L : 0L);
  curl_easy_setopt(impl.handle, CURLOPT_SSL_VERIFYHOST,
                   taskOption->getAsBool(PREF_CHECK_CERTIFICATE) ? 2L : 0L);
  if (!taskOption->blank(PREF_CA_CERTIFICATE)) {
    curl_easy_setopt(impl.handle, CURLOPT_CAINFO,
                     taskOption->get(PREF_CA_CERTIFICATE).c_str());
  }
  if (!taskOption->blank(PREF_CERTIFICATE)) {
    curl_easy_setopt(impl.handle, CURLOPT_SSLCERT,
                     taskOption->get(PREF_CERTIFICATE).c_str());
  }
  if (!taskOption->blank(PREF_PRIVATE_KEY)) {
    curl_easy_setopt(impl.handle, CURLOPT_SSLKEY,
                     taskOption->get(PREF_PRIVATE_KEY).c_str());
    curl_easy_setopt(impl.handle, CURLOPT_SSH_PRIVATE_KEYFILE,
                     taskOption->get(PREF_PRIVATE_KEY).c_str());
  }
  if (impl.http && !taskOption->blank(PREF_HTTP_USER)) {
    curl_easy_setopt(impl.handle, CURLOPT_USERNAME,
                     taskOption->get(PREF_HTTP_USER).c_str());
    curl_easy_setopt(impl.handle, CURLOPT_PASSWORD,
                     taskOption->get(PREF_HTTP_PASSWD).c_str());
  }
  else if (!impl.http && !taskOption->blank(PREF_SFTP_USER)) {
    curl_easy_setopt(impl.handle, CURLOPT_USERNAME,
                     taskOption->get(PREF_SFTP_USER).c_str());
    curl_easy_setopt(impl.handle, CURLOPT_PASSWORD,
                     taskOption->get(PREF_SFTP_PASSWD).c_str());
  }
  else if (!taskOption->getAsBool(PREF_NO_NETRC)) {
    curl_easy_setopt(impl.handle, CURLOPT_NETRC, CURL_NETRC_OPTIONAL);
    if (!taskOption->blank(PREF_NETRC_PATH)) {
      curl_easy_setopt(impl.handle, CURLOPT_NETRC_FILE,
                       taskOption->get(PREF_NETRC_PATH).c_str());
    }
  }
  if (!impl.http && !taskOption->blank(PREF_SSH_HOST_KEY_SHA256)) {
    curl_easy_setopt(impl.handle, CURLOPT_SSH_HOST_PUBLIC_KEY_SHA256,
                     taskOption->get(PREF_SSH_HOST_KEY_SHA256).c_str());
  }
  if (!taskOption->blank(PREF_REFERER)) {
    curl_easy_setopt(impl.handle, CURLOPT_REFERER,
                     taskOption->get(PREF_REFERER).c_str());
  }
  const auto proxy = proxyFor(taskOption, uriValue);
  if (!proxy.empty()) {
    curl_easy_setopt(impl.handle, CURLOPT_PROXY, proxy.c_str());
    uri::UriStruct parsed;
    uri::parse(parsed, uriValue);
    const auto proxyUser = proxyUserFor(taskOption, parsed.protocol);
    const auto proxyPassword = proxyPasswordFor(taskOption, parsed.protocol);
    if (!proxyUser.empty()) {
      curl_easy_setopt(impl.handle, CURLOPT_PROXYUSERNAME, proxyUser.c_str());
      curl_easy_setopt(impl.handle, CURLOPT_PROXYPASSWORD,
                       proxyPassword.c_str());
    }
  }
  if (!taskOption->blank(PREF_NO_PROXY)) {
    curl_easy_setopt(impl.handle, CURLOPT_NOPROXY,
                     taskOption->get(PREF_NO_PROXY).c_str());
  }
  if (!taskOption->blank(PREF_LOAD_COOKIES)) {
    curl_easy_setopt(impl.handle, CURLOPT_COOKIEFILE,
                     taskOption->get(PREF_LOAD_COOKIES).c_str());
  }
  if (!taskOption->blank(PREF_SAVE_COOKIES)) {
    curl_easy_setopt(impl.handle, CURLOPT_COOKIEJAR,
                     taskOption->get(PREF_SAVE_COOKIES).c_str());
  }
  std::istringstream headers(taskOption->get(PREF_HEADER));
  std::string header;
  while (std::getline(headers, header)) {
    if (!header.empty()) {
      impl.headers = curl_slist_append(impl.headers, header.c_str());
    }
  }
  if (impl.resumeOffset > 0) {
    curl_easy_setopt(impl.handle, CURLOPT_RESUME_FROM_LARGE,
                     static_cast<curl_off_t>(impl.resumeOffset));
    const auto& validator = !impl.etag.empty() ? impl.etag : impl.lastModified;
    if (impl.http && !validator.empty()) {
      const auto ifRange = "If-Range: " + validator;
      impl.headers = curl_slist_append(impl.headers, ifRange.c_str());
    }
  }
  if (impl.headers) {
    curl_easy_setopt(impl.handle, CURLOPT_HTTPHEADER, impl.headers);
  }
  return curl_multi_add_handle(multi_, impl.handle) == CURLM_OK;
}

std::unique_ptr<Command>
CurlSession::start(const std::shared_ptr<CurlDownload>& download,
                   RequestGroup* group, DownloadEngine* engine)
{
  engine_ = engine;
  if (prepare(download, group) && createHandle(download)) {
    downloads_[download->impl_->handle] = download;
    rebalanceLimits();
    poll();
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
  if (store_.save(state)) {
    impl.lastCheckpoint = global::wallclock();
  }
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
  if (impl.headers) {
    curl_slist_free_all(impl.headers);
    impl.headers = nullptr;
  }
  curl_easy_cleanup(impl.handle);
  impl.handle = nullptr;
  impl.resumeOffset = 0;
  impl.resumed = false;
  impl.rangeAccepted = false;
  impl.headersComplete = false;
  impl.etag.clear();
  impl.lastModified.clear();
  impl.file = fopen(impl.path.c_str(), "wb");
  download->snapshot_.completedLength = 0;
  download->snapshot_.sessionDownloadLength = 0;
  store_.removePath(impl.path);
  return impl.file && createHandle(download);
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
    fclose(impl.file);
    impl.file = nullptr;
  }
  if (impl.headers) {
    curl_slist_free_all(impl.headers);
    impl.headers = nullptr;
  }
  curl_easy_cleanup(impl.handle);
  impl.handle = nullptr;
  impl.uriIndex = (impl.uriIndex + 1) % impl.uris.size();
  impl.currentUri = impl.uris[impl.uriIndex];
  impl.resumeOffset = download->snapshot_.completedLength;
  impl.resumed = impl.resumeOffset > 0;
  impl.rangeAccepted = false;
  impl.headersComplete = false;
  if (impl.dryRun) {
    return createHandle(download);
  }
  impl.file = fopen(impl.path.c_str(), impl.resumed ? "r+b" : "wb");
  if (!impl.file ||
      (impl.resumed && fseeko(impl.file, impl.resumeOffset, SEEK_SET) != 0)) {
    return false;
  }
  return createHandle(download);
}

void CurlSession::finish(const std::shared_ptr<CurlDownload>& download,
                         CURLcode result)
{
  auto& impl = *download->impl_;
  if (impl.file) {
    fflush(impl.file);
    fclose(impl.file);
    impl.file = nullptr;
  }
  const bool rejectedResume = impl.resumed && impl.http && !impl.rangeAccepted;
  if ((result == CURLE_RANGE_ERROR || result == CURLE_WRITE_ERROR ||
       rejectedResume) &&
      restartWithoutResume(download)) {
    downloads_[impl.handle] = download;
    rebalanceLimits();
    return;
  }
  if (result != CURLE_OK) {
    if (retry(download)) {
      downloads_[impl.handle] = download;
      rebalanceLimits();
      return;
    }
    if (impl.headers) {
      curl_slist_free_all(impl.headers);
      impl.headers = nullptr;
    }
    if (impl.handle) {
      curl_easy_cleanup(impl.handle);
      impl.handle = nullptr;
    }
    checkpoint(download, true);
    download->snapshot_.state = CurlSnapshot::State::Error;
    download->snapshot_.error = curl_easy_strerror(result);
    download->snapshot_.downloadSpeed = 0;
    return;
  }

  curl_off_t reportedLength = 0;
  curl_off_t reportedFileTime = -1;
  if (impl.handle) {
    curl_easy_getinfo(impl.handle, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T,
                      &reportedLength);
    curl_easy_getinfo(impl.handle, CURLINFO_FILETIME_T, &reportedFileTime);
  }

  if (impl.headers) {
    curl_slist_free_all(impl.headers);
    impl.headers = nullptr;
  }
  if (impl.handle) {
    curl_easy_cleanup(impl.handle);
    impl.handle = nullptr;
  }

  if (impl.dryRun) {
    download->snapshot_.totalLength = std::max<curl_off_t>(0, reportedLength);
    download->snapshot_.state = CurlSnapshot::State::Complete;
    download->snapshot_.downloadSpeed = 0;
    store_.removePath(impl.path);
    return;
  }

  const auto length = File(impl.path).size();
  if (reportedFileTime >= 0 &&
      impl.group->getOption()->getAsBool(PREF_REMOTE_TIME)) {
    File(impl.path).utime(Time(), Time(static_cast<time_t>(reportedFileTime)));
  }
  download->snapshot_.completedLength = length;
  if (download->snapshot_.totalLength == 0) {
    download->snapshot_.totalLength = length;
  }
  auto context = impl.group->getDownloadContext();
  context->getFirstFileEntry()->setLength(download->snapshot_.totalLength);
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

void CurlSession::poll()
{
  if (!multi_) {
    return;
  }
  int running = 0;
  rebalanceLimits();
  curl_multi_perform(multi_, &running);
  for (const auto& entry : downloads_) {
    checkpoint(entry.second, false);
  }
  int remaining = 0;
  while (auto* message = curl_multi_info_read(multi_, &remaining)) {
    if (message->msg != CURLMSG_DONE) {
      continue;
    }
    auto found = downloads_.find(message->easy_handle);
    if (found == downloads_.end()) {
      continue;
    }
    auto download = found->second;
    curl_multi_remove_handle(multi_, message->easy_handle);
    downloads_.erase(found);
    rebalanceLimits();
    finish(download, message->data.result);
  }
}

void CurlSession::stop(const std::shared_ptr<CurlDownload>& download,
                       bool retainState)
{
  auto& impl = *download->impl_;
  impl.stopRequested = true;
  if (impl.handle) {
    auto found = downloads_.find(impl.handle);
    if (found != downloads_.end()) {
      curl_multi_remove_handle(multi_, impl.handle);
      downloads_.erase(found);
      rebalanceLimits();
    }
    curl_easy_cleanup(impl.handle);
    impl.handle = nullptr;
  }
  if (impl.headers) {
    curl_slist_free_all(impl.headers);
    impl.headers = nullptr;
  }
  if (impl.file) {
    fflush(impl.file);
    fclose(impl.file);
    impl.file = nullptr;
  }
  if (retainState) {
    checkpoint(download, true);
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
  const auto share =
      globalDownloadLimit_ > 0
          ? std::max<int64_t>(1, globalDownloadLimit_ /
                                     static_cast<int64_t>(downloads_.size()))
          : 0;
  for (const auto& entry : downloads_) {
    const auto task = entry.second->impl_->group->getMaxDownloadSpeedLimit();
    const auto limit = task > 0 && share > 0 ? std::min<int64_t>(task, share)
                       : task > 0            ? static_cast<int64_t>(task)
                                             : share;
    auto& impl = *entry.second->impl_;
    if (impl.appliedLimit != limit) {
      curl_easy_setopt(entry.first, CURLOPT_MAX_RECV_SPEED_LARGE,
                       static_cast<curl_off_t>(limit));
      impl.appliedLimit = limit;
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
