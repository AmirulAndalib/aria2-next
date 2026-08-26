#include "CurlSession.h"

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
  void testErrorResponseDoesNotChangeIdentity();
  void testNonzeroRangeRejectsCompleteResponse();
  void sendHeader(CurlHandle& handle, const std::string& line);
};

A2_TEST(CurlSessionTest, testWriteErrorBoundary)
A2_TEST(CurlSessionTest, testAcceptedRangeResponse)
A2_TEST(CurlSessionTest, testErrorResponseDoesNotChangeIdentity)
A2_TEST(CurlSessionTest, testNonzeroRangeRejectsCompleteResponse)

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

} // namespace aria2
