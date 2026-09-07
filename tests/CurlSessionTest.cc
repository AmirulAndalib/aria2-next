#include "CurlSession.h"

#include <algorithm>

#include "CurlDownload.h"
#include "CurlDownloadImpl.h"
#include "ByteArrayDiskWriter.h"
#include "DownloadContext.h"
#include "DiskWriter.h"
#include "DownloadEngine.h"
#include "FileEntry.h"
#include "DownloadFailureException.h"
#include "Option.h"
#include "RequestGroup.h"
#include "SelectEventPoll.h"
#include "SocketCore.h"
#include "a2doctest.h"
#include "a2functional.h"
#include "prefs.h"
#include "wallclock.h"

namespace aria2 {

namespace {

class FailingDiskWriter final : public DiskWriter {
public:
  void initAndOpenFile(int64_t) override {}
  void openFile(int64_t) override {}
  void closeFile() override {}
  void openExistingFile(int64_t) override {}
  int64_t size() override { return 0; }

  void writeData(const unsigned char*, size_t, int64_t) override
  {
    throw DOWNLOAD_FAILURE_EXCEPTION2("Disk is full",
                                      error_code::NOT_ENOUGH_DISK_SPACE);
  }

  ssize_t readData(unsigned char*, size_t, int64_t) override { return 0; }
};

} // namespace

class CurlSessionTest {
public:
  void testWriteErrorBoundary();
  void testOutputFilename();
  void testResponseIdentity();
  void testRangeOwnershipAndResponseBoundaries();
  void testNonzeroRangeRejectsCompleteResponse();
  void testUnsatisfiedRangeResponseForms();
  void testExistingFileDecision();
  void testOutcomeClassification();
  void testFailureMessageUsesTheFailureLayer();
  void testShutdownWithLiveSocket();
  void testTailRecovery();
  void sendHeader(CurlHandle& handle, const std::string& line);
};

A2_TEST(CurlSessionTest, testWriteErrorBoundary)
A2_TEST(CurlSessionTest, testOutputFilename)
A2_TEST(CurlSessionTest, testResponseIdentity)
A2_TEST(CurlSessionTest, testRangeOwnershipAndResponseBoundaries)
A2_TEST(CurlSessionTest, testNonzeroRangeRejectsCompleteResponse)
A2_TEST(CurlSessionTest, testUnsatisfiedRangeResponseForms)
A2_TEST(CurlSessionTest, testExistingFileDecision)
A2_TEST(CurlSessionTest, testOutcomeClassification)
A2_TEST(CurlSessionTest, testFailureMessageUsesTheFailureLayer)
A2_TEST(CurlSessionTest, testShutdownWithLiveSocket)
A2_TEST(CurlSessionTest, testTailRecovery)

void CurlSessionTest::testOutputFilename()
{
  auto option = std::make_shared<Option>();
  option->put(PREF_STATE_DIR, A2_TEST_OUT_DIR "/curl-filename-state");
  option->put(PREF_DIR, A2_TEST_OUT_DIR "/curl-filename");
  CurlSession session(option.get());
  const std::pair<std::string, std::string> cases[] = {
      {"%E4%B8%AD%E6%96%87%20file.bin", "中文 file.bin"},
      {"a%2520b.bin", "a%20b.bin"},
      {"a+b.bin?filename=ignored", "a+b.bin"},
      {"bad%ZZ.bin", "bad%ZZ.bin"},
      {"bad%FF.bin", "bad%FF.bin"},
      {"dir%2Fchild.bin", "dir%2Fchild.bin"},
      {"zero%00byte.bin", "zero%00byte.bin"},
      {"line%0Abreak.bin", "line%0Abreak.bin"},
      {"%2E%2E", "%2E%2E"},
      {"%2E", "%2E"},
      {"", "index.html"},
  };
  for (const auto& entry : cases) {
    RequestGroup group(GroupId::create(), option);
    group.setDownloadContext(std::make_shared<DownloadContext>(1_m, 0, ""));
    auto download = std::make_shared<CurlDownload>(
        std::vector<std::string>{"https://example.test/" + entry.first});
    session.restorePaused(download, &group);
    const auto expected = option->get(PREF_DIR) + "/" + entry.second;
    CHECK_EQ(expected, group.getFirstFilePath());
    // A fresh snapshot must not decode an already selected output path again.
    download = std::make_shared<CurlDownload>(
        std::vector<std::string>{"https://example.test/other"});
    session.restorePaused(download, &group);
    CHECK_EQ(expected, group.getFirstFilePath());
  }
  option->put(PREF_OUT, "literal%20name.bin");
  RequestGroup group(GroupId::create(), option);
  group.setDownloadContext(std::make_shared<DownloadContext>(1_m, 0, ""));
  auto download = std::make_shared<CurlDownload>(
      std::vector<std::string>{"https://example.test/%E4%B8%AD.bin"});
  session.restorePaused(download, &group);
  CHECK_EQ(option->get(PREF_DIR) + "/literal%20name.bin",
           group.getFirstFilePath());
}

void CurlSessionTest::testTailRecovery()
{
  auto option = std::make_shared<Option>();
  option->put(PREF_STATE_DIR, A2_TEST_OUT_DIR "/curl-tail");
  option->put(PREF_RETRY_WAIT, "10");
  option->put(PREF_MAX_TRIES, "4");
  RequestGroup group(GroupId::create(), option);
  group.setDownloadContext(
      std::make_shared<DownloadContext>(1_m, 1_m, "payload"));
  auto engine = make_unique<DownloadEngine>(make_unique<SelectEventPoll>());
  engine->setOption(option.get());
  auto* session = engine->getCurlSession();
  session->engine_ = engine.get();
  auto download = std::make_shared<CurlDownload>(
      std::vector<std::string>{"http://example.test/payload"});
  auto& impl = *download->impl_;
  impl.group = &group;
  impl.writer = make_unique<ByteArrayDiskWriter>();
  impl.http = true;
  impl.maxConnections = 64;
  impl.admission.reset(64, std::chrono::seconds(0));
  auto handle = make_unique<CurlHandle>();
  handle->value = curl_easy_init();
  REQUIRE(handle->value);
  handle->rangeAccepted = true;
  handle->download = download.get();
  handle->lease = {0, 1_m};
  handle->responseRangeEnd = 1_m;
  handle->writeOffset = 8_k;
  global::wallclock().reset();
  handle->responseStartedAt = global::wallclock();
  handle->lastPayload = global::wallclock();
  handle->payloadSpeed.reset();
  handle->payloadSpeed.update(8_k);
  impl.handles.push_back(std::move(handle));

  // The body sample must span at least one second before a split.
  global::wallclock().advance(std::chrono::milliseconds(500));
  CHECK(!session->rebalanceEndgame(download, 1_m));
  global::wallclock().advance(std::chrono::milliseconds(600));
  for (int i = 0; i < 32; ++i) {
    impl.handles.push_back(make_unique<CurlHandle>());
  }
  group.setMaxDownloadSpeedLimit(1_k);
  CHECK(!session->rebalanceEndgame(download, 1_m));
  group.setMaxDownloadSpeedLimit(0);
  CHECK(session->rebalanceEndgame(download, 1_m));
  REQUIRE_EQ(34, impl.handles.size());
  auto* donor = impl.handles.front().get();
  auto* helper = impl.handles.back().get();
  std::optional<RangeLease> lease = helper->lease;
  CHECK_EQ(donor->lease.end, lease->begin);
  CHECK_EQ(1_m, lease->end);
  CHECK(!impl.planner.takeReady({}));

  std::string body(static_cast<size_t>(1_m - donor->writeOffset), 'x');
  CHECK_EQ(CURL_WRITEFUNC_ERROR,
           CurlSession::writeData(body.data(), 1, body.size(), donor));
  CHECK_EQ(lease->begin, donor->writeOffset);
  CHECK_EQ(lease->begin, impl.writer->size());
  CHECK_EQ(error_code::UNDEFINED, download->snapshot_.errorCode);
  impl.planner.commit(0, 8_k);
  RangePlanner restored;
  restored.restore(impl.planner.completedRanges());
  restored.configure(1_m, 1_m, {});
  auto missing = restored.takeReady({});
  REQUIRE(missing);
  CHECK_EQ(lease->begin, missing->begin);
  CHECK_EQ(lease->end, missing->end);
  CHECK(!restored.takeReady({}));
  session->discardHandle(download, donor);
  session->discardHandle(download, helper);
  impl.handles.clear();

  auto replacement = make_unique<CurlHandle>();
  replacement->value = curl_easy_init();
  REQUIRE(replacement->value);
  replacement->lease = *lease;
  replacement->writeOffset = lease->begin + 16_k;
  replacement->rangeAccepted = true;
  replacement->responseStartedAt = global::wallclock();
  impl.handles.push_back(std::move(replacement));
  global::wallclock().advance(20_s);
  impl.handles.front()->lease.attempts = 1;
  CHECK(!session->rebalanceEndgame(download, 1_m));
  impl.handles.front()->lease.attempts = 0;
  // A response below 64 KiB must not monopolize its unfinished range.
  REQUIRE(session->rebalanceEndgame(download, 1_m));
  auto* slow = impl.handles.front().get();
  helper = impl.handles.back().get();
  // An idle slot can take another disjoint suffix before the first helper
  // responds. The live prefix and both helpers retain exclusive ranges.
  REQUIRE(session->rebalanceEndgame(download, 1_m));
  auto* nextHelper = impl.handles.back().get();
  CHECK_EQ(slow->lease.end, nextHelper->lease.begin);
  CHECK_EQ(nextHelper->lease.end, helper->lease.begin);
  CHECK_EQ(helper->lease.begin, helper->writeOffset);
  session->discardHandle(download, nextHelper);
  session->discardHandle(download, helper);

  // A rejected sibling while the replacement is being served: the range goes
  // back to the queue, the window never drops below the served connection, and
  // a Retry-After hold outranks the shorter configured retry wait.
  const auto before = std::chrono::steady_clock::now();
  const auto epoch = impl.admission.epoch();
  REQUIRE(session->requeue(download, {0, 80_k, 0}, epoch,
                           TransferOutcome::Overloaded, 503, 12));
  CHECK_EQ(32, impl.admission.limit());
  CHECK(!impl.admission.open(before + 11_s));
  CHECK(impl.admission.open(before + 13_s));
  CHECK(!impl.planner.takeReady(before + 11_s));
  auto queued = impl.planner.takeReady(before + 13_s);
  REQUIRE(queued);
  CHECK_EQ(0, queued->begin);
  CHECK_EQ(80_k, queued->end);
  CHECK_EQ(1, queued->attempts);

  // HTTP 500 and failed probes retain a finite budget even with healthy
  // siblings. The configured delay applies to the failed range only.
  option->put(PREF_MAX_TRIES, "2");
  impl.admission.reset(64, std::chrono::seconds(0));
  REQUIRE(session->requeue(download, {0, 80_k, 0}, impl.admission.epoch(),
                           TransferOutcome::Retryable, 500, 0));
  CHECK(impl.admission.open(before));
  CHECK_EQ(64, impl.admission.limit());
  CHECK(!impl.planner.takeReady(before + 9_s));
  queued = impl.planner.takeReady(before + 11_s);
  REQUIRE(queued);
  CHECK_EQ(1, queued->attempts);
  CHECK(!session->requeue(download, *queued, impl.admission.epoch(),
                          TransferOutcome::Retryable, 500, 0));
  session->discardHandle(download, slow);
  global::wallclock().reset();
}

void CurlSessionTest::testShutdownWithLiveSocket()
{
  SocketCore listener;
  listener.bind("127.0.0.1", 0, AF_INET);
  listener.beginListen();
  Option option;
  option.put(PREF_STATE_DIR, A2_TEST_OUT_DIR "/curl-shutdown");
  auto engine = make_unique<DownloadEngine>(make_unique<SelectEventPoll>());
  engine->setOption(&option);
  auto* session = engine->getCurlSession();
  session->engine_ = engine.get();
  auto easy = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>(
      curl_easy_init(), curl_easy_cleanup);
  REQUIRE(easy);
  const auto url =
      "http://127.0.0.1:" + std::to_string(listener.getAddrInfo().port) + "/";
  REQUIRE_EQ(CURLE_OK, curl_easy_setopt(easy.get(), CURLOPT_URL, url.c_str()));
  REQUIRE_EQ(CURLE_OK, curl_easy_setopt(easy.get(), CURLOPT_PROXY, ""));
  REQUIRE_EQ(CURLM_OK, curl_multi_add_handle(session->multi_, easy.get()));
  session->socketAction(CURL_SOCKET_TIMEOUT, 0);
  REQUIRE(!session->sockets_.empty());
  // Destroying the engine must unregister native callbacks before deleting
  // the commands they reference. AddressSanitizer detects the reversed order.
  engine.reset();
}

void CurlSessionTest::sendHeader(CurlHandle& handle, const std::string& line)
{
  auto value = line;
  REQUIRE_EQ(value.size(), CurlSession::receiveHeader(value.data(), 1,
                                                      value.size(), &handle));
}

void CurlSessionTest::testWriteErrorBoundary()
{
  CurlDownload download({"https://example.invalid/file"});
  download.impl_->writer = make_unique<FailingDiskWriter>();
  CurlHandle handle;
  handle.download = &download;
  char data[] = "data";

  CHECK_EQ(CURL_WRITEFUNC_ERROR,
           CurlSession::writeData(data, 1, sizeof(data) - 1, &handle));
  CHECK_EQ(CurlSnapshot::State::Error, download.snapshot().state);
  CHECK_EQ(error_code::NOT_ENOUGH_DISK_SPACE, download.snapshot().errorCode);
  CHECK_EQ(std::string("Disk is full"), download.snapshot().error);
}

void CurlSessionTest::testResponseIdentity()
{
  for (const auto& tag : {"\"revision-one\"", "\"\"", "W/\"weak\"", "bare",
                          "\"bad space\"", "\"bad\"quote\""}) {
    CurlDownload download({"https://example.test/file"});
    CurlHandle handle;
    handle.download = &download;
    handle.lease = {0, 4096};
    handle.ranged = true;
    sendHeader(handle, "HTTP/1.1 206 Partial Content\r\n");
    sendHeader(handle, std::string("ETag: ") + tag + "\r\n");
    sendHeader(handle, "Last-Modified: Tue, 25 Aug 2026 00:00:00 GMT\r\n");
    sendHeader(handle, "Content-Range: bytes 0-4095/8192\r\n");
    sendHeader(handle, "\r\n");
    CHECK(handle.rangeAccepted);
    CHECK_EQ(tag == std::string("\"revision-one\"") ||
                 tag == std::string("\"\""),
             !download.impl_->etag.empty());
    CHECK(!download.impl_->lastModified.empty());
  }

  for (int code : {200, 206, 503}) {
    CurlDownload download({"https://example.test/file"});
    download.impl_->etag = "\"revision-one\"";
    CurlHandle handle;
    handle.download = &download;
    handle.lease = {0, 4096};
    handle.ranged = true;
    sendHeader(handle, "HTTP/1.1 " + std::to_string(code) + " Response\r\n");
    sendHeader(handle, "ETag: \"revision-two\"\r\n");
    if (code == 206) {
      sendHeader(handle, "Content-Range: bytes 0-4095/8192\r\n");
    }
    sendHeader(handle, "\r\n");
    CHECK_EQ(code != 503, handle.validatorMismatch);
    CHECK(!handle.rangeAccepted);
    CHECK(!handle.fullResponseAccepted);
    CHECK_EQ(std::string("\"revision-one\""), download.impl_->etag);
  }
}

void CurlSessionTest::testRangeOwnershipAndResponseBoundaries()
{
  struct Case {
    int64_t begin, end, responseEnd, total;
  };
  for (const auto& item :
       {Case{0, 4 * 1024 * 1024, 60651, 60651},
        Case{0, 4 * 1024 * 1024, 60651, 8 * 1024 * 1024},
        Case{1024, 2048, 1536, 4096}, Case{1024, 2048, 1536, 1536}}) {
    CurlDownload download({"https://example.test/file"});
    CurlHandle handle;
    handle.download = &download;
    handle.lease = {item.begin, item.end};
    handle.writeOffset = item.begin;
    handle.ranged = true;

    sendHeader(handle, "HTTP/1.1 206 Partial Content\r\n");
    sendHeader(handle, "Content-Range: bytes " + std::to_string(item.begin) +
                           "-" + std::to_string(item.responseEnd - 1) + "/" +
                           std::to_string(item.total) + "\r\n");
    sendHeader(handle, "\r\n");

    CHECK(handle.rangeAccepted);
    CHECK(!handle.invalidRange);
    CHECK_EQ(item.responseEnd, handle.responseRangeEnd);
    CHECK_EQ(std::min(item.end, item.total), handle.lease.end);
    CHECK_EQ(item.total, download.snapshot().totalLength);
    const auto remainder = handle.lease.remainder(item.responseEnd);
    CHECK_EQ(item.responseEnd, remainder.begin);
    CHECK_EQ(std::min(item.end, item.total), remainder.end);
  }
}

void CurlSessionTest::testNonzeroRangeRejectsCompleteResponse()
{
  CurlDownload download({"https://example.test/file"});
  CurlHandle handle;
  handle.download = &download;
  handle.lease = {1024, 2048};
  handle.writeOffset = 1024;
  handle.ranged = true;

  sendHeader(handle, "HTTP/1.1 200 OK\r\n");
  sendHeader(handle, "Content-Length: 4096\r\n");
  sendHeader(handle, "\r\n");
  char data[] = "data";

  CHECK(!handle.fullResponseAccepted);
  CHECK_EQ(CURL_WRITEFUNC_ERROR,
           CurlSession::writeData(data, 1, sizeof(data) - 1, &handle));
}

void CurlSessionTest::testUnsatisfiedRangeResponseForms()
{
  CurlDownload download({"https://example.test/file"});
  CurlHandle handle;
  handle.download = &download;

  sendHeader(handle, "HTTP/1.1 416 Range Not Satisfiable\r\n");
  sendHeader(handle, "Content-Range: bytes */4096\r\n");
  sendHeader(handle, "\r\n");
  CHECK_EQ(4096, handle.unsatisfiedTotalLength);
  CHECK(!handle.invalidRange);

  sendHeader(handle, "HTTP/1.1 416 Range Not Satisfiable\r\n");
  sendHeader(handle, "Content-Range: */8192\r\n");
  sendHeader(handle, "\r\n");
  CHECK_EQ(8192, handle.unsatisfiedTotalLength);
  CHECK(!handle.invalidRange);

  sendHeader(handle, "HTTP/1.1 416 Range Not Satisfiable\r\n");
  sendHeader(handle, "\r\n");
  CHECK_EQ(-1, handle.unsatisfiedTotalLength);
  CHECK(!handle.invalidRange);
}

void CurlSessionTest::testExistingFileDecision()
{
  CHECK_EQ(ExistingFileDecision::Complete,
           CurlSession::decideExistingFile(4096, 4096, false));
  CHECK_EQ(ExistingFileDecision::Resume,
           CurlSession::decideExistingFile(1024, 4096, true));
  CHECK_EQ(ExistingFileDecision::Reject,
           CurlSession::decideExistingFile(1024, 4096, false));
  CHECK_EQ(ExistingFileDecision::Reject,
           CurlSession::decideExistingFile(8192, 4096, true));
}

void CurlSessionTest::testOutcomeClassification()
{
  using O = TransferOutcome;
  const auto classify = [](CURLcode result, long code, bool validated,
                           bool connected = false, int notFound = 0,
                           int maxNotFound = 0) {
    return CurlSession::classifyOutcome(result, code, validated, notFound,
                                        maxNotFound, connected);
  };
  for (auto code :
       {CURLE_SSL_CONNECT_ERROR, CURLE_COULDNT_CONNECT, CURLE_RECV_ERROR,
        CURLE_PARTIAL_FILE, CURLE_OPERATION_TIMEDOUT}) {
    CHECK_EQ(O::Retryable, classify(code, 0, true));
  }
  for (auto code : {CURLE_PEER_FAILED_VERIFICATION, CURLE_SSL_CERTPROBLEM,
                    CURLE_SSL_CACERT_BADFILE}) {
    CHECK_EQ(O::Fatal, classify(code, 0, false));
  }
  CHECK_EQ(O::Fatal, classify(CURLE_SSH, 0, false));
  CHECK_EQ(O::Retryable, classify(CURLE_SSH, 0, false, true));
  for (auto code : {403, 408, 425, 500, 502, 504}) {
    CHECK_EQ(O::Retryable, classify(CURLE_HTTP_RETURNED_ERROR, code, true));
  }
  for (auto code : {429, 503}) {
    CHECK_EQ(O::Overloaded, classify(CURLE_HTTP_RETURNED_ERROR, code, false));
    CHECK_EQ(O::Overloaded, classify(CURLE_HTTP_RETURNED_ERROR, code, true));
  }
  CHECK_EQ(O::Fatal, classify(CURLE_HTTP_RETURNED_ERROR, 403, false));
  for (auto code : {400, 401, 404}) {
    CHECK_EQ(O::Fatal, classify(CURLE_HTTP_RETURNED_ERROR, code, true));
  }
  CHECK_EQ(O::Retryable,
           classify(CURLE_HTTP_RETURNED_ERROR, 404, true, false, 1, 3));
  CHECK_EQ(O::Fatal,
           classify(CURLE_HTTP_RETURNED_ERROR, 404, true, false, 3, 3));
}

void CurlSessionTest::testFailureMessageUsesTheFailureLayer()
{
  CurlHandle handle;
  const std::string detail = "native TLS failure";
  std::copy(detail.begin(), detail.end(), handle.errorBuffer.begin());

  CHECK_EQ(detail,
           CurlSession::failureMessage(handle, CURLE_SSL_CONNECT_ERROR, 302));
  CHECK_EQ(std::string("HTTP 503: ") + detail,
           CurlSession::failureMessage(handle, CURLE_HTTP_RETURNED_ERROR, 503));
}

} // namespace aria2
