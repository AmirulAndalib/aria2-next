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
};

A2_TEST(CurlSessionTest, testWriteErrorBoundary)

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

} // namespace aria2
