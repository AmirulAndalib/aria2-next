#include "CurlSession.h"

#include <algorithm>

#include "CurlDownload.h"
#include "CurlDownloadImpl.h"
#include "DiskWriter.h"
#include "DownloadFailureException.h"
#include "a2doctest.h"
#include "a2functional.h"

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
  void testAcceptedRangeResponse();
  void testShortAcceptedRangeClampsLease();
  void testErrorResponseDoesNotChangeIdentity();
  void testNonzeroRangeRejectsCompleteResponse();
  void testUnsatisfiedRangeResponseForms();
  void testExistingFileDecision();
  void testPlatformSslOptions();
  void testRetryableFailureClassification();
  void testFailureMessageUsesTheFailureLayer();
  void sendHeader(CurlHandle& handle, const std::string& line);
};

A2_TEST(CurlSessionTest, testWriteErrorBoundary)
A2_TEST(CurlSessionTest, testAcceptedRangeResponse)
A2_TEST(CurlSessionTest, testShortAcceptedRangeClampsLease)
A2_TEST(CurlSessionTest, testErrorResponseDoesNotChangeIdentity)
A2_TEST(CurlSessionTest, testNonzeroRangeRejectsCompleteResponse)
A2_TEST(CurlSessionTest, testUnsatisfiedRangeResponseForms)
A2_TEST(CurlSessionTest, testExistingFileDecision)
A2_TEST(CurlSessionTest, testPlatformSslOptions)
A2_TEST(CurlSessionTest, testRetryableFailureClassification)
A2_TEST(CurlSessionTest, testFailureMessageUsesTheFailureLayer)

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

void CurlSessionTest::testAcceptedRangeResponse()
{
  CurlDownload download({"https://example.test/file"});
  CurlHandle handle;
  handle.download = &download;
  handle.lease = {1024, 2048};
  handle.writeOffset = 1024;
  handle.ranged = true;

  sendHeader(handle, "HTTP/1.1 206 Partial Content\r\n");
  sendHeader(handle, "ETag: \"revision-one\"\r\n");
  sendHeader(handle, "Last-Modified: Tue, 25 Aug 2026 00:00:00 GMT\r\n");
  sendHeader(handle, "Content-Range: bytes 1024-2047/4096\r\n");
  sendHeader(handle, "\r\n");

  CHECK(handle.rangeAccepted);
  CHECK(!handle.invalidRange);
  CHECK(!handle.validatorMismatch);
  CHECK_EQ(2048, handle.responseRangeEnd);
  CHECK_EQ(4096, download.snapshot().totalLength);
  CHECK_EQ(std::string("\"revision-one\""), download.impl_->etag);
  CHECK(download.impl_->rangeValidated);
}

void CurlSessionTest::testShortAcceptedRangeClampsLease()
{
  CurlDownload download({"https://example.test/file"});
  CurlHandle handle;
  handle.download = &download;
  handle.lease = {0, 4 * 1024 * 1024};
  handle.ranged = true;

  sendHeader(handle, "HTTP/1.1 206 Partial Content\r\n");
  sendHeader(handle, "Content-Range: bytes 0-60650/60651\r\n");
  sendHeader(handle, "\r\n");

  CHECK(handle.rangeAccepted);
  CHECK(!handle.invalidRange);
  CHECK_EQ(60651, handle.responseRangeEnd);
  CHECK_EQ(60651, handle.lease.end);
  CHECK_EQ(60651, download.snapshot().totalLength);
}

void CurlSessionTest::testErrorResponseDoesNotChangeIdentity()
{
  CurlDownload download({"https://example.test/file"});
  download.impl_->etag = "\"revision-one\"";
  CurlHandle handle;
  handle.download = &download;
  handle.lease = {1024, 2048};
  handle.ranged = true;

  sendHeader(handle, "HTTP/1.1 503 Service Unavailable\r\n");
  sendHeader(handle, "ETag: \"error-page\"\r\n");
  sendHeader(handle, "\r\n");

  CHECK(!handle.rangeAccepted);
  CHECK(!handle.validatorMismatch);
  CHECK_EQ(std::string("\"revision-one\""), download.impl_->etag);
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

void CurlSessionTest::testPlatformSslOptions()
{
#ifdef _WIN32
  CHECK_EQ(CURLSSLOPT_REVOKE_BEST_EFFORT, CurlSession::platformSslOptions());
#else
  CHECK_EQ(0L, CurlSession::platformSslOptions());
#endif
}

void CurlSessionTest::testRetryableFailureClassification()
{
  CHECK(CurlSession::retryableFailure(CURLE_SSL_CONNECT_ERROR, 0, 0, 0, false,
                                      false));
  CHECK(!CurlSession::retryableFailure(CURLE_PEER_FAILED_VERIFICATION, 0, 0, 0,
                                       false, true));
  CHECK(!CurlSession::retryableFailure(CURLE_SSL_CERTPROBLEM, 0, 0, 0, false,
                                       true));
  CHECK(!CurlSession::retryableFailure(CURLE_SSL_CACERT_BADFILE, 0, 0, 0,
                                       false, true));
  CHECK(CurlSession::retryableFailure(CURLE_HTTP_RETURNED_ERROR, 403, 0, 0,
                                      true, false));
  CHECK(!CurlSession::retryableFailure(CURLE_HTTP_RETURNED_ERROR, 403, 0, 0,
                                       false, false));
  CHECK(!CurlSession::retryableFailure(CURLE_SSH, 0, 0, 0, false, false));
  CHECK(CurlSession::retryableFailure(CURLE_SSH, 0, 0, 0, false, true));
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
