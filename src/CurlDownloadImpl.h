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
#ifndef D_CURL_DOWNLOAD_IMPL_H
#define D_CURL_DOWNLOAD_IMPL_H

#include "common.h"

#include <array>

#include <chrono>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <curl/curl.h>

#include "DiskWriter.h"
#include "RangePlanner.h"
#include "SpeedCalc.h"
#include "TimerA2.h"

namespace aria2 {

class RequestGroup;
class CurlDownload;

enum class CurlHandlePurpose { Payload, RangeProbe, HeadProbe };

enum class CurlStartMode { Transfer, InspectExisting };

enum class CurlResponseFailure {
  None,
  EtagChanged,
  ValidatorUnavailable,
  ModifiedChanged,
  LengthChanged,
  InvalidRange,
  PreconditionFailed
};

struct CurlEndpoint {
  std::string uri;
  uint64_t generation = 0;
  bool resolving = false;
  std::chrono::steady_clock::time_point readyAt{};
  bool unavailable = false;
};

struct CurlHandle {
  CurlDownload* download = nullptr;
  CURL* value = nullptr;
  curl_slist* headers = nullptr;
  RangeLease lease;
  int64_t writeOffset = 0;
  int64_t appliedLimit = -1;
  int64_t bufferOffset = 0;
  size_t bufferLimit = 0;
  SpeedCalc payloadSpeed;
  Timer bodySampleStart = Timer::zero();
  Timer lastPayload = Timer::zero();
  uint64_t connectionEpoch = 0;
  uint64_t endpointGeneration = 0;
  bool resolvingEndpoint = false;
  bool redirectedEndpoint = false;
  long addressFamily = CURL_IPRESOLVE_WHATEVER;
  int64_t responseRangeEnd = -1;
  int64_t responseTotalLength = -1;
  int64_t responseContentLength = -1;
  int64_t unsatisfiedTotalLength = -1;
  long responseCode = 0;
  bool ranged = false;
  bool rangeAccepted = false;
  bool fullResponseAccepted = false;
  bool headersComplete = false;
  bool primary = false;
  CurlResponseFailure responseFailure = CurlResponseFailure::None;
  CurlHandlePurpose purpose = CurlHandlePurpose::Payload;
  std::string responseEtag;
  std::string responseLastModified;
  std::string responseDate;
  std::string rangeValidator;
  std::string range;
  std::vector<unsigned char> writeBuffer;
  std::array<char, CURL_ERROR_SIZE> errorBuffer{};
};

struct CurlDownloadImpl {
  std::vector<std::string> uris;
  std::vector<CurlEndpoint> endpoints;
  std::array<long, 2> families{
      {CURL_IPRESOLVE_WHATEVER, CURL_IPRESOLVE_WHATEVER}};
  std::deque<long> idleWorkers;
  size_t preferredUriIndex = 0;
  std::string path;
  std::string currentUri;
  std::string etag;
  std::string lastModified;
  std::unique_ptr<DiskWriter> writer;
  std::vector<std::unique_ptr<CurlHandle>> handles;
  RangePlanner planner;
  RequestGroup* group = nullptr;
  int maxConnections = 1;
  int connectionLimit = 1;
  uint64_t connectionEpoch = 0;
  int64_t lastRecoveryDownloadLength = 0;
  std::chrono::steady_clock::time_point recoverConnectionsAt{};
  int fileNotFoundCount = 0;
  int64_t existingLength = 0;
  CurlStartMode startMode = CurlStartMode::Transfer;
  bool dryRun = false;
  bool http = false;
  bool rangeValidated = false;
  bool allowFullRestart = false;
  bool fullDownload = false;
  bool plannerConfigured = false;
  bool kickPending = false;
  bool stopRequested = false;
  bool createdOutput = false;
  Timer lastCheckpoint = Timer::zero();
};

} // namespace aria2

#endif // D_CURL_DOWNLOAD_IMPL_H
