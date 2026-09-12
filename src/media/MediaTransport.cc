/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "MediaTransport.h"
#include "media/MediaDownload.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <curl/curl.h>
#include <curl/easy.h>
#include <curl/multi.h>
#include <curl/system.h>
#include <ios>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include "MediaFiles.h"

#include "Option.h"
#include "prefs.h"
#include <openssl/evp.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace aria2 {
namespace media {
namespace {
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
      control_(std::move(control)),
      contexts_(parseRequestContexts(option->get(PREF_MEDIA_REQUEST_CONTEXTS)))
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
    try {
      auto resource = request(url, begin, end, temporary);
      const auto digest = fileDigest(temporary);
      const auto committed = path + "-" + digest;
      if (std::filesystem::exists(nativePath(committed)) &&
          fileDigest(committed) == digest)
        std::filesystem::remove(nativePath(temporary));
      else {
        std::filesystem::remove(nativePath(committed));
        std::filesystem::rename(nativePath(temporary), nativePath(committed));
      }
      resource.path = committed;
      return resource;
    }
    catch (const HttpError& error) {
      if (!error.retryable || (tries > 0 && attempt + 1 >= tries) ||
          control_->cancel)
        throw;
      const auto delay = std::max<curl_off_t>(
          std::max(1, option_->getAsInt(PREF_RETRY_WAIT)), error.retryAfter);
      std::unique_lock<std::mutex> lock(control_->mutex);
      control_->wake.wait_for(lock, std::chrono::seconds(delay),
                              [&] { return control_->cancel.load(); });
    }
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
