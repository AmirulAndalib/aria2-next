/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "MediaTransport.h"
#include "MediaFiles.h"

#include "Option.h"
#include "CurlSession.h"
#include "BufferedFile.h"
#include "Log.h"
#include "fmt.h"
#include "prefs.h"
#include <openssl/evp.h>
#include <curl/header.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <sstream>
#include <thread>

namespace aria2 {
namespace media {
namespace {
struct Body {
  std::unique_ptr<BufferedFile> output;
  Control* control;
  size_t bytes = 0;
};
size_t writeBody(char* data, size_t size, size_t count, void* opaque) noexcept
{
  auto& body = *static_cast<Body*>(opaque);
  if (body.control->cancel)
    return CURL_WRITEFUNC_ERROR;
  const auto length = size * count;
  if (body.output->write(data, length) != length)
    return CURL_WRITEFUNC_ERROR;
  body.control->received.fetch_add(length);
  body.bytes += length;
  return length;
}
int progress(void* opaque, curl_off_t, curl_off_t, curl_off_t,
             curl_off_t) noexcept
{
  const auto& control = *static_cast<Control*>(opaque);
  return control.cancel;
}
bool headerIs(const std::string& line, const char* name)
{
  return line.size() >= std::char_traits<char>::length(name) &&
         curl_strnequal(line.c_str(), name,
                        std::char_traits<char>::length(name));
}
std::string fileDigest(const std::string& path)
{
  std::ifstream input(nativePath(path), std::ios::binary);
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(),
                                                              EVP_MD_CTX_free);
  if (!input || !ctx || !EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr))
    throw std::runtime_error("Cannot verify media cache content");
  std::array<char, 65536> buffer{};
  while (input) {
    input.read(buffer.data(), buffer.size());
    if (!EVP_DigestUpdate(ctx.get(), buffer.data(),
                          static_cast<size_t>(input.gcount())))
      throw std::runtime_error("Cannot hash media cache content");
  }
  if (!input.eof())
    throw std::runtime_error("Cannot read media cache content");
  unsigned char digest[32];
  unsigned int size = 0;
  if (!EVP_DigestFinal_ex(ctx.get(), digest, &size))
    throw std::runtime_error("Cannot finish media cache verification");
  std::string hex;
  const char* digits = "0123456789abcdef";
  for (unsigned int i = 0; i < size; ++i) {
    hex += digits[digest[i] >> 4];
    hex += digits[digest[i] & 15];
  }
  return hex;
}
} // namespace

std::string Transport::digest(const std::string& path)
{
  return fileDigest(path);
}

void Transport::retain(const std::string& path)
{
  if (path.empty())
    return;
  auto name = nativePath(path).filename().u8string();
  if (name.size() < 129 || name[64] != '-')
    return;
  auto raw = (nativePath(path).parent_path() / name.substr(0, 129)).u8string();
  retained_[name.substr(0, 64)] = raw;
}

