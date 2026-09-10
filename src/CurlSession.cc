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
#include <ctime>
#include <exception>
#include <limits>
#include <set>
#include <sstream>
#include <utility>

#include "ApplicationStatePath.h"
#include "ChecksumCheckIntegrityEntry.h"
#include "CurlCheckIntegrityEntry.h"
#include "CurlDownload.h"
#include "media/MediaDownload.h"
#include "CurlDownloadCommand.h"
#include "CurlDownloadImpl.h"
#include "Command.h"
#include "DownloadContext.h"
#include "DownloadEngine.h"
#include "DefaultDiskWriterFactory.h"
#include "DiskAdaptor.h"
#include "DiskWriterFactory.h"
#include "DlAbortEx.h"
#include "Exception.h"
#include "File.h"
#include "FileEntry.h"
#include "GroupId.h"
#include "Log.h"
#include "Option.h"
#include "PieceStorage.h"
#include "Request.h"
#include "RequestGroup.h"
#include "RequestGroupMan.h"
#include "SimpleRandomizer.h"
#include "TimeA2.h"
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
    const bool nextWrite = action == CURL_POLL_OUT || action == CURL_POLL_INOUT;
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

constexpr int64_t rangePipelineDepth = 2;

long otherFamily(long family)
{
  return family == CURL_IPRESOLVE_V4 ? CURL_IPRESOLVE_V6 : CURL_IPRESOLVE_V4;
}

CurlEndpoint& endpointFor(CurlDownloadImpl& impl, size_t uriIndex, long family)
{
  const size_t slot = family == CURL_IPRESOLVE_V4   ? 1
                      : family == CURL_IPRESOLVE_V6 ? 2
                                                    : 0;
  return impl.endpoints[(uriIndex % impl.uris.size()) * 3 + slot];
}

int effectiveStreamMaxConnections(const Option* option)
{
  return std::clamp(option->getAsInt(PREF_STREAM_MAX_CONNECTIONS), 1, 256);
}

int64_t adaptiveRangeSize(int64_t length, int maxConnections, int64_t quantum)
{
  length = std::max<int64_t>(0, length);
  quantum = std::max<int64_t>(1, quantum);
  if (maxConnections <= 1) {
    return std::max(quantum, length);
  }
  const auto targetRanges =
      static_cast<int64_t>(maxConnections) * rangePipelineDepth;
  const auto target =
      std::max<int64_t>(quantum, (length - 1) / targetRanges + 1);
  return ((target + quantum - 1) / quantum) * quantum;
}

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
      name = parsed.file;
      if (name.size() <= static_cast<size_t>(std::numeric_limits<int>::max())) {
        int length = 0;
        const auto decoded = std::unique_ptr<char, decltype(&curl_free)>(
            curl_easy_unescape(nullptr, name.c_str(),
                               static_cast<int>(name.size()), &length),
            curl_free);
        if (decoded) {
          std::string candidate(decoded.get(), static_cast<size_t>(length));
          // Decode only the URL basename, never a configured or restored path.
          if (util::isUtf8(candidate) && candidate != "." &&
              candidate != "..") {
            name = std::move(candidate);
          }
        }
      }
      name = util::createSafePath(name);
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

void resetResponse(CurlHandle& handle)
{
  handle.responseCode = 0;
  handle.responseRangeEnd = -1;
  handle.responseTotalLength = -1;
  handle.responseContentLength = -1;
  handle.unsatisfiedTotalLength = -1;
  handle.rangeAccepted = false;
  handle.fullResponseAccepted = false;
  handle.headersComplete = false;
  handle.responseFailure = CurlResponseFailure::None;
  handle.responseEtag.clear();
  handle.responseLastModified.clear();
  handle.responseDate.clear();
}

std::string responseHeader(CURL* easy, const char* name)
{
  curl_header* header = nullptr;
  if (curl_easy_header(easy, name, 0, CURLH_HEADER, -1, &header) != CURLHE_OK) {
    return {};
  }
  // Preserve the final field value while letting libcurl handle header
  // framing, folding and separation from CONNECT, 1xx and trailer fields.
  const auto last = header->amount - 1;
  if (last && curl_easy_header(easy, name, last, CURLH_HEADER, -1, &header) !=
                  CURLHE_OK) {
    return {};
  }
  return header->value;
}

bool parseContentRange(const std::string& value, int64_t& first, int64_t& end,
                       int64_t& total)
{
  long long parsedFirst = -1;
  long long parsedLast = -1;
  long long parsedTotal = -1;
  int consumed = 0;
  if (std::sscanf(value.c_str(), "bytes %lld-%lld/%lld%n", &parsedFirst,
                  &parsedLast, &parsedTotal, &consumed) != 3 ||
      consumed != static_cast<int>(value.size()) || parsedFirst < 0 ||
      parsedLast < parsedFirst || parsedTotal <= parsedLast) {
    return false;
  }
  first = parsedFirst;
  end = parsedLast + 1;
  total = parsedTotal;
  return true;
}

bool parseUnsatisfiedContentRange(const std::string& value, int64_t& total)
{
  auto range = value;
  if (range.size() >= 6 &&
      std::equal(range.begin(), range.begin() + 5, "bytes",
                 [](char lhs, char rhs) {
                   return std::tolower(static_cast<unsigned char>(lhs)) ==
                          std::tolower(static_cast<unsigned char>(rhs));
                 }) &&
      std::isspace(static_cast<unsigned char>(range[5]))) {
    range = trimHeader(range.substr(6));
  }
  long long parsedTotal = -1;
  int consumed = 0;
  if (std::sscanf(range.c_str(), "*/%lld%n", &parsedTotal, &consumed) != 1 ||
      consumed != static_cast<int>(range.size()) || parsedTotal < 0) {
    return false;
  }
  total = parsedTotal;
  return true;
}

bool parseContentLength(const std::string& value, int64_t& length)
{
  return util::parseLLIntNoThrow(length, value) && length >= 0;
}

std::string normalizeStrongEtag(const std::string& value)
{
  if (value.empty() || value.compare(0, 2, "W/") == 0) {
    return {};
  }
  const bool quoted =
      value.size() >= 2 && value.front() == '"' && value.back() == '"';
  if (!std::all_of(value.begin() + (quoted ? 1 : 0),
                   value.end() - (quoted ? 1 : 0), [](unsigned char c) {
                     return c == 0x21 || (c >= 0x23 && c <= 0x7e) || c >= 0x80;
                   })) {
    return {};
  }
  // Some origins omit the quotes around an otherwise valid opaque tag.
  // Normalize both responses and persisted identity, including If-Range.
  return quoted ? value : '"' + value + '"';
}

void rememberIdentity(CurlDownloadImpl& impl, const std::string& etag,
                      const std::string& modified, const std::string& date)
{
  if (impl.etag.empty()) {
    impl.etag = normalizeStrongEtag(etag);
  }
  const auto modifiedTime = curl_getdate(modified.c_str(), nullptr);
  const auto responseTime = curl_getdate(date.c_str(), nullptr);
  // A parseable date alone is not a strong validator. Use the conservative
  // HTTP date-age rule also used by browser download implementations.
  if (impl.lastModified.empty() && modifiedTime != -1 && responseTime != -1 &&
      std::difftime(responseTime, modifiedTime) >= 60) {
    impl.lastModified = modified;
  }
}

CurlResponseFailure identityFailure(const CurlDownloadImpl& impl,
                                    const CurlHandle& handle)
{
  if (!impl.etag.empty()) {
    const auto etag = normalizeStrongEtag(handle.responseEtag);
    if (!handle.responseEtag.empty() && etag.empty()) {
      return CurlResponseFailure::ValidatorUnavailable;
    }
    return !etag.empty() && impl.etag != etag ? CurlResponseFailure::EtagChanged
                                              : CurlResponseFailure::None;
  }
  const auto before = curl_getdate(impl.lastModified.c_str(), nullptr);
  const auto after = curl_getdate(handle.responseLastModified.c_str(), nullptr);
  return before != -1 && after != -1 && before != after
             ? CurlResponseFailure::ModifiedChanged
             : CurlResponseFailure::None;
}

const char* responseFailureName(CurlResponseFailure failure)
{
  switch (failure) {
  case CurlResponseFailure::EtagChanged:
    return "etag_changed";
  case CurlResponseFailure::ValidatorUnavailable:
    return "validator_unavailable";
  case CurlResponseFailure::ModifiedChanged:
    return "last_modified_changed";
  case CurlResponseFailure::LengthChanged:
    return "length_changed";
  case CurlResponseFailure::InvalidRange:
    return "invalid_range";
  case CurlResponseFailure::PreconditionFailed:
    return "precondition_failed";
  default:
    return "none";
  }
}

const char* responseFailureMessage(CurlResponseFailure failure)
{
  switch (failure) {
  case CurlResponseFailure::EtagChanged:
    return "The remote resource ETag changed; existing data was preserved";
  case CurlResponseFailure::ValidatorUnavailable:
    return "The response no longer confirms the requested resource identity; "
           "existing data was preserved";
  case CurlResponseFailure::ModifiedChanged:
    return "The remote resource Last-Modified changed; existing data was "
           "preserved";
  case CurlResponseFailure::LengthChanged:
    return "The remote resource length changed; existing data was preserved";
  case CurlResponseFailure::InvalidRange:
    return "The server returned an invalid Content-Range response";
  case CurlResponseFailure::PreconditionFailed:
    return "HTTP 412: the server rejected a request precondition; existing "
           "data was preserved";
  default:
    return "Invalid HTTP response";
  }
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
  if (impl.plannerConfigured && !impl.fullDownload &&
      handle->purpose == CurlHandlePurpose::Payload) {
    impl.idleWorkers.push_back(handle->addressFamily);
  }
  impl.handles.erase(
      std::remove_if(impl.handles.begin(), impl.handles.end(),
                     [handle](const std::unique_ptr<CurlHandle>& entry) {
                       return entry.get() == handle;
                     }),
      impl.handles.end());
}

void flushWriteBuffer(CurlDownloadImpl& impl, CurlHandle& handle)
{
  if (handle.writeBuffer.empty()) {
    return;
  }
  if (!impl.writer) {
    throw DL_ABORT_EX2("The output file is not open",
                       error_code::FILE_OPEN_ERROR);
  }
  impl.writer->writeData(handle.writeBuffer.data(), handle.writeBuffer.size(),
                         handle.bufferOffset);
  impl.planner.commit(handle.bufferOffset,
                      handle.bufferOffset +
                          static_cast<int64_t>(handle.writeBuffer.size()));
  handle.writeBuffer.clear();
}

int64_t bufferedLength(const CurlDownloadImpl& impl)
{
  int64_t result = 0;
  for (const auto& handle : impl.handles) {
    result += static_cast<int64_t>(handle->writeBuffer.size());
  }
  return result;
}

bool retryableHttpFailure(CURLcode result, long responseCode,
                          int fileNotFoundCount, int maxFileNotFound,
                          bool validatedRange)
{
  if (result == CURLE_REMOTE_FILE_NOT_FOUND) {
    return maxFileNotFound > 0 && fileNotFoundCount < maxFileNotFound;
  }
  if (result != CURLE_HTTP_RETURNED_ERROR) {
    return false;
  }
  if (responseCode == 404) {
    return maxFileNotFound > 0 && fileNotFoundCount < maxFileNotFound;
  }
  if (responseCode == 403) {
    return validatedRange;
  }
  return responseCode == 408 || responseCode == 425 || responseCode == 429 ||
         responseCode == 500 || responseCode == 502 || responseCode == 503 ||
         responseCode == 504;
}

bool retryableTransportFailure(CURLcode result, bool applicationConnected)
{
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
  case CURLE_SSL_CONNECT_ERROR:
    return true;
  case CURLE_SSH:
    return applicationConnected;
  default:
    return false;
  }
}

error_code::Value curlErrorCode(CURLcode result, long responseCode)
{
  if (responseCode == 401 || responseCode == 403) {
    return error_code::HTTP_AUTH_FAILED;
  }
  if (responseCode == 404) {
    return error_code::RESOURCE_NOT_FOUND;
  }
  if (responseCode == 408 || responseCode == 425 || responseCode == 429 ||
      responseCode == 500 || responseCode == 502 || responseCode == 503 ||
      responseCode == 504) {
    return error_code::HTTP_SERVICE_UNAVAILABLE;
  }
  if (responseCode >= 400) {
    return error_code::HTTP_PROTOCOL_ERROR;
  }
  switch (result) {
  case CURLE_OPERATION_TIMEDOUT:
    return error_code::TIME_OUT;
  case CURLE_COULDNT_RESOLVE_HOST:
  case CURLE_COULDNT_RESOLVE_PROXY:
    return error_code::NAME_RESOLVE_ERROR;
  case CURLE_TOO_MANY_REDIRECTS:
    return error_code::HTTP_TOO_MANY_REDIRECTS;
  case CURLE_LOGIN_DENIED:
  case CURLE_REMOTE_ACCESS_DENIED:
    return error_code::HTTP_AUTH_FAILED;
  case CURLE_REMOTE_FILE_NOT_FOUND:
    return error_code::RESOURCE_NOT_FOUND;
  case CURLE_RANGE_ERROR:
    return error_code::CANNOT_RESUME;
  default:
    return error_code::NETWORK_PROBLEM;
  }
}

