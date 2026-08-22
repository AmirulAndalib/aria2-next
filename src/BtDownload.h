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
#ifndef D_BT_DOWNLOAD_H
#define D_BT_DOWNLOAD_H

#include "common.h"

#include <memory>
#include <string>
#include <vector>

#include "BtSnapshot.h"

namespace aria2 {

class DownloadContext;
class Option;
class RequestGroup;
class BtSession;

class BtDownload {
public:
  struct Impl;

  enum class Source { Metainfo, Magnet };
  enum class StopReason { None, Pause, FileSelection, Stop };
  enum class ShutdownStage {
    Idle,
    PendingHandle,
    SavingResume,
    Removing,
    Complete
  };

private:
  std::unique_ptr<Impl> impl_;
  BtSnapshot snapshot_;
  Source source_;
  StopReason stopReason_ = StopReason::None;
  ShutdownStage shutdownStage_ = ShutdownStage::Idle;
  bool recoverableError_ = false;
  RequestGroup* group_ = nullptr;

  BtDownload(std::unique_ptr<Impl> impl, Source source);

public:
  ~BtDownload();

  BtDownload(const BtDownload&) = delete;
  BtDownload& operator=(const BtDownload&) = delete;

  static std::shared_ptr<BtDownload>
  fromFile(const std::string& path, const std::vector<std::string>& webSeeds);

  static std::shared_ptr<BtDownload>
  fromBuffer(const std::string& data, const std::vector<std::string>& webSeeds);

  static std::shared_ptr<BtDownload> fromMagnet(const std::string& uri);

  void configure(const Option* option);
  void populateDownloadContext(const std::shared_ptr<DownloadContext>& context,
                               const Option* option);
  void updateFilePaths(const std::shared_ptr<DownloadContext>& context,
                       const Option* option) const;
  void updateSelection(const std::shared_ptr<DownloadContext>& context);
  void initialize(RequestGroup* group);

  const BtSnapshot& snapshot() const { return snapshot_; }
  BtSnapshot& mutableSnapshot() { return snapshot_; }

  bool hasMetadata() const;
  bool active() const;
  bool stopped() const;
  bool failed() const;
  bool recoverableError() const { return recoverableError_; }
  bool stopRequested() const { return shutdownStage_ != ShutdownStage::Idle; }
  StopReason stopReason() const { return stopReason_; }
  ShutdownStage shutdownStage() const { return shutdownStage_; }
  bool awaitingFileSelection() const;
  bool shouldPauseAfterMetadata() const;

  void setGroup(RequestGroup* group) { group_ = group; }
  RequestGroup* group() const { return group_; }
  void requestStop(StopReason reason);
  void beginSavingResume();
  void beginRemoving();
  void finishStopping();
  void consumeMetadataPause();
  void prepareStart();

  friend class BtSession;
};

} // namespace aria2

#endif // D_BT_DOWNLOAD_H
