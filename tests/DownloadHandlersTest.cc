#include "GroupId.h"
#include <memory>
#include "download_handlers.h"

#include "a2doctest.h"

#include "RequestGroup.h"
#include "Option.h"
#include "DownloadContext.h"
#include "MemoryBufferPreDownloadHandler.h"
#include "FileEntry.h"
#include "RequestGroupCriteria.h"

namespace aria2 {

class DownloadHandlersTest {
protected:
  std::shared_ptr<Option> option_;

public:
  DownloadHandlersTest() { option_ = std::make_shared<Option>(); }
};

TEST_CASE_FIXTURE(DownloadHandlersTest,
                  "DownloadHandlersTest.testGetMemoryPreDownloadHandler")
{
  REQUIRE(download_handlers::getMemoryPreDownloadHandler()->canHandle(nullptr));
}

#ifdef ENABLE_METALINK

TEST_CASE_FIXTURE(
    DownloadHandlersTest,
    "DownloadHandlersTest.testGetMetalinkPreDownloadHandler_extension")
{
  auto dctx = std::make_shared<DownloadContext>(0, 0, "test.metalink");
  RequestGroup rg(GroupId::create(), option_);
  rg.setDownloadContext(dctx);

  auto handler = download_handlers::getMetalinkPreDownloadHandler();

  REQUIRE(handler->canHandle(&rg));

  dctx->getFirstFileEntry()->setPath("test.metalink2");
  REQUIRE(!handler->canHandle(&rg));
}

TEST_CASE_FIXTURE(
    DownloadHandlersTest,
    "DownloadHandlersTest.testGetMetalinkPreDownloadHandler_contentType")
{
  auto dctx = std::make_shared<DownloadContext>(0, 0, "test");
  dctx->getFirstFileEntry()->setContentType("application/metalink+xml");
  RequestGroup rg(GroupId::create(), option_);
  rg.setDownloadContext(dctx);

  auto handler = download_handlers::getMetalinkPreDownloadHandler();

  REQUIRE(handler->canHandle(&rg));

  dctx->getFirstFileEntry()->setContentType("application/octet-stream");
  REQUIRE(!handler->canHandle(&rg));
}

#endif // ENABLE_METALINK

#ifdef ENABLE_BITTORRENT

TEST_CASE_FIXTURE(DownloadHandlersTest,
                  "DownloadHandlersTest.testGetBtPreDownloadHandler_extension")
{
  auto dctx =
      std::make_shared<DownloadContext>(0, 0, A2_TEST_DIR "/test.torrent");
  RequestGroup rg(GroupId::create(), option_);
  rg.setDownloadContext(dctx);

  auto handler = download_handlers::getBtPreDownloadHandler();

  REQUIRE(handler->canHandle(&rg));

  dctx->getFirstFileEntry()->setPath(A2_TEST_DIR "/test.torrent2");
  REQUIRE(!handler->canHandle(&rg));
}

TEST_CASE_FIXTURE(
    DownloadHandlersTest,
    "DownloadHandlersTest.testGetBtPreDownloadHandler_contentType")
{
  auto dctx = std::make_shared<DownloadContext>(0, 0, "test");
  dctx->getFirstFileEntry()->setContentType("application/x-bittorrent");
  RequestGroup rg(GroupId::create(), option_);
  rg.setDownloadContext(dctx);

  auto handler = download_handlers::getBtPreDownloadHandler();

  REQUIRE(handler->canHandle(&rg));

  dctx->getFirstFileEntry()->setContentType("application/octet-stream");
  REQUIRE(!handler->canHandle(&rg));
}

#endif // ENABLE_BITTORRENT

} // namespace aria2
