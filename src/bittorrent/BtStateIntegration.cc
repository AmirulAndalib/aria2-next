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
#include "BtDownloadImpl.h"
#include "wallclock.h"
#include "ApplicationStatePath.h"
#include "BtDownload.h"
#include "BtSession.h"
#include "BtSettings.h"
#include "BtStateStore.h"
#include "BufferedFile.h"
#include "File.h"
#include "Log.h"
#include "Option.h"
#include "a2functional.h"
#include "fmt.h"
#include "prefs.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/span.hpp>
#include <libtorrent/write_resume_data.hpp>
#include <sstream>
#include <string>
#include <utility>
#include "bittorrent/BtSessionInternal.h"

namespace aria2 {
namespace bt_session {
namespace {
constexpr size_t MAX_SESSION_STATE_SIZE = 16_m;
}
bool writeAtomic(const std::string& path, const char* data, size_t size)
{
  if (path.empty()) {
    return false;
  }
  const auto temporary = path + "__temp";
  File directory(File(path).getDirname());
  if (!directory.isDir() && !directory.mkdirs()) {
    return false;
  }
  BufferedFile file(temporary.c_str(), BufferedFile::WRITE);
  return file && file.write(data, size) == size && file.close() != EOF &&
         File(temporary).renameTo(path);
}

std::string readStateFile(const std::string& path)
{
  if (path.empty()) {
    return {};
  }
  File state(path);
  if (!state.isFile()) {
    return {};
  }
  const auto size = state.size();
  if (size <= 0 || static_cast<uint64_t>(size) > MAX_SESSION_STATE_SIZE) {
    A2_LOG_WARN(
        fmt("Ignoring invalid BitTorrent session state file %s", path.c_str()));
    return {};
  }
  BufferedFile file(path.c_str(), BufferedFile::READ);
  if (!file) {
    return {};
  }
  std::stringstream data;
  file.transfer(data);
  return data.str();
}

bool hasDhtNodes(const lt::session_params& params)
{
  return !params.dht_state.nodes.empty() || !params.dht_state.nodes6.empty();
}

lt::session_params makeSessionParams(const Option* option,
                                     const BtConfig& config,
                                     std::string& loadedState)
{
  const auto stateFile = state::btSessionFile(option);
  loadedState = readStateFile(stateFile);
  if (loadedState.empty()) {
    return lt::session_params(config.settings);
  }

  try {
    auto params = lt::read_session_params(
        lt::span<char const>(loadedState.data(), loadedState.size()),
        lt::session::save_dht_state);
    params.settings = config.settings;
    if (!hasDhtNodes(params)) {
      loadedState.clear();
    }
    A2_LOG_TRACE(
        fmt("Loaded BitTorrent session state from %s", stateFile.c_str()));
    return params;
  }
  catch (const std::exception& error) {
    A2_LOG_WARN(fmt("Ignoring BitTorrent session state %s: %s",
                    stateFile.c_str(), error.what()));
    loadedState.clear();
    return lt::session_params(config.settings);
  }
}

void saveSessionState(BtSession::Impl* impl)
{
  if (!impl || !impl->session || impl->sessionStateFile.empty()) {
    return;
  }
  impl->lastSessionStateSave = global::wallclock();
  if (!impl->config.dhtEnabled) {
    return;
  }
  try {
    const auto params =
        impl->session->session_state(lt::session::save_dht_state);
    if (!hasDhtNodes(params)) {
      return;
    }
    const auto buffer =
        lt::write_session_params_buf(params, lt::session::save_dht_state);
    std::string state(buffer.begin(), buffer.end());
    if (state == impl->lastSessionState) {
      return;
    }
    if (!writeAtomic(impl->sessionStateFile, state.data(), state.size())) {
      A2_LOG_ERROR(fmt("Failed to save BitTorrent session state to %s",
                       impl->sessionStateFile.c_str()));
      return;
    }
    impl->lastSessionState = std::move(state);
    A2_LOG_TRACE(fmt("Saved BitTorrent session state to %s",
                     impl->sessionStateFile.c_str()));
  }
  catch (const std::exception& error) {
    A2_LOG_ERROR(
        fmt("Failed to serialize BitTorrent session state: %s", error.what()));
  }
}

void saveResume(const std::string& path, const lt::add_torrent_params& params)
{
  const auto data = lt::write_resume_data_buf(params);
  BtStateStore::writeResume(path, data.data(), data.size());
}
} // namespace bt_session
using namespace bt_session;

void BtSession::requestResumeCheckpoint(BtDownload* download, bool force)
{
  if (!download || !download->impl_->handle.is_valid() ||
      !download->impl_->handle.in_session()) {
    return;
  }
  if (download->impl_->resumeSaveOutstanding) {
    download->impl_->checkpointPending = true;
    return;
  }
  if (!force) {
    const auto interval = impl_->option->getAsInt(PREF_BT_RESUME_SAVE_INTERVAL);
    if (interval == 0) {
      return;
    }
    if (!download->impl_->lastResumeSave.isZero() &&
        download->impl_->lastResumeSave.difference(global::wallclock()) <
            std::chrono::minutes(interval)) {
      return;
    }
  }
  download->impl_->resumeSaveOutstanding = true;
  download->impl_->handle.save_resume_data(
      lt::torrent_handle::save_info_dict |
      lt::torrent_handle::only_if_modified);
}

void BtSession::finishResumeSave(BtDownload* download)
{
  download->impl_->resumeSaveOutstanding = false;
  download->impl_->lastResumeSave = global::wallclock();
  if (download->impl_->checkpointPending &&
      download->shutdownStage() == BtDownload::ShutdownStage::Idle) {
    download->impl_->checkpointPending = false;
    requestResumeCheckpoint(download, true);
  }
}

} // namespace aria2