bool containsSensitiveCurlText(const std::string& value)
{
  std::string lowerValue(value);
  std::transform(lowerValue.begin(), lowerValue.end(), lowerValue.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  static const char* sensitive[] = {"authorization:", "proxy-authorization:",
                                    "cookie:",        "set-cookie:",
                                    "password",       "bearer ",
                                    "private key",    "client certificate"};
  return std::any_of(std::begin(sensitive), std::end(sensitive),
                     [&](const char* marker) {
                       return lowerValue.find(marker) != std::string::npos;
                     });
}

bool usefulCurlText(const std::string& value)
{
  std::string lowerValue(value);
  std::transform(lowerValue.begin(), lowerValue.end(), lowerValue.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  if (lowerValue.find("netrc file") != std::string::npos ||
      lowerValue.find("newsession ticket") != std::string::npos ||
      lowerValue.find("tls handshake") != std::string::npos ||
      lowerValue.find("change cipher") != std::string::npos ||
      lowerValue.find("certificate level") != std::string::npos ||
      lowerValue.find("server certificate:") != std::string::npos ||
      lowerValue.find("subject:") == 0 || lowerValue.find("issuer:") == 0 ||
      lowerValue.find("start date:") == 0 ||
      lowerValue.find("expire date:") == 0) {
    return false;
  }
  static const char* useful[] = {"was resolved",
                                 "trying ",
                                 "established connection",
                                 "connected to",
                                 "reusing existing",
                                 "ssl connection using",
                                 "certificate verified",
                                 "alpn: server",
                                 "using http/",
                                 "request completely",
                                 "redirect",
                                 "closing connection",
                                 "could not",
                                 "failed",
                                 "error",
                                 "timed out",
                                 "proxy",
                                 "ssh"};
  return std::any_of(std::begin(useful), std::end(useful),
                     [&](const char* marker) {
                       return lowerValue.find(marker) != std::string::npos;
                     });
}

} // namespace

long CurlSession::platformSslOptions() noexcept
{
#ifdef _WIN32
  return CURLSSLOPT_REVOKE_BEST_EFFORT;
#else
  return 0L;
#endif
}

bool CurlSession::sameOrigin(const std::string& first,
                             const std::string& second)
{
  const auto left = std::unique_ptr<CURLU, decltype(&curl_url_cleanup)>(
      curl_url(), curl_url_cleanup);
  const auto right = std::unique_ptr<CURLU, decltype(&curl_url_cleanup)>(
      curl_url(), curl_url_cleanup);
  if (!left || !right ||
      curl_url_set(left.get(), CURLUPART_URL, first.c_str(), 0) != CURLUE_OK ||
      curl_url_set(right.get(), CURLUPART_URL, second.c_str(), 0) !=
          CURLUE_OK) {
    return false;
  }
  for (const auto part : {CURLUPART_SCHEME, CURLUPART_HOST, CURLUPART_PORT}) {
    char* a = nullptr;
    char* b = nullptr;
    const auto aResult = curl_url_get(left.get(), part, &a, CURLU_DEFAULT_PORT);
    const auto bResult =
        curl_url_get(right.get(), part, &b, CURLU_DEFAULT_PORT);
    const auto aValue =
        std::unique_ptr<char, decltype(&curl_free)>(a, curl_free);
    const auto bValue =
        std::unique_ptr<char, decltype(&curl_free)>(b, curl_free);
    if (aResult != CURLUE_OK || bResult != CURLUE_OK || !util::strieq(a, b)) {
      return false;
    }
  }
  return true;
}

bool CurlSession::matchesRange(CURL* handle, int64_t begin, int64_t end,
                               int64_t length)
{
  int64_t first = 0, limit = 0, total = 0;
  return parseContentRange(responseHeader(handle, "Content-Range"), first,
                           limit, total) &&
         first == begin && (end < 0 ? limit == total : limit - 1 == end) &&
         limit - first == length;
}

CURLcode CurlSession::configureTls(CURL* handle, const Option* option)
{
  CURLcode result = CURLE_OK;
  auto set = [&](CURLoption key, auto value) {
    if (result == CURLE_OK)
      result = curl_easy_setopt(handle, key, value);
  };
  const long verify = option->getAsBool(PREF_CHECK_CERTIFICATE) ? 1L : 0L;
  set(CURLOPT_SSL_VERIFYPEER, verify);
  set(CURLOPT_SSL_VERIFYHOST, verify ? 2L : 0L);
  set(CURLOPT_PROXY_SSL_VERIFYPEER, verify);
  set(CURLOPT_PROXY_SSL_VERIFYHOST, verify ? 2L : 0L);
  set(CURLOPT_SSL_OPTIONS, platformSslOptions());
  set(CURLOPT_PROXY_SSL_OPTIONS, platformSslOptions());
  const auto& minimum = option->get(PREF_MIN_TLS_VERSION);
  const long version = minimum == A2_V_TLS13   ? CURL_SSLVERSION_TLSv1_3
                       : minimum == A2_V_TLS12 ? CURL_SSLVERSION_TLSv1_2
                                               : CURL_SSLVERSION_TLSv1_1;
  set(CURLOPT_SSLVERSION, version);
  if (!option->blank(PREF_CA_CERTIFICATE))
    set(CURLOPT_CAINFO, option->get(PREF_CA_CERTIFICATE).c_str());
  if (!option->blank(PREF_CERTIFICATE))
    set(CURLOPT_SSLCERT, option->get(PREF_CERTIFICATE).c_str());
  if (!option->blank(PREF_PRIVATE_KEY))
    set(CURLOPT_SSLKEY, option->get(PREF_PRIVATE_KEY).c_str());
  return result;
}

void CurlSession::rememberEndpoint(CurlHandle& handle)
{
  auto& impl = *handle.download->impl_;
  if (!handle.value || impl.endpoints.empty() ||
      (!handle.rangeAccepted && !handle.fullResponseAccepted) ||
      handle.responseFailure != CurlResponseFailure::None) {
    return;
  }
  auto& endpoint =
      endpointFor(impl, handle.lease.uriIndex, handle.addressFamily);
  if (handle.endpointGeneration != endpoint.generation) {
    return;
  }
  char* effective = nullptr;
  if (curl_easy_getinfo(handle.value, CURLINFO_EFFECTIVE_URL, &effective) ==
          CURLE_OK &&
      effective) {
    // The native handle owns this pointer. Retain the validated URL, not its
    // storage, and keep the original task URI for restart and authentication.
    endpoint.uri = effective;
    endpoint.resolving = false;
    endpoint.readyAt = {};
    if (!impl.plannerConfigured &&
        impl.families[0] == CURL_IPRESOLVE_WHATEVER && impl.group &&
        impl.maxConnections > 1 && handle.rangeAccepted &&
        !impl.group->getOption()->getAsBool(PREF_DISABLE_IPV6) &&
        impl.group->getOption()->blank(PREF_INTERFACE) &&
        impl.group->getMaxDownloadSpeedLimit() == 0 &&
        impl.group->getOption()->getAsLLInt(PREF_MAX_OVERALL_DOWNLOAD_LIMIT) ==
            0) {
      auto url = std::unique_ptr<CURLU, decltype(&curl_url_cleanup)>(
          curl_url(), curl_url_cleanup);
      char* host = nullptr;
      if (url &&
          curl_url_set(url.get(), CURLUPART_URL, effective, 0) == CURLUE_OK &&
          curl_url_get(url.get(), CURLUPART_HOST, &host, 0) == CURLUE_OK) {
        auto hostname =
            std::unique_ptr<char, decltype(&curl_free)>(host, curl_free);
        char* address = nullptr;
        long proxy = 0;
        curl_easy_getinfo(handle.value, CURLINFO_PRIMARY_IP, &address);
        curl_easy_getinfo(handle.value, CURLINFO_USED_PROXY, &proxy);
        // Literal addresses do not offer a second family. DNS, connection
        // racing and connection reuse remain native libcurl operations.
        if (!proxy && address && *address && host[0] != '[' &&
            !util::isNumericHost(host)) {
          handle.addressFamily =
              std::strchr(address, ':') ? CURL_IPRESOLVE_V6 : CURL_IPRESOLVE_V4;
          impl.families = {
              {handle.addressFamily, otherFamily(handle.addressFamily)}};
          auto& route =
              endpointFor(impl, handle.lease.uriIndex, handle.addressFamily);
          route.uri = effective;
          route.resolving = false;
          route.readyAt = {};
          handle.endpointGeneration = route.generation;
        }
      }
    }
  }
}

bool CurlSession::retryableFailure(CURLcode result, long responseCode,
                                   int fileNotFoundCount, int maxFileNotFound,
                                   bool validatedRange,
                                   bool applicationConnected)
{
  return retryableHttpFailure(result, responseCode, fileNotFoundCount,
                              maxFileNotFound, validatedRange) ||
         retryableTransportFailure(result, applicationConnected);
}

std::string CurlSession::failureMessage(const CurlHandle& handle,
                                        CURLcode result, long responseCode)
{
  auto detail = handle.errorBuffer[0] != '\0'
                    ? std::string(handle.errorBuffer.data())
                    : std::string(curl_easy_strerror(result));
  detail = containsSensitiveCurlText(detail)
               ? "Sensitive native diagnostic redacted"
               : logging::sanitizeUri(detail);
  const bool httpFailure =
      result == CURLE_HTTP_RETURNED_ERROR || responseCode >= 400;
  return httpFailure ? "HTTP " + std::to_string(responseCode) + ": " + detail
                     : detail;
}

ExistingFileDecision CurlSession::decideExistingFile(int64_t localLength,
                                                     int64_t remoteLength,
                                                     bool rangeSupported)
{
  if (localLength == remoteLength) {
    return ExistingFileDecision::Complete;
  }
  if (localLength < remoteLength && rangeSupported) {
    return ExistingFileDecision::Resume;
  }
  return ExistingFileDecision::Reject;
}

std::string CurlSession::gid(const CurlDownload* download)
{
  if (!download || !download->impl_ || !download->impl_->group) {
    return "unknown";
  }
  return GroupId::toHex(download->impl_->group->getGID());
}

size_t CurlSession::writeData(char* data, size_t size, size_t count,
                              void* userData) noexcept
{
  auto* handle = static_cast<CurlHandle*>(userData);
  auto* download = handle->download;
  try {
    auto& impl = *download->impl_;
    if (size != 0 && count > std::numeric_limits<size_t>::max() / size) {
      fail(download, error_code::FILE_IO_ERROR,
           "Received an oversized output block");
      return CURL_WRITEFUNC_ERROR;
    }
    auto length = size * count;
    if (handle->purpose == CurlHandlePurpose::RangeProbe) {
      return handle->responseCode == 206 && handle->rangeAccepted
                 ? length
                 : CURL_WRITEFUNC_ERROR;
    }
    if (handle->purpose == CurlHandlePurpose::HeadProbe) {
      return CURL_WRITEFUNC_ERROR;
    }
    if (handle->responseFailure != CurlResponseFailure::None ||
        (handle->ranged && handle->headersComplete && !handle->rangeAccepted &&
         !handle->fullResponseAccepted)) {
      return CURL_WRITEFUNC_ERROR;
    }
    if (download->snapshot_.mediaManifest)
      return CURL_WRITEFUNC_ERROR;
    if (!impl.writer) {
      fail(download, error_code::FILE_OPEN_ERROR,
           "The output file is not open");
      return CURL_WRITEFUNC_ERROR;
    }
    if (length > static_cast<size_t>(std::numeric_limits<int64_t>::max()) ||
        handle->writeOffset > std::numeric_limits<int64_t>::max() -
                                  static_cast<int64_t>(length) ||
        (handle->rangeAccepted &&
         handle->writeOffset + static_cast<int64_t>(length) >
             handle->responseRangeEnd)) {
      fail(download, error_code::HTTP_PROTOCOL_ERROR,
           "The response body exceeds its declared byte range");
      return CURL_WRITEFUNC_ERROR;
    }
    // A helper can own the suffix of an in-flight HTTP response. Stop this
    // request at its assigned boundary without writing into the helper's range.
    const bool assisted =
        handle->rangeAccepted && handle->lease.end < handle->responseRangeEnd;
    if (assisted) {
      length =
          std::min<uint64_t>(length, handle->lease.end - handle->writeOffset);
    }
    if (handle->writeBuffer.empty()) {
      handle->bufferOffset = handle->writeOffset;
    }
    else if (handle->bufferOffset +
                 static_cast<int64_t>(handle->writeBuffer.size()) !=
             handle->writeOffset) {
      flushWriteBuffer(impl, *handle);
      handle->bufferOffset = handle->writeOffset;
    }
    if (length > 0 && handle->writeOffset == handle->lease.begin &&
        A2_LOG_ENABLED(spdlog::level::debug)) {
      char* address = nullptr;
      curl_easy_getinfo(handle->value, CURLINFO_PRIMARY_IP, &address);
      A2_LOG_DEBUG(fmt("component=stream event=route_payload gid=%s family=%s "
                       "bytes=%" PRId64,
                       gid(download).c_str(),
                       address && std::strchr(address, ':') ? "ipv6" : "ipv4",
                       static_cast<int64_t>(length)));
    }
    const auto* bytes = reinterpret_cast<unsigned char*>(data);
    handle->writeBuffer.insert(handle->writeBuffer.end(), bytes,
                               bytes + length);
    handle->writeOffset += static_cast<int64_t>(length);
    if (length > 0) {
      handle->lastPayload = global::wallclock();
    }
    if (length > 0) {
      if (handle->bodySampleStart.isZero()) {
        handle->bodySampleStart = global::wallclock();
        handle->payloadSpeed.reset();
      }
      handle->payloadSpeed.update(length);
    }
    if (handle->writeBuffer.size() >= handle->bufferLimit) {
      flushWriteBuffer(impl, *handle);
    }
    download->snapshot_.completedLength =
        impl.planner.completedLength() + bufferedLength(impl);
    download->snapshot_.sessionDownloadLength += static_cast<int64_t>(length);
    if (impl.group) {
      impl.group->getDownloadContext()->updateDownload(length);
    }
    return assisted && handle->writeOffset == handle->lease.end
               ? CURL_WRITEFUNC_ERROR
               : length;
  }
  catch (const Exception& error) {
    fail(download, error.getErrorCode(), error.what());
  }
  catch (const std::exception& error) {
    fail(download, error_code::FILE_IO_ERROR, error.what());
  }
  catch (...) {
    fail(download, error_code::FILE_IO_ERROR, "Unable to write output data");
  }
  return CURL_WRITEFUNC_ERROR;
}

void CurlSession::validateResponse(CurlHandle& handle,
                                   const std::string& contentRange)
{
  auto& download = *handle.download;
  auto& impl = *download.impl_;
  handle.headersComplete = true;
  if (handle.responseCode == 412) {
    handle.responseFailure = CurlResponseFailure::PreconditionFailed;
    return;
  }
  if (handle.responseCode == 416) {
    parseUnsatisfiedContentRange(contentRange, handle.unsatisfiedTotalLength);
    return;
  }
  if (handle.responseCode != 200 && handle.responseCode != 206) {
    return;
  }
  handle.responseFailure = identityFailure(impl, handle);
  if (handle.responseCode == 206 && handle.ranged) {
    int64_t first = -1;
    if (!parseContentRange(contentRange, first, handle.responseRangeEnd,
                           handle.responseTotalLength) ||
        first != handle.lease.begin ||
        handle.responseRangeEnd > handle.lease.end) {
      handle.responseFailure = CurlResponseFailure::InvalidRange;
    }
    else if ((impl.planner.totalLength() > 0 &&
              impl.planner.totalLength() != handle.responseTotalLength) ||
             (download.snapshot_.totalLength > 0 &&
              download.snapshot_.totalLength != handle.responseTotalLength)) {
      handle.responseFailure = CurlResponseFailure::LengthChanged;
    }
    if (handle.responseFailure == CurlResponseFailure::None) {
      handle.lease.end = std::min(handle.lease.end, handle.responseTotalLength);
      handle.rangeAccepted = true;
      if (handle.purpose == CurlHandlePurpose::Payload) {
        impl.rangeValidated = true;
        download.snapshot_.totalLength = handle.responseTotalLength;
        rememberIdentity(impl, handle.responseEtag, handle.responseLastModified,
                         handle.responseDate);
      }
    }
  }
  else if (handle.responseCode == 200) {
    if (!handle.rangeValidator.empty() &&
        ((!impl.etag.empty() && handle.responseEtag.empty()) ||
         (impl.etag.empty() && handle.responseLastModified.empty()))) {
      handle.responseFailure = CurlResponseFailure::ValidatorUnavailable;
    }
    if (download.snapshot_.totalLength > 0 &&
        handle.responseContentLength >= 0 &&
        download.snapshot_.totalLength != handle.responseContentLength) {
      handle.responseFailure = CurlResponseFailure::LengthChanged;
    }
    if (handle.responseFailure == CurlResponseFailure::None &&
        handle.lease.begin == 0 && impl.planner.completedLength() == 0 &&
        !impl.plannerConfigured) {
      handle.fullResponseAccepted = true;
      if (handle.purpose == CurlHandlePurpose::Payload) {
        rememberIdentity(impl, handle.responseEtag, handle.responseLastModified,
                         handle.responseDate);
      }
    }
  }
}

size_t CurlSession::receiveHeader(char* data, size_t size, size_t count,
                                  void* userData) noexcept
{
  auto* handle = static_cast<CurlHandle*>(userData);
  auto* download = handle->download;
  try {
    if (size != 0 && count > std::numeric_limits<size_t>::max() / size) {
      fail(download, error_code::HTTP_PROTOCOL_ERROR, "Oversized HTTP header");
      return CURL_WRITEFUNC_ERROR;
    }
    const auto length = size * count;
    const std::string line(data, length);
    if (startsWithHeader(line, "http/")) {
      resetResponse(*handle);
    }
    else if (!handle->headersComplete && (line == "\r\n" || line == "\n")) {
      curl_easy_getinfo(handle->value, CURLINFO_RESPONSE_CODE,
                        &handle->responseCode);
      handle->responseEtag = responseHeader(handle->value, "ETag");
      handle->responseLastModified =
          responseHeader(handle->value, "Last-Modified");
      handle->responseDate = responseHeader(handle->value, "Date");
      parseContentLength(responseHeader(handle->value, "Content-Length"),
                         handle->responseContentLength);
      if (handle->responseCode >= 200 && handle->responseCode < 300 &&
          handle->lease.begin == 0 && download->impl_->existingLength == 0 &&
          download->impl_->group->getOption()->get(PREF_MEDIA) == "auto" &&
          media::Download::manifestMime(
              responseHeader(handle->value, "Content-Type"))) {
        download->snapshot_.mediaManifest = true;
        const auto mime = responseHeader(handle->value, "Content-Type");
        download->impl_->group->getOption()->put(
            PREF_MEDIA, curl_strequal(mime.substr(0, mime.find(';')).c_str(),
                                      "application/dash+xml")
                            ? "dash"
                            : "hls");
      }
      validateResponse(*handle, responseHeader(handle->value, "Content-Range"));
      rememberEndpoint(*handle);
    }
    return length;
  }
  catch (const std::exception& error) {
    fail(download, error_code::UNKNOWN_ERROR, error.what());
  }
  catch (...) {
    fail(download, error_code::UNKNOWN_ERROR,
         "Unable to process response headers");
  }
  return CURL_WRITEFUNC_ERROR;
}

int CurlSession::updateProgress(void* userData, curl_off_t downloadTotal,
                                curl_off_t downloaded, curl_off_t,
                                curl_off_t) noexcept
{
  try {
    auto* handle = static_cast<CurlHandle*>(userData);
    auto* download = handle->download;
    auto& impl = *download->impl_;
    const auto total =
        handle->ranged && !handle->fullResponseAccepted
            ? 0
            : handle->lease.begin + std::max<curl_off_t>(0, downloadTotal);
    if (total > 0 && download->snapshot_.totalLength == 0) {
      download->snapshot_.totalLength = total;
    }
    (void)downloaded;
    return impl.stopRequested ? 1 : 0;
  }
  catch (...) {
    return 1;
  }
}

int CurlSession::debugCallback(CURL* easy, curl_infotype type, char* data,
                               size_t size, void* userData) noexcept
{
  try {
    auto* handle = static_cast<CurlHandle*>(userData);
    void* privateData = nullptr;
    if (!handle || !handle->download ||
        curl_easy_getinfo(easy, CURLINFO_PRIVATE, &privateData) != CURLE_OK ||
        privateData != handle) {
      return 0;
    }

    std::string message;
    const char* direction = nullptr;
    if (type == CURLINFO_TEXT) {
      message.assign(data, size);
      message = trimHeader(message);
      if (message.empty() || containsSensitiveCurlText(message) ||
          !usefulCurlText(message)) {
        return 0;
      }
      message = logging::sanitizeUri(message);
      direction = "info";
    }
    else if (type == CURLINFO_HEADER_IN || type == CURLINFO_HEADER_OUT) {
      message = logging::summarizeHttpMessage(std::string(data, size));
      if (message.empty()) {
        return 0;
      }
      direction = type == CURLINFO_HEADER_IN ? "recv" : "send";
    }
    else {
      return 0;
    }

    curl_off_t transferId = -1;
    curl_easy_getinfo(easy, CURLINFO_XFER_ID, &transferId);
    logging::tryWrite(
        spdlog::level::trace, __FILE__, __LINE__,
        fmt("component=stream event=curl_trace gid=%s transfer=%" PRId64
            " direction=%s %s",
            gid(handle->download).c_str(), static_cast<int64_t>(transferId),
            direction, message.c_str()));
  }
  catch (...) {
  }
  return 0;
}

void CurlSession::fail(CurlDownload* download, error_code::Value errorCode,
                       const std::string& message) noexcept
{
  if (!download || download->snapshot_.errorCode != error_code::UNDEFINED) {
    return;
  }
  download->snapshot_.errorCode = errorCode;
  download->snapshot_.state = CurlSnapshot::State::Error;
  try {
    download->snapshot_.error = message;
  }
  catch (...) {
  }
}

bool CurlSession::openOutput(const std::shared_ptr<CurlDownload>& download,
                             bool preserveExisting)
{
  auto& impl = *download->impl_;
  impl.createdOutput = !File(impl.path).exists();
  const auto fallbackError = preserveExisting ? error_code::FILE_OPEN_ERROR
                                              : error_code::FILE_CREATE_ERROR;
  impl.writer.reset();
  try {
    const auto& configuredFactory = impl.group->getDiskWriterFactory();
    if (configuredFactory) {
      impl.writer = configuredFactory->newDiskWriter(impl.path);
    }
    else {
      impl.writer = DefaultDiskWriterFactory().newDiskWriter(impl.path);
    }
    if (!impl.writer) {
      fail(download.get(), fallbackError, "Unable to create the output writer");
      return false;
    }
    if (preserveExisting) {
      impl.writer->openExistingFile();
    }
    else {
      impl.writer->initAndOpenFile();
    }
    return true;
  }
  catch (const Exception& error) {
    impl.writer.reset();
    fail(download.get(), error.getErrorCode(), error.what());
  }
  catch (const std::exception& error) {
    impl.writer.reset();
    fail(download.get(), fallbackError, error.what());
  }
  catch (...) {
    impl.writer.reset();
    fail(download.get(), fallbackError, "Unable to open the output file");
  }
  return false;
}

void CurlSession::closeOutput(CurlDownload* download) noexcept
{
  if (!download || !download->impl_->writer) {
    return;
  }
  try {
    download->impl_->writer->closeFile();
  }
  catch (const std::exception& error) {
    A2_LOG_ERROR(fmt("Closing stream output failed: %s", error.what()));
  }
  catch (...) {
    A2_LOG_ERROR("Closing stream output failed");
  }
  download->impl_->writer.reset();
}

CurlSession::CurlSession(const Option* option)
    : option_(option),
      globalDownloadLimit_(option->getAsLLInt(PREF_MAX_OVERALL_DOWNLOAD_LIMIT)),
      store_(state::streamDatabaseFile(option)),
      loggingRevision_(logging::revision())
{
  const auto globalResult = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (globalResult != CURLE_OK) {
    A2_LOG_ERROR(fmt("component=stream event=session_init_failed curl=%d "
                     "message=%s",
                     static_cast<int>(globalResult),
                     curl_easy_strerror(globalResult)));
    return;
  }
  curlInitialized_ = true;
  multi_ = curl_multi_init();
  if (!multi_) {
    A2_LOG_ERROR("component=stream event=session_init_failed "
                 "message=curl_multi_init returned null");
    return;
  }
  share_ = curl_share_init();
  if (share_) {
    const auto cookie =
        curl_share_setopt(share_, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
    const auto dns =
        curl_share_setopt(share_, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
    const auto tls =
        curl_share_setopt(share_, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
    if (cookie != CURLSHE_OK || dns != CURLSHE_OK || tls != CURLSHE_OK) {
      A2_LOG_WARN(fmt("component=stream event=share_disabled cookie=%d dns=%d "
                      "tls=%d",
                      static_cast<int>(cookie), static_cast<int>(dns),
                      static_cast<int>(tls)));
      curl_share_cleanup(share_);
      share_ = nullptr;
    }
  }
  store_.open();
  const CURLMcode options[] = {
      curl_multi_setopt(multi_, CURLMOPT_SOCKETFUNCTION, socketCallback),
      curl_multi_setopt(multi_, CURLMOPT_SOCKETDATA, this),
      curl_multi_setopt(multi_, CURLMOPT_TIMERFUNCTION, timerCallback),
      curl_multi_setopt(multi_, CURLMOPT_TIMERDATA, this)};
  const auto failed =
      std::find_if(std::begin(options), std::end(options),
                   [](CURLMcode value) { return value != CURLM_OK; });
  if (failed != std::end(options)) {
    A2_LOG_ERROR(fmt("component=stream event=session_init_failed curlm=%d "
                     "message=%s",
                     static_cast<int>(*failed), curl_multi_strerror(*failed)));
    curl_multi_cleanup(multi_);
    multi_ = nullptr;
    return;
  }
  if (!refreshConnectionPoolLimits()) {
    curl_multi_cleanup(multi_);
    multi_ = nullptr;
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
    const auto result = curl_multi_remove_handle(multi_, entry.first);
    if (result != CURLM_OK) {
      A2_LOG_WARN(fmt("component=stream event=handle_remove_failed curlm=%d "
                      "message=%s",
                      static_cast<int>(result), curl_multi_strerror(result)));
    }
    cleanupHandle(*entry.second.second);
  }
  downloads_.clear();
  for (const auto& entry : tasks_) {
    try {
      for (auto& handle : entry.second->impl_->handles) {
        flushWriteBuffer(*entry.second->impl_, *handle);
      }
      entry.second->snapshot_.completedLength =
          entry.second->impl_->planner.completedLength();
    }
    catch (const std::exception& error) {
      A2_LOG_ERROR(fmt("Flushing stream output failed: %s", error.what()));
    }
    checkpoint(entry.second, true);
    closeOutput(entry.first);
  }
  tasks_.clear();
  if (multi_) {
    curl_multi_cleanup(multi_);
  }
  if (share_) {
    curl_share_cleanup(share_);
  }
  if (curlInitialized_) {
    curl_global_cleanup();
  }
}

bool CurlSession::prepare(const std::shared_ptr<CurlDownload>& download,
                          RequestGroup* group)
{
  download->snapshot_.errorCode = error_code::UNDEFINED;
  download->snapshot_.error.clear();
  download->snapshot_.totalLength = 0;
  download->snapshot_.completedLength = 0;
  download->snapshot_.sessionDownloadLength = 0;
  download->snapshot_.connections = 0;
  if (!multi_ || download->impl_->uris.empty()) {
    fail(download.get(), error_code::NETWORK_PROBLEM,
         "The curl transfer session is unavailable");
    return false;
  }
  auto& impl = *download->impl_;
  for (auto& handle : impl.handles) {
    cleanupHandle(*handle);
  }
  impl.handles.clear();
  closeOutput(download.get());
  impl.group = group;
  impl.endpoints.assign(impl.uris.size() * 3, CurlEndpoint{});
  impl.families = {{CURL_IPRESOLVE_WHATEVER, CURL_IPRESOLVE_WHATEVER}};
  impl.idleWorkers.clear();
  impl.planner.clear();
  impl.etag.clear();
  impl.lastModified.clear();
  impl.dryRun = group->getOption()->getAsBool(PREF_DRY_RUN);
  impl.stopRequested = false;
  impl.plannerConfigured = false;
  impl.kickPending = false;
  impl.fileNotFoundCount = 0;
  impl.rangeValidated = false;
  impl.fullDownload = false;
  impl.existingLength = 0;
  impl.startMode = CurlStartMode::Transfer;
  impl.maxConnections = effectiveStreamMaxConnections(group->getOption().get());
  impl.connectionLimit = impl.maxConnections;
  impl.connectionEpoch = 0;
  impl.lastRecoveryDownloadLength = 0;
  impl.recoverConnectionsAt = {};
  impl.preferredUriIndex %= impl.uris.size();
  const auto& uriValue = impl.uris[impl.preferredUriIndex];
  impl.currentUri = uriValue;
  impl.path = outputPath(group, uriValue);

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
    impl.preferredUriIndex = static_cast<size_t>(restored - impl.uris.begin());
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
  const bool rangesFit =
      std::all_of(state.completedRanges.begin(), state.completedRanges.end(),
                  [existingLength](const auto& range) {
                    return range.second <= existingLength;
                  });
  const bool restoreState = hasState && output.isFile() && rangesFit &&
                            existingLength >= state.completedLength;
  if (hasState && !restoreState) {
    store_.removePath(impl.path);
  }
  impl.allowFullRestart = existingLength == 0 ||
                          group->getOption()->getAsBool(PREF_ALLOW_OVERWRITE);
  if (restoreState) {
    impl.planner.restore(state.completedRanges);
    impl.etag = normalizeStrongEtag(state.etag);
    impl.lastModified = state.lastModified;
    download->snapshot_.totalLength = state.totalLength;
  }
  else if (output.isFile() && group->getOption()->getAsBool(PREF_CONTINUE) &&
           !group->getOption()->getAsBool(PREF_ALLOW_OVERWRITE)) {
    impl.existingLength = existingLength;
    if (impl.http) {
      impl.startMode = CurlStartMode::InspectExisting;
    }
    else {
      impl.planner.commit(0, existingLength);
    }
  }
  if (impl.dryRun) {
    impl.planner.clear();
    download->snapshot_.completedLength = 0;
    download->snapshot_.sessionDownloadLength = 0;
    download->snapshot_.state = CurlSnapshot::State::Active;
    return true;
  }

  if (impl.startMode == CurlStartMode::InspectExisting) {
    download->snapshot_.completedLength = 0;
    download->snapshot_.sessionDownloadLength = 0;
    download->snapshot_.state = CurlSnapshot::State::Active;
    return true;
  }

  const bool preserveExisting =
      restoreState || impl.planner.completedLength() > 0;
  if (!openOutput(download, preserveExisting)) {
    return false;
  }
  download->snapshot_.completedLength = impl.planner.completedLength();
  download->snapshot_.sessionDownloadLength = 0;
  download->snapshot_.state = CurlSnapshot::State::Active;
  return true;
}

void CurlSession::restorePaused(const std::shared_ptr<CurlDownload>& download,
                                RequestGroup* group)
{
  auto& impl = *download->impl_;
  if (download->snapshot_.state != CurlSnapshot::State::Waiting ||
      impl.uris.empty()) {
    return;
  }
  impl.path = outputPath(group, impl.uris.front());
  download->snapshot_.state = CurlSnapshot::State::Paused;
  auto context = group->getDownloadContext();
  context->setBasePath(impl.path);
  context->getFirstFileEntry()->setPath(impl.path);

  StreamState state;
  File file(impl.path);
  const auto groupId = GroupId::toHex(group->getGID());
  if (!store_.load(state, groupId, impl.path) || state.gid != groupId ||
      state.path != impl.path || !file.isFile() ||
      std::find(impl.uris.begin(), impl.uris.end(), state.uri) ==
          impl.uris.end()) {
    return;
  }
  const auto size = file.size();
  if (size < state.completedLength ||
      std::any_of(
          state.completedRanges.begin(), state.completedRanges.end(),
          [size](const auto& range) { return range.second > size; })) {
    return;
  }
  impl.group = group;
  impl.planner.restore(state.completedRanges);
  impl.etag = normalizeStrongEtag(state.etag);
  impl.lastModified = state.lastModified;
  impl.currentUri = state.uri;
  download->snapshot_.currentUri = state.uri;
  download->snapshot_.totalLength = state.totalLength;
  download->snapshot_.completedLength = impl.planner.completedLength();
  context->getFirstFileEntry()->setLength(state.totalLength);
  context->markTotalLengthIsKnown();
}

bool CurlSession::createHandle(const std::shared_ptr<CurlDownload>& download,
                               const RangeLease& lease, bool primary,
                               bool ranged, CurlHandlePurpose purpose,
                               long addressFamily)
{
  auto& impl = *download->impl_;
  const auto taskOption = impl.group->getOption().get();
  auto transfer = std::unique_ptr<CurlHandle>(new CurlHandle());
  transfer->download = download.get();
  transfer->lease = lease;
  transfer->connectionEpoch = impl.connectionEpoch;
  transfer->writeOffset = lease.begin;
  transfer->bufferLimit = static_cast<size_t>(std::max<int64_t>(
      256_k, std::min<int64_t>(1_m, 32_m / impl.maxConnections)));
  transfer->writeBuffer.reserve(transfer->bufferLimit);
  transfer->primary = primary;
  transfer->purpose = purpose;
  transfer->ranged = impl.http && ranged && !impl.dryRun;
  auto* easy = curl_easy_init();
  if (!easy) {
    fail(download.get(), error_code::NETWORK_PROBLEM,
         "curl_easy_init returned null");
    return false;
  }
  transfer->value = easy;
  auto*& headers = transfer->headers;
  auto setOption = [&](CURLoption option, auto value, const char* name) {
    const auto result = curl_easy_setopt(easy, option, value);
    if (result == CURLE_OK) {
      return true;
    }
    const auto message =
        fmt("Unable to configure %s: %s", name, curl_easy_strerror(result));
    A2_LOG_ERROR(fmt("component=stream event=handle_setup_failed gid=%s "
                     "option=%s curl=%d message=%s",
                     gid(download.get()).c_str(), name,
                     static_cast<int>(result),
                     logging::sanitizeText(message).c_str()));
    fail(download.get(), error_code::NETWORK_PROBLEM, message);
    return false;
  };
  auto appendHeader = [&](const std::string& value) {
    auto* replacement = curl_slist_append(headers, value.c_str());
    if (replacement) {
      headers = replacement;
      return true;
    }
    fail(download.get(), error_code::UNKNOWN_ERROR,
         "Unable to allocate HTTP request headers");
    return false;
  };
#define SET_CURL_OPTION(name, value)                                           \
  do {                                                                         \
    if (!setOption(name, value, #name)) {                                      \
      cleanupHandle(*transfer);                                                \
      return false;                                                            \
    }                                                                          \
  } while (0)
  const auto uriIndex = lease.uriIndex % impl.uris.size();
  auto& endpoint = endpointFor(impl, uriIndex, addressFamily);
  const auto& originalUri = impl.uris[uriIndex];
  const auto& uriValue = endpoint.uri.empty() ? originalUri : endpoint.uri;
  transfer->endpointGeneration = endpoint.generation;
  transfer->resolvingEndpoint = impl.http && endpoint.uri.empty();
  transfer->redirectedEndpoint =
      !endpoint.uri.empty() && uriValue != originalUri;
  transfer->addressFamily = addressFamily;
  const bool sourceCredentials = sameOrigin(originalUri, uriValue);
  impl.currentUri = originalUri;
  download->snapshot_.currentUri = originalUri;
  markUriUsed(impl.group, originalUri);
  SET_CURL_OPTION(CURLOPT_URL, uriValue.c_str());
  if (share_) {
    SET_CURL_OPTION(CURLOPT_SHARE, share_);
  }
  SET_CURL_OPTION(CURLOPT_PRIVATE, transfer.get());
  SET_CURL_OPTION(CURLOPT_ERRORBUFFER, transfer->errorBuffer.data());
  SET_CURL_OPTION(CURLOPT_DEBUGFUNCTION, debugCallback);
  SET_CURL_OPTION(CURLOPT_DEBUGDATA, transfer.get());
  SET_CURL_OPTION(CURLOPT_VERBOSE,
                  A2_LOG_ENABLED(spdlog::level::trace) ? 1L : 0L);
  SET_CURL_OPTION(CURLOPT_PROTOCOLS_STR, "http,https,sftp");
  SET_CURL_OPTION(CURLOPT_REDIR_PROTOCOLS_STR, "http,https,sftp");
  SET_CURL_OPTION(CURLOPT_FOLLOWLOCATION, 1L);
  SET_CURL_OPTION(CURLOPT_SUPPRESS_CONNECT_HEADERS, 1L);
  SET_CURL_OPTION(CURLOPT_FAILONERROR, 1L);
  SET_CURL_OPTION(CURLOPT_MAXREDIRS, 10L);
  // Parallel ranges retain independent connections rather than sharing one
  // HTTP/2 congestion and flow-control window. Ordinary transfers negotiate.
  SET_CURL_OPTION(CURLOPT_HTTP_VERSION,
                  transfer->ranged && impl.maxConnections > 1
                      ? CURL_HTTP_VERSION_1_1
                      : CURL_HTTP_VERSION_2TLS);
  SET_CURL_OPTION(CURLOPT_NOSIGNAL, 1L);
  SET_CURL_OPTION(CURLOPT_BUFFERSIZE, 1024L * 1024L);
  SET_CURL_OPTION(CURLOPT_TCP_KEEPALIVE, 1L);
  SET_CURL_OPTION(CURLOPT_DNS_CACHE_TIMEOUT, 300L);
  SET_CURL_OPTION(CURLOPT_SOCKOPTFUNCTION, socketOptionCallback);
  SET_CURL_OPTION(CURLOPT_SOCKOPTDATA, const_cast<Option*>(taskOption));
  SET_CURL_OPTION(CURLOPT_HTTPAUTH, CURLAUTH_ANY);
  SET_CURL_OPTION(CURLOPT_NOBODY,
                  impl.dryRun || purpose == CurlHandlePurpose::HeadProbe ? 1L
                                                                         : 0L);
  SET_CURL_OPTION(CURLOPT_FILETIME,
                  taskOption->getAsBool(PREF_REMOTE_TIME) ? 1L : 0L);
  SET_CURL_OPTION(CURLOPT_WRITEFUNCTION, writeData);
  SET_CURL_OPTION(CURLOPT_WRITEDATA, transfer.get());
  SET_CURL_OPTION(CURLOPT_HEADERFUNCTION, receiveHeader);
  SET_CURL_OPTION(CURLOPT_HEADERDATA, transfer.get());
  SET_CURL_OPTION(CURLOPT_XFERINFOFUNCTION, updateProgress);
  SET_CURL_OPTION(CURLOPT_XFERINFODATA, transfer.get());
  SET_CURL_OPTION(CURLOPT_NOPROGRESS, 0L);
  SET_CURL_OPTION(CURLOPT_CONNECTTIMEOUT,
                  taskOption->getAsInt(PREF_CONNECT_TIMEOUT));
  SET_CURL_OPTION(CURLOPT_LOW_SPEED_LIMIT,
                  std::max(1, taskOption->getAsInt(PREF_LOWEST_SPEED_LIMIT)));
  SET_CURL_OPTION(CURLOPT_LOW_SPEED_TIME, taskOption->getAsInt(PREF_TIMEOUT));
  SET_CURL_OPTION(CURLOPT_USERAGENT, taskOption->get(PREF_USER_AGENT).c_str());
  SET_CURL_OPTION(CURLOPT_FORBID_REUSE,
                  taskOption->getAsBool(PREF_ENABLE_HTTP_KEEP_ALIVE) ? 0L : 1L);
  if (taskOption->getAsBool(PREF_HTTP_ACCEPT_GZIP) && !transfer->ranged) {
    SET_CURL_OPTION(CURLOPT_ACCEPT_ENCODING, "");
  }
  if (taskOption->getAsBool(PREF_HTTP_NO_CACHE)) {
    if (!appendHeader("Cache-Control: no-cache") ||
        !appendHeader("Pragma: no-cache")) {
      cleanupHandle(*transfer);
      return false;
    }
  }
  if (const auto result = configureTls(transfer->value, taskOption);
      result != CURLE_OK) {
    const auto message =
        fmt("Cannot configure TLS: %s", curl_easy_strerror(result));
    A2_LOG_ERROR(message);
    fail(download.get(), error_code::NETWORK_PROBLEM, message);
    cleanupHandle(*transfer);
    return false;
  }
  if (!taskOption->blank(PREF_INTERFACE)) {
    SET_CURL_OPTION(CURLOPT_INTERFACE, taskOption->get(PREF_INTERFACE).c_str());
  }
  if (taskOption->getAsBool(PREF_DISABLE_IPV6)) {
    SET_CURL_OPTION(CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
  }
  else if (transfer->addressFamily != CURL_IPRESOLVE_WHATEVER) {
    SET_CURL_OPTION(CURLOPT_IPRESOLVE, transfer->addressFamily);
  }
  if (!taskOption->blank(PREF_PRIVATE_KEY)) {
    SET_CURL_OPTION(CURLOPT_SSH_PRIVATE_KEYFILE,
                    taskOption->get(PREF_PRIVATE_KEY).c_str());
  }
  if (impl.http && sourceCredentials && !taskOption->blank(PREF_HTTP_USER)) {
    SET_CURL_OPTION(CURLOPT_USERNAME, taskOption->get(PREF_HTTP_USER).c_str());
    SET_CURL_OPTION(CURLOPT_PASSWORD,
                    taskOption->get(PREF_HTTP_PASSWD).c_str());
  }
  else if (!impl.http && !taskOption->blank(PREF_SFTP_USER)) {
    SET_CURL_OPTION(CURLOPT_USERNAME, taskOption->get(PREF_SFTP_USER).c_str());
    SET_CURL_OPTION(CURLOPT_PASSWORD,
                    taskOption->get(PREF_SFTP_PASSWD).c_str());
  }
  else if (!taskOption->getAsBool(PREF_NO_NETRC)) {
    SET_CURL_OPTION(CURLOPT_NETRC, CURL_NETRC_OPTIONAL);
    if (!taskOption->blank(PREF_NETRC_PATH)) {
      SET_CURL_OPTION(CURLOPT_NETRC_FILE,
                      taskOption->get(PREF_NETRC_PATH).c_str());
    }
  }
  if (!impl.http && !taskOption->blank(PREF_SSH_HOST_KEY_SHA256)) {
    SET_CURL_OPTION(CURLOPT_SSH_HOST_PUBLIC_KEY_SHA256,
                    taskOption->get(PREF_SSH_HOST_KEY_SHA256).c_str());
  }
  if (!taskOption->blank(PREF_REFERER)) {
    SET_CURL_OPTION(CURLOPT_REFERER, taskOption->get(PREF_REFERER).c_str());
  }
  const auto proxy = proxyFor(taskOption, uriValue);
  if (!proxy.empty()) {
    SET_CURL_OPTION(CURLOPT_PROXY, proxy.c_str());
    uri::UriStruct parsed;
    uri::parse(parsed, uriValue);
    const auto proxyUser = proxyUserFor(taskOption, parsed.protocol);
    const auto proxyPassword = proxyPasswordFor(taskOption, parsed.protocol);
    if (!proxyUser.empty()) {
      SET_CURL_OPTION(CURLOPT_PROXYUSERNAME, proxyUser.c_str());
      SET_CURL_OPTION(CURLOPT_PROXYPASSWORD, proxyPassword.c_str());
    }
  }
  if (!taskOption->blank(PREF_NO_PROXY)) {
    SET_CURL_OPTION(CURLOPT_NOPROXY, taskOption->get(PREF_NO_PROXY).c_str());
  }
  if (impl.http) {
    SET_CURL_OPTION(CURLOPT_COOKIEFILE, "");
  }
  if (!taskOption->blank(PREF_LOAD_COOKIES)) {
    SET_CURL_OPTION(CURLOPT_COOKIEFILE,
                    taskOption->get(PREF_LOAD_COOKIES).c_str());
  }
  if (!taskOption->blank(PREF_SAVE_COOKIES)) {
    SET_CURL_OPTION(CURLOPT_COOKIEJAR,
                    taskOption->get(PREF_SAVE_COOKIES).c_str());
  }
  std::istringstream configuredHeaders(taskOption->get(PREF_HEADER));
  std::string header;
  while (std::getline(configuredHeaders, header)) {
    // libcurl suppresses these on a cross-origin redirect. Starting a new
    // range at that destination must preserve the same credential boundary.
    if (!sourceCredentials && (startsWithHeader(header, "Authorization:") ||
                               startsWithHeader(header, "Cookie:"))) {
      continue;
    }
    if (!header.empty() && !appendHeader(header)) {
      cleanupHandle(*transfer);
      return false;
    }
  }
  if (transfer->ranged) {
    transfer->range =
        lease.end == std::numeric_limits<int64_t>::max()
            ? std::to_string(lease.begin) + '-'
            : std::to_string(lease.begin) + '-' + std::to_string(lease.end - 1);
    SET_CURL_OPTION(CURLOPT_RANGE, transfer->range.c_str());
  }
  else if (lease.begin > 0) {
    SET_CURL_OPTION(CURLOPT_RESUME_FROM_LARGE,
                    static_cast<curl_off_t>(lease.begin));
  }
  // Range validators qualify the requested representation, not a token or
  // redirect endpoint. libcurl owns redirects; response validation prevents a
  // full 200 response from being written at the requested range offset.
  if (transfer->ranged && purpose == CurlHandlePurpose::Payload) {
    transfer->rangeValidator =
        !impl.etag.empty() ? impl.etag : impl.lastModified;
    if (!transfer->rangeValidator.empty() &&
        !appendHeader("If-Range: " + transfer->rangeValidator)) {
      cleanupHandle(*transfer);
      return false;
    }
  }
  if (headers) {
    SET_CURL_OPTION(CURLOPT_HTTPHEADER, headers);
  }
  const auto addResult = curl_multi_add_handle(multi_, easy);
  const auto result = addResult == CURLM_OK;
  if (result) {
    if (transfer->resolvingEndpoint) {
      endpoint.resolving = true;
    }
    impl.handles.push_back(std::move(transfer));
  }
  else {
    const auto message = std::string("Unable to add curl transfer: ") +
                         curl_multi_strerror(addResult);
    A2_LOG_ERROR(fmt("component=stream event=handle_add_failed gid=%s "
                     "curlm=%d message=%s",
                     gid(download.get()).c_str(), static_cast<int>(addResult),
                     logging::sanitizeText(message).c_str()));
    fail(download.get(), error_code::NETWORK_PROBLEM, message);
    cleanupHandle(*transfer);
  }
#undef SET_CURL_OPTION
  return result;
}

bool CurlSession::startProbe(const std::shared_ptr<CurlDownload>& download,
                             CurlHandlePurpose purpose)
{
  const bool rangeProbe = purpose == CurlHandlePurpose::RangeProbe;
  const RangeLease lease{0,
                         rangeProbe ? 1 : std::numeric_limits<int64_t>::max(),
                         0, download->impl_->preferredUriIndex};
  if (!createHandle(download, lease, true, rangeProbe, purpose)) {
    return false;
  }
  auto* handle = download->impl_->handles.back().get();
  downloads_[handle->value] = std::make_pair(download, handle);
  download->impl_->kickPending = true;
  rebalanceLimits();
  if (engine_) {
    engine_->setNoWait(true);
  }
  return true;
}

std::unique_ptr<Command>
CurlSession::start(const std::shared_ptr<CurlDownload>& download,
                   RequestGroup* group, DownloadEngine* engine)
{
  engine_ = engine;
  constexpr int64_t probeSize = 4_m;
  if (prepare(download, group)) {
    auto& impl = *download->impl_;
    tasks_[download.get()] = download;
    if (!refreshConnectionPoolLimits()) {
      failTask(download, error_code::NETWORK_PROBLEM,
               "Unable to configure the libcurl connection pool", false);
      return std::unique_ptr<Command>(new CurlDownloadCommand(
          engine->newCUID(), download, this, group, engine));
    }
    if (impl.startMode == CurlStartMode::InspectExisting) {
      if (!startProbe(download, CurlHandlePurpose::RangeProbe)) {
        failTask(download, error_code::NETWORK_PROBLEM,
                 "Unable to inspect the existing output file", false);
      }
      return std::unique_ptr<Command>(new CurlDownloadCommand(
          engine->newCUID(), download, this, group, engine));
    }
    if (impl.planner.complete()) {
      finalize(download, -1);
      return std::unique_ptr<Command>(new CurlDownloadCommand(
          engine->newCUID(), download, this, group, engine));
    }
    const auto rangeStart = impl.planner.contiguousLength();
    const bool ranged =
        impl.http && (impl.maxConnections > 1 || rangeStart > 0);
    auto rangeEnd =
        ranged ? rangeStart +
                     (impl.maxConnections > 1
                          ? probeSize
                          : std::numeric_limits<int64_t>::max() - rangeStart)
               : std::numeric_limits<int64_t>::max();
    if (download->snapshot_.totalLength > 0) {
      rangeEnd = std::min(rangeEnd, download->snapshot_.totalLength);
    }
    rangeEnd = impl.planner.gapEnd(rangeStart, rangeEnd);
    const RangeLease lease{rangeStart, rangeEnd, 0, impl.preferredUriIndex};
    if (!createHandle(download, lease, true, ranged,
                      CurlHandlePurpose::Payload)) {
      fail(download.get(), error_code::NETWORK_PROBLEM,
           "Unable to start the curl transfer");
      eraseTask(download.get());
      return std::unique_ptr<Command>(new CurlDownloadCommand(
          engine->newCUID(), download, this, group, engine));
    }
    auto* handle = download->impl_->handles.back().get();
    downloads_[handle->value] = std::make_pair(download, handle);
    rebalanceLimits();
    socketAction(CURL_SOCKET_TIMEOUT, 0);
  }
  else if (!download->failed()) {
    fail(download.get(), error_code::NETWORK_PROBLEM,
         "Unable to start the curl transfer");
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
  StreamState state;
  state.gid = GroupId::toHex(impl.group->getGID());
  state.uri = impl.currentUri;
  state.path = impl.path;
  state.etag = impl.etag;
  state.lastModified = impl.lastModified;
  state.totalLength = download->snapshot_.totalLength;
  state.completedLength = impl.planner.completedLength();
  state.completedRanges = impl.planner.completedRanges();
  if (store_.save(state)) {
    impl.lastCheckpoint = global::wallclock();
  }
}

std::vector<RangeLease>
CurlSession::activeLeases(const std::shared_ptr<CurlDownload>& download) const
{
  std::vector<RangeLease> result;
  for (const auto& entry : download->impl_->handles) {
    if (entry->value) {
      result.push_back(entry->lease);
    }
  }
  return result;
}

void CurlSession::configurePlanner(
    const std::shared_ptr<CurlDownload>& download,
    const RangeLease* retainedLease)
{
  auto& impl = *download->impl_;
  const auto total = download->snapshot_.totalLength;
  if (impl.plannerConfigured || total <= 0) {
    return;
  }
  if (!impl.rangeValidated) {
    return;
  }
  const auto quantum = std::max<int64_t>(
      1_m, impl.group->getOption()->getAsInt(PREF_PIECE_LENGTH));
  const auto rangeSize = adaptiveRangeSize(total, impl.maxConnections, quantum);
  auto active = activeLeases(download);
  if (retainedLease && !retainedLease->empty()) {
    active.push_back(*retainedLease);
  }
  impl.planner.configure(total, rangeSize, active);
  impl.plannerConfigured = true;
  // Workers retain their transport choice as they pull ranges. Fast workers
  // naturally claim more work; no task-wide address-family winner is needed.
  impl.idleWorkers.clear();
  for (int i = 0; i < impl.maxConnections; ++i) {
    impl.idleWorkers.push_back(impl.families[i % impl.families.size()]);
  }
  for (const auto& handle : impl.handles) {
    if (handle->purpose != CurlHandlePurpose::Payload) {
      continue;
    }
    const auto worker =
        std::find(impl.idleWorkers.begin(), impl.idleWorkers.end(),
                  handle->addressFamily);
    if (worker != impl.idleWorkers.end()) {
      impl.idleWorkers.erase(worker);
    }
  }

  const auto taskOption = impl.group->getOption();
  const bool sizeOutput =
      taskOption->get(PREF_FILE_ALLOCATION) != V_NONE &&
      total >= taskOption->getAsLLInt(PREF_NO_FILE_ALLOCATION_LIMIT);
  if (!sizeOutput || !impl.writer) {
    return;
  }
  try {
    impl.writer->truncate(total);
  }
  catch (const Exception& error) {
    failTask(download, error.getErrorCode(), error.what());
  }
  catch (const std::exception& error) {
    failTask(download, error_code::FILE_IO_ERROR, error.what());
  }
}

void CurlSession::schedule(const std::shared_ptr<CurlDownload>& download)
{
  auto& impl = *download->impl_;
  if (impl.stopRequested || download->stopped()) {
    return;
  }
  configurePlanner(download);
  if (download->failed()) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  if (!impl.plannerConfigured) {
    if (impl.handles.empty()) {
      if (auto lease = impl.planner.takeReady(now)) {
        const bool ranged =
            impl.http && (impl.maxConnections > 1 || lease->begin > 0);
        if (!createHandle(download, *lease, true, ranged,
                          CurlHandlePurpose::Payload)) {
          failTask(download, error_code::NETWORK_PROBLEM,
                   "Unable to restart the stream transfer");
          return;
        }
        auto* handle = impl.handles.back().get();
        downloads_[handle->value] = std::make_pair(download, handle);
        impl.kickPending = true;
        rebalanceLimits();
      }
    }
    if (const auto deadline = impl.planner.nextDeadline()) {
      engine_->setRefreshInterval(
          std::max(std::chrono::milliseconds(0),
                   std::chrono::duration_cast<std::chrono::milliseconds>(
                       *deadline - now)));
    }
    return;
  }

  rewardConnectionLimit(download);
  const auto pieceLength = std::max<int64_t>(
      1_m, impl.group->getOption()->getAsInt(PREF_PIECE_LENGTH));
  bool scheduled = false;
  for (;;) {
    const auto remaining =
        std::max<int64_t>(0, download->snapshot_.totalLength -
                                 download->snapshot_.completedLength);
    const auto preferredPiece =
        adaptiveRangeSize(remaining, impl.connectionLimit, pieceLength);
    impl.planner.refillReady(static_cast<size_t>(impl.connectionLimit),
                             preferredPiece, pieceLength);
    auto available = impl.idleWorkers.size();
    while (impl.handles.size() < static_cast<size_t>(impl.connectionLimit) &&
           available-- > 0) {
      auto lease = impl.planner.takeReady(now);
      if (!lease) {
        break;
      }
      auto family = impl.idleWorkers.front();
      impl.idleWorkers.pop_front();
      if (endpointFor(impl, lease->uriIndex, family).unavailable) {
        family = otherFamily(family);
      }
      auto& endpoint = endpointFor(impl, lease->uriIndex, family);
      if (impl.http && endpoint.uri.empty() &&
          (endpoint.resolving || endpoint.readyAt > now)) {
        impl.planner.enqueue(*lease);
        impl.idleWorkers.push_back(family);
        if (endpoint.readyAt > now) {
          engine_->setRefreshInterval(
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  endpoint.readyAt - now));
        }
        continue;
      }
      if (!createHandle(download, *lease, false, true,
                        CurlHandlePurpose::Payload, family)) {
        failTask(download, error_code::NETWORK_PROBLEM,
                 "Unable to create a ranged transfer");
        return;
      }
      auto* handle = impl.handles.back().get();
      downloads_[handle->value] = std::make_pair(download, handle);
      scheduled = true;
    }
    if (!impl.planner.hasReady(now) &&
        rebalanceEndgame(download, pieceLength)) {
      continue;
    }
    break;
  }

  if (const auto deadline = impl.planner.nextDeadline()) {
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - now);
    engine_->setRefreshInterval(
        std::max(std::chrono::milliseconds(0), remaining));
  }
  if (scheduled) {
    impl.kickPending = true;
    rebalanceLimits();
  }
  if (impl.handles.empty() && !impl.planner.hasPending() &&
      !impl.planner.complete() && !download->stopped()) {
    failTask(download, error_code::UNKNOWN_ERROR,
             fmt("Stream scheduler lost an unfinished range: completed=%" PRId64
                 " total=%" PRId64,
                 impl.planner.completedLength(), impl.planner.totalLength()));
  }
}

bool CurlSession::rebalanceEndgame(
    const std::shared_ptr<CurlDownload>& download, int64_t pieceLength)
{
  auto& impl = *download->impl_;
  const auto active = impl.handles.size();
  if (active == 0) {
    return false;
  }
  const auto capacity = static_cast<size_t>(impl.connectionLimit);
  if (capacity <= 1 || active >= capacity || globalDownloadLimit_ > 0 ||
      impl.group->getMaxDownloadSpeedLimit() > 0) {
    return false;
  }
  // Deferred retries still own work. Creating helpers during their cooldown
  // would bypass backoff; an in-flight retry must receive data first.
  if (impl.planner.hasPending() ||
      std::any_of(impl.handles.begin(), impl.handles.end(),
                  [](const auto& handle) {
                    return handle->lease.attempts > 0 &&
                           handle->writeOffset == handle->lease.begin;
                  })) {
    return false;
  }
  // Assign idle workers the largest unfinished suffix, preserving the live
  // prefix. Splitting does not need to predict a connection's future speed.
  CurlHandle* handle = nullptr;
  int64_t largest = 0;
  long double longest = 0;
  int64_t splitQuantum = pieceLength;
  const auto& now = global::wallclock();
  for (const auto& candidate : impl.handles) {
    const auto remaining = candidate->lease.end - candidate->writeOffset;
    if (!candidate->rangeAccepted || remaining <= 0) {
      continue;
    }
    if (remaining >= 2 * 64_k) {
      if (remaining > largest) {
        largest = remaining;
        handle = candidate.get();
        splitQuantum = 64_k;
      }
      continue;
    }
    // Small tails cannot be split. Preserve fresh requests until measured
    // remaining time justifies paying for another request.
    if (largest > 0 || candidate->bodySampleStart.isZero()) {
      continue;
    }
    curl_off_t firstByte = 0;
    curl_easy_getinfo(candidate->value, CURLINFO_STARTTRANSFER_TIME_T,
                      &firstByte);
    const auto requestCost = std::max(1.0L, firstByte / 1000000.0L);
    const auto bodySeconds = std::chrono::duration<long double>(
                                 candidate->bodySampleStart.difference(now))
                                 .count();
    if (bodySeconds < std::max(2.0L, requestCost)) {
      continue;
    }
    const auto speed = candidate->payloadSpeed.calculateNewestSpeed(2);
    const auto idleSeconds = std::chrono::duration<long double>(
                                 candidate->lastPayload.difference(now))
                                 .count();
    if (speed == 0 && idleSeconds < requestCost * 2) {
      continue;
    }
    const auto remainingTime =
        speed > 0 ? static_cast<long double>(remaining) / speed
                  : std::numeric_limits<long double>::infinity();
    const auto quantum =
        std::max<int64_t>(64_k, static_cast<int64_t>(std::min<long double>(
                                    pieceLength, speed * requestCost)));
    if (remainingTime > requestCost * 2 && remainingTime > longest) {
      handle = candidate.get();
      longest = remainingTime;
      splitQuantum = quantum;
    }
  }
  if (!handle) {
    return false;
  }
  try {
    flushWriteBuffer(impl, *handle);
    download->snapshot_.completedLength =
        impl.planner.completedLength() + bufferedLength(impl);
  }
  catch (const Exception& error) {
    failTask(download, error.getErrorCode(), error.what());
    return false;
  }
  catch (const std::exception& error) {
    failTask(download, error_code::FILE_IO_ERROR, error.what());
    return false;
  }

  auto remainder = handle->lease.remainder(handle->writeOffset);
  if (remainder.length() >= splitQuantum * 2) {
    remainder.begin += (remainder.length() / 2 / splitQuantum) * splitQuantum;
    handle->lease.end = remainder.begin;
    impl.planner.enqueue(remainder);
    A2_LOG_DEBUG(fmt("component=stream event=tail_assisted gid=%s "
                     "range=%" PRId64 "-%" PRId64,
                     gid(download.get()).c_str(), remainder.begin,
                     remainder.end));
    return true;
  }
  auto found = downloads_.find(handle->value);
  if (found != downloads_.end()) {
    const auto result = curl_multi_remove_handle(multi_, handle->value);
    if (result != CURLM_OK) {
      A2_LOG_WARN(fmt("component=stream event=handle_remove_failed gid=%s "
                      "curlm=%d message=%s",
                      gid(download.get()).c_str(), static_cast<int>(result),
                      curl_multi_strerror(result)));
    }
    downloads_.erase(found);
  }
  cleanupHandle(*handle);
  eraseHandle(impl, handle);
  impl.planner.enqueue(remainder);
  A2_LOG_DEBUG(fmt("component=stream event=tail_reassigned gid=%s "
                   "range=%" PRId64 "-%" PRId64,
                   gid(download.get()).c_str(), remainder.begin,
                   remainder.end));
  return true;
}

void CurlSession::cancelHandles(const std::shared_ptr<CurlDownload>& download)
{
  auto& impl = *download->impl_;
  for (auto& handle : impl.handles) {
    if (handle->value) {
      auto found = downloads_.find(handle->value);
      if (found != downloads_.end()) {
        const auto result = curl_multi_remove_handle(multi_, handle->value);
        if (result != CURLM_OK) {
          A2_LOG_WARN(fmt("component=stream event=handle_remove_failed gid=%s "
                          "curlm=%d message=%s",
                          gid(download.get()).c_str(), static_cast<int>(result),
                          curl_multi_strerror(result)));
        }
        downloads_.erase(found);
      }
      cleanupHandle(*handle);
    }
  }
  impl.handles.clear();
  impl.idleWorkers.clear();
  download->snapshot_.connections = 0;
}

void CurlSession::restartFullDownload(
    const std::shared_ptr<CurlDownload>& download)
{
  auto& impl = *download->impl_;
  if (!impl.allowFullRestart || impl.fullDownload) {
    failTask(
        download, error_code::CANNOT_RESUME,
        "The server ignored the byte range; restarting requires permission "
        "to overwrite the existing file");
    return;
  }
  cancelHandles(download);
  closeOutput(download.get());
  store_.removePath(impl.path);
  impl.planner.clear();
  impl.plannerConfigured = false;
  impl.rangeValidated = false;
  impl.fullDownload = true;
  download->snapshot_.completedLength = 0;
  if (!openOutput(download, false)) {
    failTask(download, download->snapshot_.errorCode, download->snapshot_.error,
             false);
    return;
  }
  const RangeLease lease{0, std::numeric_limits<int64_t>::max(), 0,
                         impl.preferredUriIndex};
  if (!createHandle(download, lease, true, false, CurlHandlePurpose::Payload)) {
    failTask(download, error_code::NETWORK_PROBLEM,
             "Unable to restart the complete transfer");
    return;
  }
  auto* handle = impl.handles.back().get();
  downloads_[handle->value] = std::make_pair(download, handle);
  impl.kickPending = true;
  rebalanceLimits();
  checkpoint(download, true);
  engine_->setNoWait(true);
  A2_LOG_INFO(fmt("component=stream event=full_download_restart gid=%s "
                  "reason=range_ignored",
                  gid(download.get()).c_str()));
}

void CurlSession::finalize(const std::shared_ptr<CurlDownload>& download,
                           curl_off_t reportedFileTime)
{
  auto& impl = *download->impl_;
  closeOutput(download.get());
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
  eraseTask(download.get());
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

void CurlSession::failTask(const std::shared_ptr<CurlDownload>& download,
                           error_code::Value errorCode,
                           const std::string& message, bool retainState)
{
  auto& impl = *download->impl_;
  auto finalMessage = message;
  if (retainState) {
    if (!download->failed()) {
      try {
        for (auto& handle : impl.handles) {
          flushWriteBuffer(impl, *handle);
        }
        download->snapshot_.completedLength = impl.planner.completedLength();
      }
      catch (const Exception& error) {
        errorCode = error.getErrorCode();
        finalMessage = error.what();
      }
      catch (const std::exception& error) {
        errorCode = error_code::FILE_IO_ERROR;
        finalMessage = error.what();
      }
    }
    checkpoint(download, true);
  }
  else if (impl.group) {
    store_.removePath(impl.path);
  }
  cancelHandles(download);
  closeOutput(download.get());
  fail(download.get(), errorCode, finalMessage);
  A2_LOG_ERROR(fmt("component=stream event=task_failed gid=%s error_code=%d "
                   "completed=%" PRId64 " uri=%s message=%s",
                   gid(download.get()).c_str(), static_cast<int>(errorCode),
                   download->snapshot_.completedLength,
                   logging::sanitizeUri(impl.currentUri).c_str(),
                   logging::sanitizeText(finalMessage).c_str()));
  eraseTask(download.get());
  if (engine_) {
    engine_->setNoWait(true);
  }
}

std::optional<std::chrono::milliseconds>
CurlSession::retryRange(const std::shared_ptr<CurlDownload>& download,
                        const RangeLease& failed, curl_off_t retryAfter)
{
  auto& impl = *download->impl_;
  auto retry = failed;
  ++retry.attempts;
  const auto maxTries = impl.group->getOption()->getAsInt(PREF_MAX_TRIES);
  if (maxTries > 0 && retry.attempts >= static_cast<size_t>(maxTries)) {
    return std::nullopt;
  }
  if (impl.uris.size() > 1) {
    retry.uriIndex = (retry.uriIndex + 1) % impl.uris.size();
  }
  const auto configured = impl.group->getOption()->getAsInt(PREF_RETRY_WAIT);
  const auto wait =
      std::max<curl_off_t>(configured, std::max<curl_off_t>(0, retryAfter));
  const auto backoff =
      std::min<long>(30000, 100L << std::min<size_t>(retry.attempts - 1, 9));
  const auto jitter = std::chrono::milliseconds(
      SimpleRandomizer::getInstance()->getRandomNumber(backoff + 1));
  const auto delay = std::chrono::seconds(wait) + jitter;
  impl.planner.defer(retry, std::chrono::steady_clock::now() + delay);
  engine_->setNoWait(true);
  return delay;
}

void CurlSession::penalizeConnectionLimit(
    const std::shared_ptr<CurlDownload>& download, uint64_t requestEpoch)
{
  auto& impl = *download->impl_;
  // An overload response reduces one request generation once. Requests still
  // waiting for a response provide no evidence of the origin's capacity.
  if (requestEpoch != impl.connectionEpoch) {
    return;
  }
  ++impl.connectionEpoch;
  const auto previous = impl.connectionLimit;
  const auto admitted = static_cast<int>(std::count_if(
      impl.handles.begin(), impl.handles.end(), [](const auto& handle) {
        return handle->rangeAccepted &&
               handle->writeOffset > handle->lease.begin;
      }));
  impl.connectionLimit =
      std::clamp(std::max(previous / 2, admitted), 1, impl.maxConnections);
  impl.lastRecoveryDownloadLength = download->snapshot_.sessionDownloadLength;
  impl.recoverConnectionsAt =
      std::chrono::steady_clock::now() + std::chrono::seconds(1);
  if (impl.connectionLimit != previous) {
    A2_LOG_DEBUG(fmt("component=stream event=connection_limit_reduced gid=%s "
                     "previous=%d current=%d",
                     gid(download.get()).c_str(), previous,
                     impl.connectionLimit));
  }
}

void CurlSession::rewardConnectionLimit(
    const std::shared_ptr<CurlDownload>& download)
{
  auto& impl = *download->impl_;
  const auto now = std::chrono::steady_clock::now();
  if (impl.connectionLimit >= impl.maxConnections ||
      download->snapshot_.sessionDownloadLength <=
          impl.lastRecoveryDownloadLength) {
    return;
  }
  const auto previous = impl.connectionLimit;
  const auto admitted = static_cast<int>(std::count_if(
      impl.handles.begin(), impl.handles.end(), [](const auto& handle) {
        return handle->rangeAccepted &&
               handle->writeOffset > handle->lease.begin;
      }));
  // Late payload corrects the observed floor immediately. Further growth is
  // paced and requires fresh bytes, not completion of an entire long range.
  impl.connectionLimit =
      std::min(impl.maxConnections,
               std::max(admitted,
                        previous + (now >= impl.recoverConnectionsAt ? 1 : 0)));
  if (impl.connectionLimit == previous) {
    return;
  }
  impl.lastRecoveryDownloadLength = download->snapshot_.sessionDownloadLength;
  impl.recoverConnectionsAt = now + std::chrono::seconds(1);
  A2_LOG_DEBUG(fmt("component=stream event=connection_limit_increased gid=%s "
                   "current=%d maximum=%d",
                   gid(download.get()).c_str(), impl.connectionLimit,
                   impl.maxConnections));
}

void CurlSession::advance(const std::shared_ptr<CurlDownload>& download)
{
  schedule(download);
  if (download->impl_->kickPending) {
    download->impl_->kickPending = false;
    socketAction(CURL_SOCKET_TIMEOUT, 0);
  }
}

void CurlSession::finishProbe(const std::shared_ptr<CurlDownload>& download,
                              CurlHandle* handle, CURLcode result,
                              long responseCode, curl_off_t reportedLength,
                              curl_off_t reportedFileTime)
{
  auto& impl = *download->impl_;
  const auto purpose = handle->purpose;
  const auto nativeFailure = failureMessage(*handle, result, responseCode);
  const auto contentLength = handle->responseContentLength >= 0
                                 ? handle->responseContentLength
                                 : static_cast<int64_t>(reportedLength);
  int64_t remoteLength = -1;
  bool rangeSupported = false;
  if (purpose == CurlHandlePurpose::RangeProbe) {
    if (responseCode == 206 && handle->rangeAccepted) {
      remoteLength = handle->responseTotalLength;
      rangeSupported = true;
    }
    else if (responseCode == 200 && handle->fullResponseAccepted &&
             contentLength >= 0) {
      remoteLength = contentLength;
    }
    else if (responseCode == 416 && handle->unsatisfiedTotalLength >= 0) {
      remoteLength = handle->unsatisfiedTotalLength;
    }
  }
  else if (responseCode >= 200 && responseCode < 300 && contentLength >= 0) {
    remoteLength = contentLength;
  }

  const auto responseEtag = handle->responseEtag;
  const auto responseLastModified = handle->responseLastModified;
  const auto responseDate = handle->responseDate;
  const bool invalidRange =
      handle->responseFailure == CurlResponseFailure::InvalidRange;
  cleanupHandle(*handle);
  eraseHandle(impl, handle);

  if (remoteLength < 0 && purpose == CurlHandlePurpose::RangeProbe) {
    if (startProbe(download, CurlHandlePurpose::HeadProbe)) {
      return;
    }
    failTask(download, error_code::NETWORK_PROBLEM,
             "Unable to inspect the remote file", false);
    return;
  }
  if (remoteLength < 0) {
    failTask(download,
             responseCode >= 400 ? curlErrorCode(result, responseCode)
                                 : error_code::CANNOT_RESUME,
             responseCode >= 400 ? nativeFailure
                                 : "The remote file length is unavailable",
             false);
    return;
  }
  if (invalidRange) {
    failTask(download, error_code::HTTP_PROTOCOL_ERROR,
             "The server returned an invalid Content-Range response", false);
    return;
  }

  rememberIdentity(impl, responseEtag, responseLastModified, responseDate);
  download->snapshot_.totalLength = remoteLength;

  switch (
      decideExistingFile(impl.existingLength, remoteLength, rangeSupported)) {
  case ExistingFileDecision::Complete:
    impl.planner.clear();
    impl.planner.commit(0, remoteLength);
    impl.planner.configure(remoteLength, std::max<int64_t>(1, remoteLength),
                           {});
    impl.plannerConfigured = true;
    finalize(download, reportedFileTime);
    return;
  case ExistingFileDecision::Resume:
    if (!openOutput(download, true)) {
      failTask(download, download->snapshot_.errorCode,
               download->snapshot_.error, false);
      return;
    }
    impl.planner.clear();
    impl.planner.commit(0, impl.existingLength);
    impl.rangeValidated = true;
    impl.startMode = CurlStartMode::Transfer;
    download->snapshot_.completedLength = impl.existingLength;
    configurePlanner(download);
    if (download->failed()) {
      return;
    }
    checkpoint(download, true);
    schedule(download);
    engine_->setNoWait(true);
    return;
  case ExistingFileDecision::Reject:
    break;
  }

  const auto message = impl.existingLength > remoteLength
                           ? "The local file is larger than the remote file"
                           : "The server does not support resuming this file";
  failTask(download, error_code::CANNOT_RESUME, message, false);
}

void CurlSession::finish(const std::shared_ptr<CurlDownload>& download,
                         CurlHandle* handle, CURLcode result)
{
  auto& impl = *download->impl_;
  long responseCode = handle->responseCode;
  if (download->snapshot_.mediaManifest) {
    cancelHandles(download);
    closeOutput(download.get());
    store_.removePath(impl.path);
    if (impl.createdOutput && File(impl.path).size() == 0)
      File(impl.path).remove();
    download->snapshot_.state = CurlSnapshot::State::Stopped;
    eraseTask(download.get());
    return;
  }
  curl_off_t retryAfter = 0;
  curl_off_t reportedLength = 0;
  curl_off_t reportedFileTime = -1;
  curl_off_t reportedSpeed = 0;
  curl_off_t transferId = -1;
  curl_off_t connectionId = -1;
  curl_off_t nameLookupTime = 0;
  curl_off_t connectTime = 0;
  curl_off_t appConnectTime = 0;
  curl_off_t startTransferTime = 0;
  curl_off_t queueTime = 0;
  long newConnections = 0;
  long httpVersion = 0;
  long osError = 0;
  long primaryPort = 0;
  char* primaryIp = nullptr;
  char* effectiveUri = nullptr;
  if (handle->value) {
    curl_easy_getinfo(handle->value, CURLINFO_RESPONSE_CODE, &responseCode);
    curl_easy_getinfo(handle->value, CURLINFO_RETRY_AFTER, &retryAfter);
    curl_easy_getinfo(handle->value, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T,
                      &reportedLength);
    curl_easy_getinfo(handle->value, CURLINFO_FILETIME_T, &reportedFileTime);
    curl_easy_getinfo(handle->value, CURLINFO_SPEED_DOWNLOAD_T, &reportedSpeed);
    curl_easy_getinfo(handle->value, CURLINFO_XFER_ID, &transferId);
    curl_easy_getinfo(handle->value, CURLINFO_CONN_ID, &connectionId);
    curl_easy_getinfo(handle->value, CURLINFO_NAMELOOKUP_TIME_T,
                      &nameLookupTime);
    curl_easy_getinfo(handle->value, CURLINFO_CONNECT_TIME_T, &connectTime);
    curl_easy_getinfo(handle->value, CURLINFO_APPCONNECT_TIME_T,
                      &appConnectTime);
    curl_easy_getinfo(handle->value, CURLINFO_STARTTRANSFER_TIME_T,
                      &startTransferTime);
    curl_easy_getinfo(handle->value, CURLINFO_QUEUE_TIME_T, &queueTime);
    curl_easy_getinfo(handle->value, CURLINFO_NUM_CONNECTS, &newConnections);
    curl_easy_getinfo(handle->value, CURLINFO_HTTP_VERSION, &httpVersion);
    curl_easy_getinfo(handle->value, CURLINFO_OS_ERRNO, &osError);
    curl_easy_getinfo(handle->value, CURLINFO_PRIMARY_IP, &primaryIp);
    curl_easy_getinfo(handle->value, CURLINFO_PRIMARY_PORT, &primaryPort);
    curl_easy_getinfo(handle->value, CURLINFO_EFFECTIVE_URL, &effectiveUri);
  }
  if (result == CURLE_WRITE_ERROR &&
      handle->purpose == CurlHandlePurpose::Payload && handle->rangeAccepted &&
      handle->writeOffset == handle->lease.end &&
      handle->lease.end < handle->responseRangeEnd &&
      download->snapshot_.errorCode == error_code::UNDEFINED) {
    // The native callback stopped a donor after completing its shortened
    // assignment. Its suffix is independently owned by another request.
    result = CURLE_OK;
  }
  const auto nativeFailure = failureMessage(*handle, result, responseCode);
  const auto safePrimaryIp =
      logging::sanitizeText(primaryIp ? primaryIp : "unknown");
  const auto safeEffectiveUri =
      logging::sanitizeUri(effectiveUri ? effectiveUri : impl.currentUri);
  if (handle->purpose != CurlHandlePurpose::Payload) {
    finishProbe(download, handle, result, responseCode, reportedLength,
                reportedFileTime);
    return;
  }
  try {
    flushWriteBuffer(impl, *handle);
    download->snapshot_.completedLength =
        impl.planner.completedLength() + bufferedLength(impl);
  }
  catch (const Exception& error) {
    fail(download.get(), error.getErrorCode(), error.what());
  }
  catch (const std::exception& error) {
    fail(download.get(), error_code::FILE_IO_ERROR, error.what());
  }
  const auto lease = handle->lease;
  const auto writeOffset = handle->writeOffset;
  const auto unsatisfiedTotalLength = handle->unsatisfiedTotalLength;
  const bool ranged = handle->ranged;
  const bool rangeAccepted = handle->rangeAccepted;
  const bool fullResponseAccepted = handle->fullResponseAccepted;
  const auto responseFailure = responseCode == 412
                                   ? CurlResponseFailure::PreconditionFailed
                                   : handle->responseFailure;
  if (responseFailure != CurlResponseFailure::None) {
    A2_LOG_DEBUG(
        fmt("component=stream event=response_rejected gid=%s transfer=%" PRId64
            " reason=%s http=%ld range=%" PRId64 "-%" PRId64
            " expected_etag=%s received_etag=%s expected_modified=%s"
            " received_modified=%s expected_length=%" PRId64
            " received_length=%" PRId64 " uri=%s",
            gid(download.get()).c_str(), static_cast<int64_t>(transferId),
            responseFailureName(responseFailure), responseCode, lease.begin,
            lease.end, logging::sanitizeText(impl.etag).c_str(),
            logging::sanitizeText(handle->responseEtag).c_str(),
            logging::sanitizeText(impl.lastModified).c_str(),
            logging::sanitizeText(handle->responseLastModified).c_str(),
            download->snapshot_.totalLength,
            responseCode == 206 ? handle->responseTotalLength
                                : handle->responseContentLength,
            safeEffectiveUri.c_str()));
  }
  const bool primary = handle->primary;
  const auto requestEpoch = handle->connectionEpoch;
  const auto endpointGeneration = handle->endpointGeneration;
  const bool resolvingEndpoint = handle->resolvingEndpoint;
  const bool redirectedEndpoint = handle->redirectedEndpoint;
  const auto addressFamily = handle->addressFamily;
  auto& endpoint = endpointFor(impl, lease.uriIndex, addressFamily);
  const auto& peer =
      endpointFor(impl, lease.uriIndex, otherFamily(addressFamily));
  const bool unavailableRoute =
      resolvingEndpoint && addressFamily != CURL_IPRESOLVE_WHATEVER &&
      !peer.unavailable && !peer.uri.empty() &&
      (result == CURLE_COULDNT_RESOLVE_HOST ||
       result == CURLE_COULDNT_CONNECT || result == CURLE_OPERATION_TIMEDOUT ||
       (result == CURLE_HTTP_RETURNED_ERROR &&
        (responseCode == 401 || responseCode == 403 || responseCode == 404)));
  const bool expiredEndpoint =
      redirectedEndpoint &&
      (responseCode == 401 || responseCode == 403 || responseCode == 404);
  if (endpointGeneration == endpoint.generation) {
    if (unavailableRoute) {
      endpoint.unavailable = true;
    }
    if (resolvingEndpoint) {
      endpoint.resolving = false;
    }
    if (expiredEndpoint) {
      const auto generation = endpoint.generation + 1;
      endpoint = CurlEndpoint{};
      endpoint.generation = generation;
    }
  }
  const bool outputFailure =
      download->snapshot_.errorCode != error_code::UNDEFINED;
  cleanupHandle(*handle);
  eraseHandle(impl, handle);
  if (primary && rangeAccepted && !impl.plannerConfigured) {
    configurePlanner(download, &lease);
    if (download->failed()) {
      return;
    }
  }

  A2_LOG_TRACE(fmt(
      "component=stream event=range_finished gid=%s transfer=%" PRId64
      " connection=%" PRId64 " range=%" PRId64 "-%" PRId64
      " http=%ld curl=%d os_error=%ld remote=%s:%ld speed=%" PRId64
      " dns_us=%" PRId64 " connect_us=%" PRId64 " tls_us=%" PRId64
      " first_byte_us=%" PRId64 " queue_us=%" PRId64
      " new_connections=%ld http_version=%ld uri=%s",
      gid(download.get()).c_str(), static_cast<int64_t>(transferId),
      static_cast<int64_t>(connectionId), lease.begin, lease.end, responseCode,
      static_cast<int>(result), osError, safePrimaryIp.c_str(), primaryPort,
      static_cast<int64_t>(reportedSpeed), static_cast<int64_t>(nameLookupTime),
      static_cast<int64_t>(connectTime), static_cast<int64_t>(appConnectTime),
      static_cast<int64_t>(startTransferTime), static_cast<int64_t>(queueTime),
      newConnections, httpVersion, safeEffectiveUri.c_str()));

  if (impl.dryRun) {
    download->snapshot_.totalLength = std::max<curl_off_t>(0, reportedLength);
    download->snapshot_.state = CurlSnapshot::State::Complete;
    store_.removePath(impl.path);
    eraseTask(download.get());
    return;
  }

  if (outputFailure) {
    failTask(download, download->snapshot_.errorCode,
             download->snapshot_.error);
    return;
  }
  if (responseFailure != CurlResponseFailure::None) {
    failTask(download,
             responseFailure == CurlResponseFailure::InvalidRange
                 ? error_code::HTTP_PROTOCOL_ERROR
                 : error_code::CANNOT_RESUME,
             responseFailureMessage(responseFailure));
    return;
  }
  if (responseCode == 416) {
    if ((unsatisfiedTotalLength == 0 && lease.begin == 0 &&
         impl.planner.completedLength() == 0 && impl.handles.empty()) ||
        (unsatisfiedTotalLength >= 0 &&
         impl.planner.completedRanges().size() == 1 &&
         impl.planner.completedRanges().front().first == 0 &&
         impl.planner.completedRanges().front().second >=
             unsatisfiedTotalLength)) {
      download->snapshot_.totalLength = unsatisfiedTotalLength;
      impl.planner.configure(unsatisfiedTotalLength,
                             std::max<int64_t>(1, unsatisfiedTotalLength), {});
      impl.plannerConfigured = true;
      finalize(download, reportedFileTime);
      return;
    }
    failTask(download, error_code::CANNOT_RESUME,
             "The requested byte range is no longer satisfiable");
    return;
  }

  if (ranged && !rangeAccepted && !fullResponseAccepted &&
      responseCode == 200) {
    restartFullDownload(download);
    return;
  }

  if (result != CURLE_OK) {
    if (unavailableRoute && endpointGeneration == endpoint.generation) {
      // One unusable route does not invalidate an already validated peer.
      impl.planner.enqueue(lease.remainder(writeOffset));
      schedule(download);
      return;
    }
    if (responseCode == 404) {
      ++impl.fileNotFoundCount;
    }
    const auto maxFileNotFound =
        impl.group->getOption()->getAsInt(PREF_MAX_FILE_NOT_FOUND);
    const bool alternateMirror =
        impl.uris.size() > 1 && result != CURLE_WRITE_ERROR &&
        result != CURLE_OUT_OF_MEMORY && result != CURLE_ABORTED_BY_CALLBACK;
    const bool overloaded = ranged && impl.rangeValidated &&
                            (responseCode == 429 || responseCode == 503);
    if (overloaded) {
      penalizeConnectionLimit(download, requestEpoch);
    }
    auto remainder = lease.remainder(writeOffset);
    if (download->snapshot_.totalLength > 0) {
      remainder.end = std::min(remainder.end, download->snapshot_.totalLength);
    }
    std::optional<std::chrono::milliseconds> retryDelay;
    if (!impl.fullDownload && !remainder.empty() &&
        (alternateMirror || expiredEndpoint ||
         retryableFailure(result, responseCode, impl.fileNotFoundCount,
                          maxFileNotFound, ranged && impl.rangeValidated,
                          appConnectTime > 0 || startTransferTime > 0 ||
                              writeOffset > lease.begin))) {
      retryDelay = retryRange(download, remainder,
                              overloaded ? std::max<curl_off_t>(1, retryAfter)
                                         : retryAfter);
    }
    if (retryDelay) {
      if (resolvingEndpoint && endpointGeneration == endpoint.generation) {
        endpoint.readyAt = std::chrono::steady_clock::now() + *retryDelay;
      }
      A2_LOG_DEBUG(fmt(
          "component=stream event=range_retry gid=%s transfer=%" PRId64
          " connection=%" PRId64 " range=%" PRId64 "-%" PRId64
          " attempt=%lu http=%ld curl=%d retry_in_ms=%" PRId64 " message=%s",
          gid(download.get()).c_str(), static_cast<int64_t>(transferId),
          static_cast<int64_t>(connectionId), remainder.begin, remainder.end,
          static_cast<unsigned long>(lease.attempts + 1), responseCode,
          static_cast<int>(result), static_cast<int64_t>(retryDelay->count()),
          nativeFailure.c_str()));
      schedule(download);
      return;
    }
    failTask(download, curlErrorCode(result, responseCode), nativeFailure);
    return;
  }

  if (ranged && rangeAccepted) {
    impl.planner.enqueue(lease.remainder(writeOffset));
    schedule(download);
    if (download->stopped() || !impl.planner.complete() ||
        !impl.handles.empty()) {
      return;
    }
  }
  else if (fullResponseAccepted || !impl.http) {
    auto length = download->snapshot_.totalLength;
    if (length <= 0) {
      length = std::max<int64_t>(impl.planner.completedLength(),
                                 lease.begin + reportedLength);
      download->snapshot_.totalLength = length;
    }
    impl.planner.configure(length, std::max<int64_t>(1, length), {});
    impl.plannerConfigured = true;
    if (length > 0 && !impl.planner.complete()) {
      failTask(download, error_code::NETWORK_PROBLEM,
               "The complete response ended before the declared file length");
      return;
    }
  }
  finalize(download, reportedFileTime);
}

void CurlSession::poll()
{
  if (!multi_) {
    return;
  }
  const auto revision = logging::revision();
  if (revision != loggingRevision_) {
    loggingRevision_ = revision;
    const auto verbose = A2_LOG_ENABLED(spdlog::level::trace) ? 1L : 0L;
    for (const auto& entry : downloads_) {
      const auto result =
          curl_easy_setopt(entry.first, CURLOPT_VERBOSE, verbose);
      if (result != CURLE_OK) {
        A2_LOG_WARN(fmt("component=stream event=trace_reconfigure_failed "
                        "gid=%s curl=%d message=%s",
                        gid(entry.second.first.get()).c_str(),
                        static_cast<int>(result), curl_easy_strerror(result)));
      }
    }
  }
  rebalanceLimits();
  for (const auto& entry : tasks_) {
    checkpoint(entry.second, false);
  }
  if (timeoutArmed_ && std::chrono::steady_clock::now() >= timeoutDeadline_) {
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
  const auto result =
      curl_multi_socket_action(multi_, socket, events, &running);
  if (result != CURLM_OK && result != CURLM_BAD_SOCKET) {
    A2_LOG_ERROR(fmt("libcurl multi socket action failed: %s",
                     curl_multi_strerror(result)));
  }
  processMessages();
  for (const auto& entry : tasks_) {
    refreshConnectionCount(entry.second);
  }
}

void CurlSession::refreshConnectionCount(
    const std::shared_ptr<CurlDownload>& download) const
{
  std::set<curl_off_t> connections;
  for (const auto& handle : download->impl_->handles) {
    if (!handle->value) {
      continue;
    }
    curl_off_t connectionId = -1;
    if (curl_easy_getinfo(handle->value, CURLINFO_CONN_ID, &connectionId) ==
            CURLE_OK &&
        connectionId >= 0) {
      connections.insert(connectionId);
    }
  }
  download->snapshot_.connections = static_cast<int>(connections.size());
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
    const auto result = message->data.result;
    const auto removeResult =
        curl_multi_remove_handle(multi_, message->easy_handle);
    if (removeResult != CURLM_OK) {
      A2_LOG_ERROR(fmt("component=stream event=handle_remove_failed gid=%s "
                       "curlm=%d message=%s",
                       gid(download.get()).c_str(),
                       static_cast<int>(removeResult),
                       curl_multi_strerror(removeResult)));
    }
    downloads_.erase(found);
    rebalanceLimits();
    finish(download, handle, result);
  }
}

void CurlSession::updateSocket(curl_socket_t socket, int action,
                               CurlSocketCommand* command)
{
  if (!engine_ || shuttingDown_) {
    return;
  }
  if (!command) {
    auto next = std::unique_ptr<CurlSocketCommand>(
        new CurlSocketCommand(engine_->newCUID(), socket, this, engine_));
    command = next.get();
    sockets_[socket] = command;
    const auto result = curl_multi_assign(multi_, socket, command);
    if (result != CURLM_OK) {
      throw DL_ABORT_EX(std::string("Unable to assign libcurl socket: ") +
                        curl_multi_strerror(result));
    }
    command->update(action);
    engine_->addCommand(std::move(next));
    return;
  }
  command->update(action);
}

void CurlSession::removeSocket(curl_socket_t socket, CurlSocketCommand* command)
{
  if (command) {
    command->remove();
  }
  sockets_.erase(socket);
  if (multi_) {
    const auto result = curl_multi_assign(multi_, socket, nullptr);
    if (result != CURLM_OK) {
      A2_LOG_WARN(fmt("component=stream event=socket_unassign_failed curlm=%d "
                      "message=%s",
                      static_cast<int>(result), curl_multi_strerror(result)));
    }
  }
}

void CurlSession::updateTimeout(long timeoutMs)
{
  if (timeoutMs < 0) {
    timeoutArmed_ = false;
    return;
  }
  timeoutArmed_ = true;
  timeoutDeadline_ =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  armTimeout();
}

int CurlSession::socketCallback(CURL*, curl_socket_t socket, int action,
                                void* userData, void* socketData) noexcept
{
  try {
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
  catch (const std::exception& error) {
    try {
      logging::tryWrite(
          spdlog::level::err, __FILE__, __LINE__,
          fmt("component=stream event=socket_callback_failed message=%s",
              logging::sanitizeText(error.what()).c_str()));
    }
    catch (...) {
    }
  }
  catch (...) {
    logging::tryWrite(spdlog::level::err, __FILE__, __LINE__,
                      "component=stream event=socket_callback_failed");
  }
  return -1;
}

int CurlSession::timerCallback(CURLM*, long timeoutMs, void* userData) noexcept
{
  try {
    static_cast<CurlSession*>(userData)->updateTimeout(timeoutMs);
    return 0;
  }
  catch (const std::exception& error) {
    try {
      logging::tryWrite(
          spdlog::level::err, __FILE__, __LINE__,
          fmt("component=stream event=timer_callback_failed message=%s",
              logging::sanitizeText(error.what()).c_str()));
    }
    catch (...) {
    }
  }
  catch (...) {
    logging::tryWrite(spdlog::level::err, __FILE__, __LINE__,
                      "component=stream event=timer_callback_failed");
  }
  return -1;
}

int CurlSession::socketOptionCallback(void* userData, curl_socket_t socket,
                                      curlsocktype) noexcept
{
  try {
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
  catch (...) {
    return CURL_SOCKOPT_ERROR;
  }
}

void CurlSession::stop(const std::shared_ptr<CurlDownload>& download,
                       bool retainState)
{
  auto& impl = *download->impl_;
  impl.stopRequested = true;
  try {
    for (auto& handle : impl.handles) {
      flushWriteBuffer(impl, *handle);
    }
    download->snapshot_.completedLength = impl.planner.completedLength();
  }
  catch (const Exception& error) {
    fail(download.get(), error.getErrorCode(), error.what());
  }
  catch (const std::exception& error) {
    fail(download.get(), error_code::FILE_IO_ERROR, error.what());
  }
  checkpoint(download, true);
  cancelHandles(download);
  rebalanceLimits();
  closeOutput(download.get());
  if (!download->failed()) {
    if (retainState) {
      download->snapshot_.state = CurlSnapshot::State::Paused;
    }
    else {
      if (impl.group) {
        store_.removePath(impl.path);
      }
      download->snapshot_.state = CurlSnapshot::State::Stopped;
    }
  }
  eraseTask(download.get());
}

bool CurlSession::refreshConnectionPoolLimits()
{
  if (!multi_ || !option_) {
    return false;
  }
  const auto maximum = std::numeric_limits<long>::max() / 2;
  const auto saturatingAdd = [maximum](long lhs, long rhs) {
    return lhs >= maximum - rhs ? maximum : lhs + rhs;
  };
  const auto maxTasks = static_cast<long>(
      std::max(1, option_->getAsInt(PREF_MAX_CONCURRENT_DOWNLOADS)));
  const auto defaultPerTask =
      static_cast<long>(effectiveStreamMaxConnections(option_));
  const auto baseline = maxTasks >= maximum / defaultPerTask
                            ? maximum
                            : maxTasks * defaultPerTask;
  long active = 0;
  for (const auto& entry : tasks_) {
    active = saturatingAdd(
        active,
        static_cast<long>(std::max(1, entry.second->impl_->maxConnections)));
  }
  const auto limit = std::max<long>(1, std::max(baseline, active));
  if (limit == connectionPoolLimit_) {
    return true;
  }
  const CURLMcode results[] = {
      curl_multi_setopt(multi_, CURLMOPT_MAX_TOTAL_CONNECTIONS, limit),
      curl_multi_setopt(multi_, CURLMOPT_MAX_HOST_CONNECTIONS, limit),
      curl_multi_setopt(multi_, CURLMOPT_MAXCONNECTS, limit * 2)};
  const auto failed =
      std::find_if(std::begin(results), std::end(results),
                   [](CURLMcode value) { return value != CURLM_OK; });
  if (failed != std::end(results)) {
    A2_LOG_ERROR(fmt("component=stream event=connection_pool_config_failed "
                     "limit=%ld curlm=%d message=%s",
                     limit, static_cast<int>(*failed),
                     curl_multi_strerror(*failed)));
    return false;
  }
  connectionPoolLimit_ = limit;
  A2_LOG_DEBUG(fmt("component=stream event=connection_pool_configured "
                   "limit=%ld active_tasks=%lu",
                   limit, static_cast<unsigned long>(tasks_.size())));
  return true;
}

void CurlSession::eraseTask(CurlDownload* download)
{
  tasks_.erase(download);
  if (multi_) {
    refreshConnectionPoolLimits();
  }
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
          ? std::max<int64_t>(1,
                              globalDownloadLimit_ /
                                  static_cast<int64_t>(taskHandles.size() +
                                                       externalDownloadCount_))
          : 0;
  for (const auto& entry : downloads_) {
    const auto task =
        entry.second.first->impl_->group->getMaxDownloadSpeedLimit();
    const auto taskLimit = task > 0 && taskShare > 0
                               ? std::min<int64_t>(task, taskShare)
                           : task > 0 ? static_cast<int64_t>(task)
                                      : taskShare;
    const auto count = taskHandles[entry.second.first.get()];
    const auto limit =
        taskLimit > 0
            ? std::max<int64_t>(1, taskLimit / static_cast<int64_t>(count))
            : 0;
    auto* handle = entry.second.second;
    if (handle->appliedLimit != limit) {
      const auto result =
          curl_easy_setopt(entry.first, CURLOPT_MAX_RECV_SPEED_LARGE,
                           static_cast<curl_off_t>(limit));
      if (result != CURLE_OK) {
        A2_LOG_ERROR(fmt("component=stream event=rate_limit_failed gid=%s "
                         "curl=%d message=%s",
                         gid(entry.second.first.get()).c_str(),
                         static_cast<int>(result), curl_easy_strerror(result)));
        continue;
      }
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

void CurlSession::setExternalDownloadCount(size_t count)
{
  if (externalDownloadCount_ == count)
    return;
  externalDownloadCount_ = count;
  rebalanceLimits();
}

} // namespace aria2