std::string Transport::fingerprint(const std::string& value)
{
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int size = 0;
  if (!EVP_Digest(value.data(), value.size(), digest, &size, EVP_sha256(),
                  nullptr))
    throw std::runtime_error("Cannot hash media resource identity");
  const char* digits = "0123456789abcdef";
  std::string result;
  for (unsigned i = 0; i < size; ++i) {
    result += digits[digest[i] >> 4];
    result += digits[digest[i] & 15];
  }
  return result;
}
Transport::Transport(const Option* option, std::string source,
                     std::string directory, std::shared_ptr<Control> control)
    : option_(option),
      source_(std::move(source)),
      directory_(std::move(directory)),
      control_(std::move(control))
{
  std::filesystem::create_directories(nativePath(directory_));
  multi_ = curl_multi_init();
  share_ = curl_share_init();
  if (!multi_ || !share_) {
    if (multi_)
      curl_multi_cleanup(multi_);
    if (share_)
      curl_share_cleanup(share_);
    throw std::runtime_error("Cannot initialize media HTTP transport");
  }
  curl_share_setopt(share_, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
  curl_share_setopt(share_, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
  curl_share_setopt(share_, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
}
Transport::~Transport()
{
  curl_multi_cleanup(multi_);
  curl_share_cleanup(share_);
}

Resource Transport::get(const std::string& url, int64_t begin, int64_t end,
                        bool cached)
{
  if (!(url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0))
    throw std::runtime_error(
        "Media manifests may reference HTTP(S) resources only");
  if (begin < 0 || (end >= 0 && end < begin))
    throw std::runtime_error("Invalid media byte range");
  const auto key = fingerprint(url + "\n" + std::to_string(begin) + "\n" +
                               std::to_string(end));
  auto path = (nativePath(directory_) / key).u8string();
  auto found = retained_.find(key);
  if (cached && found != retained_.end() &&
      std::filesystem::is_regular_file(nativePath(found->second))) {
    auto name = nativePath(found->second).filename().u8string();
    if (fileDigest(found->second) == name.substr(65, 64))
      return {found->second, url, "",
              static_cast<int64_t>(
                  std::filesystem::file_size(nativePath(found->second)))};
  }
  auto temporary = path + ".partial";
  const int tries = option_->getAsInt(PREF_MAX_TRIES);
  for (int attempt = 0;; ++attempt) {
    if (control_->cancel)
      throw std::runtime_error("Media transfer interrupted");
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> easy(curl_easy_init(),
                                                             curl_easy_cleanup);
    if (!easy)
      throw std::runtime_error("Cannot create media HTTP request");
    auto h = easy.get();
    Body body{std::make_unique<BufferedFile>(temporary.c_str(), "wb"),
              control_.get()};
    if (!*body.output)
      throw std::runtime_error("Cannot open media cache file");
    auto set = [h](CURLoption key, auto value) {
      auto rc = curl_easy_setopt(h, key, value);
      if (rc != CURLE_OK)
        throw std::runtime_error(curl_easy_strerror(rc));
    };
    set(CURLOPT_URL, url.c_str());
    set(CURLOPT_SHARE, share_);
    set(CURLOPT_PROTOCOLS_STR, "http,https");
    set(CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    set(CURLOPT_FOLLOWLOCATION, 1L);
    set(CURLOPT_MAXREDIRS, 10L);
    set(CURLOPT_FAILONERROR, 1L);
    set(CURLOPT_NOSIGNAL, 1L);
    set(CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    set(CURLOPT_TCP_KEEPALIVE, 1L);
    set(CURLOPT_COOKIEFILE, "");
    set(CURLOPT_CONNECTTIMEOUT,
        static_cast<long>(option_->getAsInt(PREF_CONNECT_TIMEOUT)));
    set(CURLOPT_LOW_SPEED_TIME,
        static_cast<long>(option_->getAsInt(PREF_TIMEOUT)));
    set(CURLOPT_LOW_SPEED_LIMIT, 1L);
    set(CURLOPT_USERAGENT, option_->get(PREF_USER_AGENT).c_str());
    const auto tlsResult = CurlSession::configureTls(h, option_);
    if (tlsResult != CURLE_OK)
      throw std::runtime_error(curl_easy_strerror(tlsResult));
    if (!option_->blank(PREF_INTERFACE))
      set(CURLOPT_INTERFACE, option_->get(PREF_INTERFACE).c_str());
    if (option_->getAsBool(PREF_DISABLE_IPV6))
      set(CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    const auto proxy =
        url.rfind("https:", 0) == 0 ? PREF_HTTPS_PROXY : PREF_HTTP_PROXY;
    const auto& proxyUrl = option_->blank(proxy) ? option_->get(PREF_ALL_PROXY)
                                                 : option_->get(proxy);
    if (!proxyUrl.empty())
      set(CURLOPT_PROXY, proxyUrl.c_str());
    const auto user = option_->blank(proxy)       ? PREF_ALL_PROXY_USER
                      : proxy == PREF_HTTPS_PROXY ? PREF_HTTPS_PROXY_USER
                                                  : PREF_HTTP_PROXY_USER;
    const auto password = option_->blank(proxy)       ? PREF_ALL_PROXY_PASSWD
                          : proxy == PREF_HTTPS_PROXY ? PREF_HTTPS_PROXY_PASSWD
                                                      : PREF_HTTP_PROXY_PASSWD;
    if (!option_->blank(user)) {
      set(CURLOPT_PROXYUSERNAME, option_->get(user).c_str());
      set(CURLOPT_PROXYPASSWORD, option_->get(password).c_str());
    }
    if (!option_->blank(PREF_NO_PROXY))
      set(CURLOPT_NOPROXY, option_->get(PREF_NO_PROXY).c_str());
    if (!option_->blank(PREF_REFERER))
      set(CURLOPT_REFERER, option_->get(PREF_REFERER).c_str());
    if (!option_->blank(PREF_LOAD_COOKIES))
      set(CURLOPT_COOKIEFILE, option_->get(PREF_LOAD_COOKIES).c_str());
    auto limit = control_->downloadLimit.load();
    set(CURLOPT_MAX_RECV_SPEED_LARGE, static_cast<curl_off_t>(limit));
    curl_slist* rawHeaders = nullptr;
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(
        nullptr, curl_slist_free_all);
    const bool credentials = CurlSession::sameOrigin(source_, url);
    std::istringstream configuredHeaders(option_->get(PREF_HEADER));
    std::string header;
    while (std::getline(configuredHeaders, header)) {
      if (!credentials &&
          (headerIs(header, "Cookie:") || headerIs(header, "Authorization:")))
        continue;
      if (headerIs(header, "Range:") || headerIs(header, "Host:"))
        continue;
      auto next = curl_slist_append(rawHeaders, header.c_str());
      if (!next) {
        curl_slist_free_all(rawHeaders);
        throw std::bad_alloc();
      }
      rawHeaders = next;
    }
    headers.reset(rawHeaders);
    if (rawHeaders)
      set(CURLOPT_HTTPHEADER, rawHeaders);
    if (credentials && !option_->blank(PREF_HTTP_USER)) {
      set(CURLOPT_HTTPAUTH, CURLAUTH_ANY);
      set(CURLOPT_USERNAME, option_->get(PREF_HTTP_USER).c_str());
      set(CURLOPT_PASSWORD, option_->get(PREF_HTTP_PASSWD).c_str());
    }
    std::string range;
    if (begin || end >= 0) {
      range =
          std::to_string(begin) + "-" + (end >= 0 ? std::to_string(end) : "");
      set(CURLOPT_RANGE, range.c_str());
    }
    set(CURLOPT_WRITEFUNCTION, writeBody);
    set(CURLOPT_WRITEDATA, &body);
    set(CURLOPT_XFERINFOFUNCTION, progress);
    set(CURLOPT_XFERINFODATA, control_.get());
    set(CURLOPT_NOPROGRESS, 0L);
    if (curl_multi_add_handle(multi_, h) != CURLM_OK)
      throw std::runtime_error("Cannot schedule media transfer");
    control_->connections = 1;
    int running = 0;
    CURLMcode mc;
    do {
      auto currentLimit = control_->downloadLimit.load();
      if (currentLimit != limit) {
        limit = currentLimit;
        curl_easy_setopt(h, CURLOPT_MAX_RECV_SPEED_LARGE,
                         static_cast<curl_off_t>(limit));
      }
      mc = curl_multi_perform(multi_, &running);
      if (mc != CURLM_OK || !running || control_->cancel)
        break;
      mc = curl_multi_poll(multi_, nullptr, 0, 100, nullptr);
    } while (mc == CURLM_OK);
    CURLcode result = CURLE_ABORTED_BY_CALLBACK;
    int remaining = 0;
    while (auto message = curl_multi_info_read(multi_, &remaining))
      if (message->msg == CURLMSG_DONE && message->easy_handle == h)
        result = message->data.result;
    curl_multi_remove_handle(multi_, h);
    control_->connections = 0;
    if (body.output->close() != 0)
      throw std::runtime_error("Cannot close media cache file");
    long response = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &response);
    char* effective = nullptr;
    char* mime = nullptr;
    curl_easy_getinfo(h, CURLINFO_EFFECTIVE_URL, &effective);
    curl_easy_getinfo(h, CURLINFO_CONTENT_TYPE, &mime);
    A2_LOG_DEBUG(fmt(
        "component=media event=http_complete url=%s status=%ld "
        "curl=%d bytes=%llu range=%s",
        logging::sanitizeUri(url).c_str(), response, static_cast<int>(result),
        static_cast<unsigned long long>(body.bytes), range.c_str()));
    if (result == CURLE_OK && mc == CURLM_OK && response >= 200 &&
        response < 300) {
      if (!range.empty() && response != 206)
        throw std::runtime_error("Server ignored a required media byte range");
      if (response == 206 &&
          !CurlSession::matchesRange(h, begin, end, body.bytes))
        throw std::runtime_error(
            "Server returned an incorrect media byte range");
      if (end >= 0 && body.bytes != static_cast<uint64_t>(end - begin + 1))
        throw std::runtime_error("Incomplete media byte range");
      const auto digest = fileDigest(temporary);
      auto committed = path + "-" + digest;
      if (std::filesystem::exists(nativePath(committed)) &&
          fileDigest(committed) == digest)
        std::filesystem::remove(nativePath(temporary));
      else {
        // A damaged cache entry must not win over newly verified content.
        std::filesystem::remove(nativePath(committed));
        std::filesystem::rename(nativePath(temporary), nativePath(committed));
      }
      Resource resource{committed, effective ? effective : url,
                        mime ? mime : "", static_cast<int64_t>(body.bytes)};
      curl_off_t elapsed = 0, firstByte = 0;
      curl_easy_getinfo(h, CURLINFO_TOTAL_TIME_T, &elapsed);
      curl_easy_getinfo(h, CURLINFO_STARTTRANSFER_TIME_T, &firstByte);
      resource.utcStart =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::system_clock::now().time_since_epoch())
              .count() -
          (elapsed - firstByte) / 1000;
      curl_header* header = nullptr;
      while ((header = curl_easy_nextheader(h, CURLH_HEADER, -1, header)))
        resource.headers[header->name] = header->value;
      return resource;
    }
    const bool retry = response == 429 || response == 503 || response >= 500 ||
                       result == CURLE_COULDNT_CONNECT ||
                       result == CURLE_COULDNT_RESOLVE_HOST ||
                       result == CURLE_OPERATION_TIMEDOUT ||
                       result == CURLE_RECV_ERROR ||
                       result == CURLE_PARTIAL_FILE;
    if (!retry || (tries > 0 && attempt + 1 >= tries) || control_->cancel)
      throw std::runtime_error(
          "Media HTTP request failed: status=" + std::to_string(response) +
          " curl=" + std::to_string(result));
    curl_off_t retryAfter = 0;
    curl_easy_getinfo(h, CURLINFO_RETRY_AFTER, &retryAfter);
    const auto delay = std::max<curl_off_t>(
        std::max(1, option_->getAsInt(PREF_RETRY_WAIT)), retryAfter);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(delay);
    while (std::chrono::steady_clock::now() < deadline && !control_->cancel)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}
std::string Transport::decrypt(const std::string& path,
                               const std::string& keyUrl,
                               const unsigned char* iv, bool cacheKey)
{
  auto key = get(keyUrl, 0, -1, cacheKey);
  if (key.size != 16)
    throw std::runtime_error("HLS AES-128 key must contain 16 bytes");
  std::array<unsigned char, 16> bytes{};
  std::ifstream keyFile(nativePath(key.path), std::ios::binary);
  keyFile.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
  if (keyFile.gcount() != 16)
    throw std::runtime_error("Cannot read HLS AES-128 key");
  keyFile.close();
  if (cacheKey)
    retain(key.path);
  else
    std::filesystem::remove(nativePath(key.path));
  std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(
      EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
  if (!ctx || !EVP_DecryptInit_ex(ctx.get(), EVP_aes_128_cbc(), nullptr,
                                  bytes.data(), iv))
    throw std::runtime_error("Cannot initialize HLS AES-128 decryption");
  std::ifstream input(nativePath(path), std::ios::binary);
  auto clear = path + ".clear";
  std::ofstream output(nativePath(clear + ".partial"),
                       std::ios::binary | std::ios::trunc);
  std::array<unsigned char, 65536> in{};
  std::array<unsigned char, 65552> out{};
  while (input && !control_->cancel) {
    input.read(reinterpret_cast<char*>(in.data()), in.size());
    int size = 0;
    if (!EVP_DecryptUpdate(ctx.get(), out.data(), &size, in.data(),
                           static_cast<int>(input.gcount())))
      throw std::runtime_error("HLS AES-128 decryption failed");
    output.write(reinterpret_cast<char*>(out.data()), size);
  }
  int size = 0;
  if (control_->cancel || !input.eof())
    throw std::runtime_error("HLS decryption interrupted or unreadable input");
  if (!EVP_DecryptFinal_ex(ctx.get(), out.data(), &size))
    throw std::runtime_error("Invalid HLS AES-128 padding");
  output.write(reinterpret_cast<char*>(out.data()), size);
  output.close();
  if (!output)
    throw std::runtime_error("Cannot write decrypted media fragment");
  std::filesystem::rename(nativePath(clear + ".partial"), nativePath(clear));
  return clear;
}
} // namespace media
} // namespace aria2
