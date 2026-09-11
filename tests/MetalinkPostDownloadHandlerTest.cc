#include "DiskAdaptor.h"
#include "FileEntry.h"
#include "PieceStorage.h"
#include "GroupId.h"
#include "a2functional.h"
#include <cstddef>
#include <memory>
#include <vector>
#include "MetalinkPostDownloadHandler.h"

#include "a2doctest.h"

#include "RequestGroup.h"
#include "Option.h"
#include "DownloadContext.h"
#include "RequestGroupCriteria.h"

namespace aria2 {

class MetalinkPostDownloadHandlerTest {
protected:
  std::shared_ptr<Option> option_;

public:
  MetalinkPostDownloadHandlerTest() { option_.reset(new Option()); }
};

TEST_CASE_FIXTURE(MetalinkPostDownloadHandlerTest,
                  "MetalinkPostDownloadHandlerTest.testCanHandle_extension")
{
  std::shared_ptr<DownloadContext> dctx(
      new DownloadContext(0, 0, "test.metalink"));
  RequestGroup rg(GroupId::create(), option_);
  rg.setDownloadContext(dctx);

  MetalinkPostDownloadHandler handler;

  REQUIRE(handler.canHandle(&rg));

  dctx->getFirstFileEntry()->setPath("test.metalink2");
  REQUIRE(!handler.canHandle(&rg));
}

TEST_CASE_FIXTURE(MetalinkPostDownloadHandlerTest,
                  "MetalinkPostDownloadHandlerTest.testCanHandle_contentType")
{
  std::shared_ptr<DownloadContext> dctx(new DownloadContext(0, 0, "test"));
  dctx->getFirstFileEntry()->setContentType("application/metalink+xml");
  RequestGroup rg(GroupId::create(), option_);
  rg.setDownloadContext(dctx);

  MetalinkPostDownloadHandler handler;

  REQUIRE(handler.canHandle(&rg));

  dctx->getFirstFileEntry()->setContentType("application/octet-stream");
  REQUIRE(!handler.canHandle(&rg));
}

TEST_CASE_FIXTURE(MetalinkPostDownloadHandlerTest,
                  "MetalinkPostDownloadHandlerTest.testGetNextRequestGroups")
{
  std::shared_ptr<DownloadContext> dctx(
      new DownloadContext(1_k, 0, A2_TEST_DIR "/test.xml"));
  RequestGroup rg(GroupId::create(), option_);
  rg.setDownloadContext(dctx);
  rg.initPieceStorage();
  rg.getPieceStorage()->getDiskAdaptor()->enableReadOnly();

  MetalinkPostDownloadHandler handler;
  std::vector<std::shared_ptr<RequestGroup>> groups;
  handler.getNextRequestGroups(groups, &rg);
  REQUIRE_EQ((size_t)5, groups.size());

  for (auto& nrg : groups) {
    REQUIRE_EQ(rg.getGID(), nrg->following());
  }
}

TEST_CASE_FIXTURE(
    MetalinkPostDownloadHandlerTest,
    "MetalinkPostDownloadHandlerTest.testGetNextRequestGroups_withBaseUri")
{
  std::shared_ptr<DownloadContext> dctx(
      new DownloadContext(1_k, 0, A2_TEST_DIR "/base_uri.xml"));
  dctx->getFirstFileEntry()->addUri("http://base/dir/base_uri.xml");
  RequestGroup rg(GroupId::create(), option_);
  rg.setDownloadContext(dctx);
  rg.initPieceStorage();
  rg.getPieceStorage()->getDiskAdaptor()->enableReadOnly();

  MetalinkPostDownloadHandler handler;
  std::vector<std::shared_ptr<RequestGroup>> groups;
  handler.getNextRequestGroups(groups, &rg);
  REQUIRE_EQ((size_t)1, groups.size());
  REQUIRE_EQ(std::string("http://base/dir/example.ext"),
             groups[0]
                 ->getDownloadContext()
                 ->getFirstFileEntry()
                 ->getRemainingUris()[0]);
}

} // namespace aria2
