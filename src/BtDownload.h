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
#include "RecoverableException.h"

namespace aria2 {

class DownloadContext;
class Option;
class RequestGroup;
class BtSession;

class BtMetainfoError : public RecoverableException {
private:
  std::string kind_;
  std::string category_;
  int nativeCode_;

protected:
  std::shared_ptr<Exception> copy() const override;

public:
  BtMetainfoError(const char* file, int line, std::string message,
                  std::string kind, std::string category, int nativeCode);

  const std::string& kind() const { return kind_; }
  const std::string& category() const { return category_; }
  int nativeCode() const { return nativeCode_; }
};

struct BtMetainfoFile {
  size_t index = 0;
  std::string path;
  int64_t length = 0;
};

struct BtMetainfo {
  enum class Mode { Single, Multi };

  std::string name;
  Mode mode = Mode::Single;
  std::string infoHashV1;
  std::string infoHashV2;
  std::vector<BtMetainfoFile> files;
  int64_t totalLength = 0;
};

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
  enum class ProgressState { Stable, Verifying, Selecting, Refreshing };

  std::unique_ptr<Impl> impl_;
  BtSnapshot snapshot_;
  Source source_;
  StopReason stopReason_ = StopReason::None;
  ShutdownStage shutdownStage_ = ShutdownStage::Idle;
  ProgressState progressState_ = ProgressState::Stable;
  bool completionNotified_ = false;
  RequestGroup* group_ = nullptr;

  std::string fileSelectionError(const Option* option) const;
  void restoreResumeProgress();
  void refreshLogicalProgress();
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

  static size_t maxMetainfoSize();

  BtMetainfo metainfo() const;

  void configure(const Option* option);
  void populateDownloadContext(const std::shared_ptr<DownloadContext>& context,
                               const Option* option);
  void updateFilePaths(const std::shared_ptr<DownloadContext>& context,
                       const Option* option) const;
  void updateSelection(const std::shared_ptr<DownloadContext>& context);
  void initialize(RequestGroup* group);
  std::string trackerSource(const std::string& url) const;

  const BtSnapshot& snapshot() const { return snapshot_; }
  BtSnapshot& mutableSnapshot() { return snapshot_; }

  bool hasMetadata() const;
  bool active() const;
  bool stopped() const;
  bool failed() const;
  bool stopRequested() const { return shutdownStage_ != ShutdownStage::Idle; }
  StopReason stopReason() const { return stopReason_; }
  ShutdownStage shutdownStage() const { return shutdownStage_; }
  bool awaitingFileSelection() const;
  bool fileSelectionReady() const;
  bool fileSelectionApplying() const;
  bool shouldPauseAfterMetadata() const;
  void validateFileSelection(const Option* option) const;

  void setGroup(RequestGroup* group) { group_ = group; }
  RequestGroup* group() const { return group_; }
  void requestStop(StopReason reason);
  void beginSavingResume();
  void beginRemoving();
  void finishStopping();
  void beginFileSelectionPause();
  void submitFileSelection(const Option* option);
  void beginFileSelectionApply();
  void completeFileSelectionApply();
  void failFileSelectionApply();
  void applyTransportState(BtSnapshot::State state);
  void beginProgressVerification();
  void beginSelectionProgressHold();
  void beginProgressRefresh();
  void applyFileProgress(const std::vector<int64_t>& completedLengths);
  void setError(std::string message);
  void setError(BtErrorSnapshot error);
  void clearError();
  bool takeCompletionNotification();
  void prepareStart();

  friend class BtSession;
};

} // namespace aria2

#endif // D_BT_DOWNLOAD_H
