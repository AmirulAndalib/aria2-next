/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2026 The aria2-next contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
/* copyright --> */
#include "BtStateStore.h"
#include "Exception.h"
#include "RecoverableException.h"
#include "a2functional.h"
#include "error_code.h"
#include <cstddef>
#include <exception>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/span.hpp>
#include <libtorrent/units.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "BtDownload.h"
#include "BtDownloadImpl.h"
#include "bittorrent/BtDownloadSupport.h"
#include "DlAbortEx.h"
#include "BtMetadata.h"
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/write_resume_data.hpp>
#include <libtorrent/torrent_info.hpp>

namespace aria2 {

namespace lt = libtorrent;
using namespace bt_download;

namespace {
std::unique_ptr<BtDownload::Impl>
makeImpl(lt::add_torrent_params params,
         const std::vector<std::string>& webSeeds)
{
  params.url_seeds.insert(params.url_seeds.end(), webSeeds.begin(),
                          webSeeds.end());
  auto impl = make_unique<BtDownload::Impl>();
  impl->params = std::move(params);
  const auto origin =
      impl->params.ti ? BtTrackerOrigin::Metainfo : BtTrackerOrigin::Magnet;
  impl->sourceTrackers = trackerSpecs(impl->params, origin);
  impl->effectiveTrackers = impl->sourceTrackers;
  return impl;
}

} // namespace

std::shared_ptr<Exception> BtMetainfoError::copy() const
{
  return std::make_shared<BtMetainfoError>(*this);
}

BtMetainfoError::BtMetainfoError(const char* file, int line,
                                 std::string message, std::string kind,
                                 std::string category, int nativeCode)
    : RecoverableException(file, line, std::move(message),
                           error_code::BITTORRENT_PARSE_ERROR),
      kind_(std::move(kind)),
      category_(std::move(category)),
      nativeCode_(nativeCode)
{
}

BtDownload::BtDownload(std::unique_ptr<Impl> impl, Source source)
    : impl_(std::move(impl)), source_(source)
{
  auto attrs = BtMetadata();
  assignHashes(&attrs, snapshot_, impl_->params.info_hashes);
  snapshot_.name = impl_->params.name;
  snapshot_.announceList = announceList(impl_->params);
  snapshot_.webSeeds = impl_->params.url_seeds;
  snapshot_.magnetLink = lt::make_magnet_uri(impl_->params);
  snapshot_.hasMetadata = static_cast<bool>(impl_->params.ti);
  snapshot_.state = snapshot_.hasMetadata
                        ? BtSnapshot::State::Adding
                        : BtSnapshot::State::DownloadingMetadata;
}

std::shared_ptr<BtDownload>
BtDownload::fromFile(const std::string& path,
                     const std::vector<std::string>& webSeeds)
{
  lt::error_code error;
  auto params = lt::load_torrent_file(path, error, {});
  if (error) {
    throw DL_ABORT_EX("Unable to load torrent file: " + error.message());
  }
  auto download = std::shared_ptr<BtDownload>(
      new BtDownload(makeImpl(std::move(params), webSeeds), Source::Metainfo));
  download->impl_->metadataSourcePath = path;
  return download;
}

std::shared_ptr<BtDownload>
BtDownload::fromBuffer(const std::string& data,
                       const std::vector<std::string>& webSeeds)
{
  const lt::load_torrent_limits limits;
  if (data.size() > static_cast<size_t>(limits.max_buffer_size)) {
    const auto error =
        lt::errors::make_error_code(lt::errors::metadata_too_large);
    throw BtMetainfoError(__FILE__, __LINE__, error.message(),
                          "torrentTooLarge", error.category().name(),
                          error.value());
  }
  lt::error_code error;
  auto params = lt::load_torrent_buffer(
      {data.data(), static_cast<lt::span<char const>::index_type>(data.size())},
      error, limits);
  if (error) {
    throw BtMetainfoError(__FILE__, __LINE__, error.message(), "invalidTorrent",
                          error.category().name(), error.value());
  }
  return std::shared_ptr<BtDownload>(
      new BtDownload(makeImpl(std::move(params), webSeeds), Source::Metainfo));
}

size_t BtDownload::maxMetainfoSize()
{
  return static_cast<size_t>(lt::load_torrent_limits{}.max_buffer_size);
}

std::shared_ptr<BtDownload> BtDownload::fromMagnet(const std::string& uri)
{
  lt::error_code error;
  auto params = lt::parse_magnet_uri(uri, error);
  if (error) {
    throw DL_ABORT_EX("Unable to parse magnet URI: " + error.message());
  }
  return std::shared_ptr<BtDownload>(
      new BtDownload(makeImpl(std::move(params), {}), Source::Magnet));
}

BtMetainfo BtDownload::metainfo() const
{
  if (!impl_->params.ti) {
    throw BtMetainfoError(__FILE__, __LINE__, "Torrent metadata is missing.",
                          "invalidTorrent", "aria2", 0);
  }

  const auto& info = *impl_->params.ti;
  const auto& layout = info.layout();
  BtMetainfo result;
  result.name = info.name();
  result.mode = layout.num_files() > 1 ? BtMetainfo::Mode::Multi
                                       : BtMetainfo::Mode::Single;
  result.infoHashV1 = snapshot_.infoHashV1;
  result.infoHashV2 = snapshot_.infoHashV2;
  result.totalLength = info.total_size();
  result.files.reserve(static_cast<size_t>(layout.num_files()));
  size_t rpcIndex = 1;
  for (lt::file_index_t index{0}; index < layout.end_file(); ++index) {
    result.files.push_back(
        {rpcIndex++, layout.file_path(index), layout.file_size(index)});
  }
  return result;
}

std::string BtDownload::torrentFileData() const
{
  try {
    const auto data = lt::write_torrent_file_buf(impl_->params, {});
    return std::string(data.data(), data.size());
  }
  catch (const std::exception& error) {
    throw DL_ABORT_EX("Unable to serialize torrent metadata: " +
                      std::string(error.what()));
  }
}

void BtDownload::setManagedMetadataPath(std::string path)
{
  impl_->metadataSourcePath = path;
  impl_->managedMetadataPath = std::move(path);
}

BtStateReference BtDownload::stateReference() const
{
  return {impl_->managedMetadataPath, impl_->resumePath};
}

BtDownload::~BtDownload() = default;

} // namespace aria2
