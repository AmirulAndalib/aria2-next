/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2006 Tatsuhiro Tsujikawa
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
/* copyright --> */
#include "AbstractCommand.h"

#include <algorithm>
#include <functional>

#include "Request.h"
#include "DownloadEngine.h"
#include "Option.h"
#include "PeerStat.h"
#include "SegmentMan.h"
#include "Log.h"
#include "Segment.h"
#include "DlAbortEx.h"
#include "DlRetryEx.h"
#include "DownloadFailureException.h"
#include "PieceStorage.h"
#include "SocketCore.h"
#include "message.h"
#include "prefs.h"
#include "fmt.h"
#include "ServerStat.h"
#include "RequestGroupMan.h"
#include "util.h"
#include "Log.h"
#include "DownloadContext.h"
#include "wallclock.h"
#include "NameResolver.h"
#include "uri.h"
#include "FileEntry.h"
#include "error_code.h"
#include "SocketRecvBuffer.h"
#include "ChecksumCheckIntegrityEntry.h"
#ifdef ENABLE_ASYNC_DNS
#  include "AsyncNameResolver.h"
#  include "AsyncNameResolverMan.h"
#endif // ENABLE_ASYNC_DNS

namespace aria2 {

AbstractCommand::AbstractCommand(
    cuid_t cuid, const std::shared_ptr<Request>& req,
    const std::shared_ptr<FileEntry>& fileEntry, RequestGroup* requestGroup,
    DownloadEngine* e, const std::shared_ptr<SocketCore>& s,
    const std::shared_ptr<SocketRecvBuffer>& socketRecvBuffer,
    bool incNumConnection, bool incNumStreamCommand)
    : Command(cuid),
      req_(req),
      fileEntry_(fileEntry),
      socket_(s),
      socketRecvBuffer_(socketRecvBuffer),
#ifdef ENABLE_ASYNC_DNS
      asyncNameResolverMan_(make_unique<AsyncNameResolverMan>()),
#endif // ENABLE_ASYNC_DNS
      requestGroup_(requestGroup),
      e_(e),
      checkPoint_(global::wallclock()),
      serverStatTimer_(global::wallclock()),
      timeout_(requestGroup->getTimeout()),
      checkSocketIsReadable_(false),
      checkSocketIsWritable_(false),
      incNumConnection_(incNumConnection),
      incNumStreamCommand_(incNumStreamCommand)
{
  if (socket_ && socket_->isOpen()) {
    setReadCheckSocket(socket_);
  }
  if (incNumConnection_) {
    requestGroup->increaseStreamConnection();
  }
  if (incNumStreamCommand_) {
    requestGroup_->increaseStreamCommand();
  }
  requestGroup_->increaseNumCommand();
#ifdef ENABLE_ASYNC_DNS
  configureAsyncNameResolverMan(asyncNameResolverMan_.get(), e_->getOption());
#endif // ENABLE_ASYNC_DNS
}

AbstractCommand::~AbstractCommand()
{
  disableReadCheckSocket();
  disableWriteCheckSocket();
#ifdef ENABLE_ASYNC_DNS
  asyncNameResolverMan_->disableNameResolverCheck(e_, this);
#endif // ENABLE_ASYNC_DNS
  requestGroup_->decreaseNumCommand();
  if (incNumStreamCommand_) {
    requestGroup_->decreaseStreamCommand();
  }
  if (incNumConnection_) {
    requestGroup_->decreaseStreamConnection();
  }
}

void AbstractCommand::changeRequestGroup(RequestGroup* requestGroup)
{
  if (!requestGroup || requestGroup == requestGroup_) {
    return;
  }
  requestGroup_->decreaseNumCommand();
  if (incNumStreamCommand_) {
    requestGroup_->decreaseStreamCommand();
  }
  if (incNumConnection_) {
    requestGroup_->decreaseStreamConnection();
  }

  requestGroup_ = requestGroup;
  fileEntry_ = requestGroup_->getDownloadContext()->getFirstFileEntry();
  timeout_ = requestGroup_->getTimeout();

  if (incNumConnection_) {
    requestGroup_->increaseStreamConnection();
  }
  if (incNumStreamCommand_) {
    requestGroup_->increaseStreamCommand();
  }
  requestGroup_->increaseNumCommand();
}

bool AbstractCommand::shouldProcess() const
{
  if (checkSocketIsReadable_) {
    if (readEventEnabled()) {
      return true;
    }

    if (socketRecvBuffer_ && !socketRecvBuffer_->bufferEmpty()) {
      return true;
    }

    if (socket_ && socket_->getRecvBufferedLength()) {
      return true;
    }
  }

  if (checkSocketIsWritable_ && writeEventEnabled()) {
    return true;
  }

#ifdef ENABLE_ASYNC_DNS
  const auto resolverChecked = asyncNameResolverMan_->resolverChecked();
  if (resolverChecked && asyncNameResolverMan_->getStatus() != 0) {
    return true;
  }

  if (!checkSocketIsReadable_ && !checkSocketIsWritable_ && !resolverChecked) {
    return true;
  }
#else  // ENABLE_ASYNC_DNS
  if (!checkSocketIsReadable_ && !checkSocketIsWritable_) {
    return true;
  }
#endif // ENABLE_ASYNC_DNS

  return noCheck();
}

bool AbstractCommand::execute()
{
  try {
    if (requestGroup_->downloadFinished() || requestGroup_->isHaltRequested()) {
      return true;
    }

    if (req_ && req_->removalRequested()) {
      A2_LOG_TRACE(fmt("CUID#%" PRId64
                       " - Discard original URI=%s because it is"
                       " requested.",
                       getCuid(), req_->getUri().c_str()));
      return prepareForRetry(0);
    }

    auto sm = getSegmentMan();

    if (getPieceStorage()) {
      segments_.clear();
      sm->getInFlightSegment(segments_, getCuid());

      if (req_ && segments_.empty()) {
        // This command previously has assigned segments, but it is
        // canceled. So discard current request chain.  Plus, if no
        // segment is available when http pipelining is used.
        A2_LOG_TRACE(fmt("CUID#%" PRId64
                         " - It seems previously assigned segments"
                         " are canceled. Restart.",
                         getCuid()));
        return prepareForRetry(0);
      }
    }

    if (shouldProcess()) {
      checkPoint_ = global::wallclock();

      if (!getPieceStorage()) {
        return executeInternal();
      }

      if (!req_ || req_->getMaxPipelinedRequest() == 1 ||
          // Why the following condition is necessary? That's because
          // For single file download, SegmentMan::getSegment(cuid)
          // is more efficient.
          getDownloadContext()->getFileEntries().size() == 1) {
        size_t maxSegments = req_ ? req_->getMaxPipelinedRequest() : 1;
        size_t minSplitSize = calculateMinSplitSize();
        while (segments_.size() < maxSegments) {
          auto segment = sm->getSegment(getCuid(), minSplitSize);
          if (!segment) {
            break;
          }
          segments_.push_back(segment);
        }
        if (segments_.empty()) {
          // TODO socket could be pooled here if pipelining is
          // enabled...  Hmm, I don't think if pipelining is enabled
          // it does not go here.
          A2_LOG_DEBUG(fmt(MSG_NO_SEGMENT_AVAILABLE, getCuid()));
          // When all segments are ignored in SegmentMan, there are
          // no URIs available, so don't retry.
          if (sm->allSegmentsIgnored()) {
            A2_LOG_TRACE("All segments are ignored.");
            // This will execute other idle Commands and let them
            // finish quickly.
            e_->setRefreshInterval(std::chrono::milliseconds(0));
            return true;
          }

          return prepareForRetry(1);
        }
      }
      else {
        // For multi-file downloads
        size_t minSplitSize = calculateMinSplitSize();
        size_t maxSegments = req_->getMaxPipelinedRequest();
        if (segments_.size() < maxSegments) {
          sm->getSegment(segments_, getCuid(), minSplitSize, fileEntry_,
                         maxSegments);
        }
        if (segments_.empty()) {
          return prepareForRetry(0);
        }
      }

      return executeInternal();
    }

    if (errorEventEnabled()) {
      // older kernel may report "connection refused" here.
      auto ss = e_->getRequestGroupMan()->getOrCreateServerStat(
          req_->getHost(), req_->getProtocol());
      ss->setError();

      throw DL_RETRY_EX(
          fmt(MSG_NETWORK_PROBLEM, socket_->getSocketError().c_str()));
    }

    if (checkPoint_.difference(global::wallclock()) >= timeout_) {
      // timeout triggers ServerStat error state.
      auto ss = e_->getRequestGroupMan()->getOrCreateServerStat(
          req_->getHost(), req_->getProtocol());
      ss->setError();
      // When DNS query was timeout, req_->getConnectedAddr() is
      // empty.
      if (!req_->getConnectedAddr().empty()) {
        // Purging IP address cache to renew IP address.
        A2_LOG_TRACE(fmt("CUID#%" PRId64 " - Marking IP address %s as bad",
                         getCuid(), req_->getConnectedAddr().c_str()));
        e_->markBadIPAddress(req_->getConnectedHostname(),
                             req_->getConnectedAddr(),
                             req_->getConnectedPort());
      }
      if (e_->findCachedIPAddress(req_->getConnectedHostname(),
                                  req_->getConnectedPort())
              .empty()) {
        A2_LOG_TRACE(fmt("CUID#%" PRId64 " - All IP addresses were marked bad."
                         " Removing Entry.",
                         getCuid()));
        e_->removeCachedIPAddress(req_->getConnectedHostname(),
                                  req_->getConnectedPort());
      }
      throw DL_RETRY_EX2(EX_TIME_OUT, error_code::TIME_OUT);
    }

    addCommandSelf();
    return false;
  }
  catch (DlAbortEx& err) {
    requestGroup_->setLastErrorCode(err.getErrorCode(), err.what());
    if (req_) {
      A2_LOG_ERROR_EX(
          fmt(MSG_DOWNLOAD_ABORTED, getCuid(),
              logging::sanitizeUri(req_->getUri()).c_str()),
          DL_ABORT_EX2(fmt("URI=%s",
                           logging::sanitizeUri(req_->getCurrentUri()).c_str()),
                       err));
      fileEntry_->addURIResult(req_->getUri(), err.getErrorCode());
      if (err.getErrorCode() == error_code::CANNOT_RESUME) {
        requestGroup_->increaseResumeFailureCount();
      }
    }
    else {
      A2_LOG_TRACE_EX(EX_EXCEPTION_CAUGHT, err);
    }
    onAbort();
    tryReserved();
    return true;
  }
  catch (DlRetryEx& err) {
    assert(req_);
    A2_LOG_DEBUG_EX(
        fmt(MSG_RESTARTING_DOWNLOAD, getCuid(),
            logging::sanitizeUri(req_->getUri()).c_str()),
        DL_RETRY_EX2(
            fmt("URI=%s", logging::sanitizeUri(req_->getCurrentUri()).c_str()),
            err));
    req_->addTryCount();
    req_->resetRedirectCount();
    req_->resetUri();

    const int maxTries = getOption()->getAsInt(PREF_MAX_TRIES);
    bool isAbort = maxTries != 0 && req_->getTryCount() >= maxTries;
    if (isAbort) {
      A2_LOG_DEBUG(fmt(MSG_MAX_TRY, getCuid(), req_->getTryCount()));
      A2_LOG_ERROR_EX(fmt(MSG_DOWNLOAD_ABORTED, getCuid(),
                          logging::sanitizeUri(req_->getUri()).c_str()),
                      err);
      fileEntry_->addURIResult(req_->getUri(), err.getErrorCode());
      requestGroup_->setLastErrorCode(err.getErrorCode(), err.what());
      if (err.getErrorCode() == error_code::CANNOT_RESUME) {
        requestGroup_->increaseResumeFailureCount();
      }
      onAbort();
      tryReserved();
      return true;
    }

    if (err.getErrorCode() == error_code::HTTP_SERVICE_UNAVAILABLE) {
      Timer wakeTime(global::wallclock());
      wakeTime.advance(
          std::chrono::seconds(getOption()->getAsInt(PREF_RETRY_WAIT)));
      req_->setWakeTime(wakeTime);
      req_->setResetTryCountAfterWake(true);
    }

    return prepareForRetry(0);
  }
  catch (DownloadFailureException& err) {
    requestGroup_->setLastErrorCode(err.getErrorCode(), err.what());
    if (req_) {
      A2_LOG_ERROR_EX(
          fmt(MSG_DOWNLOAD_ABORTED, getCuid(),
              logging::sanitizeUri(req_->getUri()).c_str()),
          DL_ABORT_EX2(fmt("URI=%s",
                           logging::sanitizeUri(req_->getCurrentUri()).c_str()),
                       err));
      fileEntry_->addURIResult(req_->getUri(), err.getErrorCode());
    }
    else {
      A2_LOG_ERROR_EX(EX_EXCEPTION_CAUGHT, err);
    }
    requestGroup_->setHaltRequested(true);
    getDownloadEngine()->setRefreshInterval(std::chrono::milliseconds(0));
    return true;
  }
}

void AbstractCommand::tryReserved() {}

bool AbstractCommand::prepareForRetry(time_t wait)
{
  if (getPieceStorage()) {
    getSegmentMan()->cancelSegment(getCuid());
  }
  if (req_) {
    // Reset persistentConnection and maxPipelinedRequest to handle
    // the situation where remote server returns Connection: close
    // after several pipelined requests.
    req_->supportsPersistentConnection(true);
    req_->setMaxPipelinedRequest(1);

    fileEntry_->poolRequest(req_);
    A2_LOG_TRACE(fmt("CUID#%" PRId64 " - Pooling request URI=%s", getCuid(),
                     logging::sanitizeUri(req_->getUri()).c_str()));
    if (getSegmentMan()) {
      getSegmentMan()->recognizeSegmentFor(fileEntry_);
    }
  }

  (void)wait;
  return true;
}

void AbstractCommand::onAbort()
{
  if (req_) {
    fileEntry_->removeIdenticalURI(req_->getUri());
    fileEntry_->removeRequest(req_);
  }

  A2_LOG_TRACE(fmt("CUID#%" PRId64 " - Aborting download", getCuid()));
  if (!getPieceStorage()) {
    return;
  }

  getSegmentMan()->cancelSegment(getCuid());
  // Don't do following process if BitTorrent is involved or files
  // in DownloadContext is more than 1. The latter condition is
  // limitation of current implementation.
  if (getOption()->getAsBool(PREF_ALWAYS_RESUME) || !fileEntry_ ||
      getDownloadContext()->getNetStat().getSessionDownloadLength() != 0 ||
      requestGroup_->p2pInvolved() ||
      getDownloadContext()->getFileEntries().size() != 1) {
    return;
  }

  const int maxTries = getOption()->getAsInt(PREF_MAX_RESUME_FAILURE_TRIES);
  if (!(maxTries > 0 && requestGroup_->getResumeFailureCount() >= maxTries) &&
      !fileEntry_->emptyRequestUri()) {
    return;
  }
  // Local file exists, but given servers(or at least contacted
  // ones) doesn't support resume. Let's restart download from
  // scratch.
  A2_LOG_INFO(fmt(_("CUID#%" PRId64 " - Failed to resume download."
                    " Download from scratch."),
                  getCuid()));
  A2_LOG_TRACE(fmt("CUID#%" PRId64 " - Gathering URIs that has CANNOT_RESUME"
                   " error",
                   getCuid()));
  // Set PREF_ALWAYS_RESUME to A2_V_TRUE to avoid repeating this
  // process.
  getOption()->put(PREF_ALWAYS_RESUME, A2_V_TRUE);
  std::deque<URIResult> res;
  fileEntry_->extractURIResult(res, error_code::CANNOT_RESUME);
  if (res.empty()) {
    return;
  }

  getSegmentMan()->cancelAllSegments();
  getSegmentMan()->eraseSegmentWrittenLengthMemo();
  getPieceStorage()->markPiecesDone(0);
  std::vector<std::string> uris;
  uris.reserve(res.size());
  std::transform(std::begin(res), std::end(res), std::back_inserter(uris),
                 std::mem_fn(&URIResult::getURI));
  A2_LOG_TRACE(fmt("CUID#%" PRId64 " - %lu URIs found.", getCuid(),
                   static_cast<unsigned long int>(uris.size())));
  fileEntry_->addUris(std::begin(uris), std::end(uris));
  getSegmentMan()->recognizeSegmentFor(fileEntry_);
}

void AbstractCommand::disableReadCheckSocket()
{
  if (!checkSocketIsReadable_) {
    return;
  }

  e_->deleteSocketForReadCheck(readCheckTarget_, this);
  checkSocketIsReadable_ = false;
  readCheckTarget_.reset();
}

void AbstractCommand::setReadCheckSocket(
    const std::shared_ptr<SocketCore>& socket)
{
  if (!socket->isOpen()) {
    disableReadCheckSocket();
    return;
  }

  if (checkSocketIsReadable_) {
    if (*readCheckTarget_ != *socket) {
      e_->deleteSocketForReadCheck(readCheckTarget_, this);
      e_->addSocketForReadCheck(socket, this);
      readCheckTarget_ = socket;
    }
    return;
  }

  e_->addSocketForReadCheck(socket, this);
  checkSocketIsReadable_ = true;
  readCheckTarget_ = socket;
}

void AbstractCommand::setReadCheckSocketIf(
    const std::shared_ptr<SocketCore>& socket, bool pred)
{
  if (pred) {
    setReadCheckSocket(socket);
    return;
  }

  disableReadCheckSocket();
}

void AbstractCommand::disableWriteCheckSocket()
{
  if (!checkSocketIsWritable_) {
    return;
  }
  e_->deleteSocketForWriteCheck(writeCheckTarget_, this);
  checkSocketIsWritable_ = false;
  writeCheckTarget_.reset();
}

void AbstractCommand::setWriteCheckSocket(
    const std::shared_ptr<SocketCore>& socket)
{
  if (!socket->isOpen()) {
    disableWriteCheckSocket();
    return;
  }

  if (checkSocketIsWritable_) {
    if (*writeCheckTarget_ != *socket) {
      e_->deleteSocketForWriteCheck(writeCheckTarget_, this);
      e_->addSocketForWriteCheck(socket, this);
      writeCheckTarget_ = socket;
    }
    return;
  }

  e_->addSocketForWriteCheck(socket, this);
  checkSocketIsWritable_ = true;
  writeCheckTarget_ = socket;
}

void AbstractCommand::setWriteCheckSocketIf(
    const std::shared_ptr<SocketCore>& socket, bool pred)
{
  if (pred) {
    setWriteCheckSocket(socket);
    return;
  }

  disableWriteCheckSocket();
}

void AbstractCommand::swapSocket(std::shared_ptr<SocketCore>& socket)
{
  disableReadCheckSocket();
  disableWriteCheckSocket();
  socket_.swap(socket);
}

std::string AbstractCommand::resolveHostname(std::vector<std::string>& addrs,
                                             const std::string& hostname,
                                             uint16_t port)
{
  if (util::isNumericHost(hostname)) {
    addrs.push_back(hostname);
    return hostname;
  }

  e_->findAllCachedIPAddresses(std::back_inserter(addrs), hostname, port);
  if (!addrs.empty()) {
    auto ipaddr = addrs.front();
    A2_LOG_DEBUG(
        fmt(MSG_DNS_CACHE_HIT, getCuid(), hostname.c_str(),
            strjoin(std::begin(addrs), std::end(addrs), ", ").c_str()));
    return ipaddr;
  }

  std::string ipaddr;
  auto resolveWithSystemResolver = [&]() {
    NameResolver res;
    res.setSocktype(SOCK_STREAM);
    if (e_->getOption()->getAsBool(PREF_DISABLE_IPV6)) {
      res.setFamily(AF_INET);
    }
    res.resolve(addrs, hostname);
  };
#ifdef ENABLE_ASYNC_DNS
  if (getOption()->getAsBool(PREF_ASYNC_DNS)) {
    if (!asyncNameResolverMan_->started()) {
      asyncNameResolverMan_->startAsync(hostname, e_, this);
    }
    switch (asyncNameResolverMan_->getStatus()) {
    case -1:
      if (asyncNameResolverMan_->shouldFallbackToSystemResolver()) {
        try {
          resolveWithSystemResolver();
          A2_LOG_DEBUG(fmt("CUID#%" PRId64
                           " - Falling back to system name resolver for %s",
                           getCuid(), hostname.c_str()));
          break;
        }
        catch (const DlAbortEx& ex) {
          throw DL_ABORT_EX2(fmt(MSG_NAME_RESOLUTION_FAILED, getCuid(),
                                 hostname.c_str(),
                                 asyncNameResolverMan_->getLastError().c_str()),
                             ex);
        }
      }
      e_->getRequestGroupMan()
          ->getOrCreateServerStat(req_->getHost(), req_->getProtocol())
          ->setError();
      throw DL_ABORT_EX2(fmt(MSG_NAME_RESOLUTION_FAILED, getCuid(),
                             hostname.c_str(),
                             asyncNameResolverMan_->getLastError().c_str()),
                         error_code::NAME_RESOLVE_ERROR);
    case 0:
      return "";

    case 1:
      asyncNameResolverMan_->getResolvedAddress(addrs);
      if (addrs.empty()) {
        throw DL_ABORT_EX2(fmt(MSG_NAME_RESOLUTION_FAILED, getCuid(),
                               hostname.c_str(), "No address returned"),
                           error_code::NAME_RESOLVE_ERROR);
      }
      break;
    }
  }
  else
#endif // ENABLE_ASYNC_DNS
  {
    resolveWithSystemResolver();
  }
  A2_LOG_DEBUG(fmt(MSG_NAME_RESOLUTION_COMPLETE, getCuid(), hostname.c_str(),
                   strjoin(std::begin(addrs), std::end(addrs), ", ").c_str()));
  for (const auto& addr : addrs) {
    e_->cacheIPAddress(hostname, addr, port);
  }
  ipaddr = e_->findCachedIPAddress(hostname, port);
  return ipaddr;
}

bool AbstractCommand::checkIfConnectionEstablished(
    const std::shared_ptr<SocketCore>& socket,
    const std::string& connectedHostname, const std::string& connectedAddr,
    uint16_t connectedPort)
{
  std::string error = socket->getSocketError();
  if (error.empty()) {
    return true;
  }

  // See also InitiateConnectionCommand::executeInternal()
  e_->markBadIPAddress(connectedHostname, connectedAddr, connectedPort);
  if (e_->findCachedIPAddress(connectedHostname, connectedPort).empty()) {
    e_->removeCachedIPAddress(connectedHostname, connectedPort);
    e_->getRequestGroupMan()
        ->getOrCreateServerStat(req_->getHost(), req_->getProtocol())
        ->setError();
    throw DL_RETRY_EX(fmt(MSG_ESTABLISHING_CONNECTION_FAILED, error.c_str()));
  }

  A2_LOG_DEBUG(fmt(MSG_CONNECT_FAILED_AND_RETRY, getCuid(),
                   connectedAddr.c_str(), connectedPort));
  throw DL_RETRY_EX(fmt(MSG_ESTABLISHING_CONNECTION_FAILED, error.c_str()));
}

const std::shared_ptr<Option>& AbstractCommand::getOption() const
{
  return requestGroup_->getOption();
}

void AbstractCommand::createSocket()
{
  socket_ = std::make_shared<SocketCore>();
}

int32_t AbstractCommand::calculateMinSplitSize() const
{
  if (req_ && req_->isPipeliningEnabled()) {
    return getDownloadContext()->getPieceLength();
  }

  return getOption()->getAsInt(PREF_ED2K_MIN_SPLIT_SIZE);
}

void AbstractCommand::setRequest(const std::shared_ptr<Request>& request)
{
  req_ = request;
}

void AbstractCommand::resetRequest() { req_.reset(); }

void AbstractCommand::setFileEntry(const std::shared_ptr<FileEntry>& fileEntry)
{
  fileEntry_ = fileEntry;
}

void AbstractCommand::setSocket(const std::shared_ptr<SocketCore>& s)
{
  socket_ = s;
}

const std::shared_ptr<DownloadContext>&
AbstractCommand::getDownloadContext() const
{
  return requestGroup_->getDownloadContext();
}

const std::shared_ptr<SegmentMan>& AbstractCommand::getSegmentMan() const
{
  return requestGroup_->getSegmentMan();
}

const std::shared_ptr<PieceStorage>& AbstractCommand::getPieceStorage() const
{
  return requestGroup_->getPieceStorage();
}

void AbstractCommand::checkSocketRecvBuffer()
{
  if (socketRecvBuffer_->bufferEmpty() &&
      socket_->getRecvBufferedLength() == 0) {
    return;
  }

  setStatus(Command::STATUS_ONESHOT_REALTIME);
  e_->setNoWait(true);
}

void AbstractCommand::addCommandSelf()
{
  e_->addCommand(std::unique_ptr<Command>(this));
}

} // namespace aria2
