#include "ShareRatioSeedCriteria.h"

#include "a2doctest.h"

#include "DownloadContext.h"
#include "MockPieceStorage.h"
#include "FileEntry.h"

namespace aria2 {

class ShareRatioSeedCriteriaTest {


public:
  void testEvaluate();
};

A2_TEST(ShareRatioSeedCriteriaTest, testEvaluate)

void ShareRatioSeedCriteriaTest::testEvaluate()
{
  std::shared_ptr<DownloadContext> dctx(new DownloadContext(1_m, 1000000));
  dctx->getNetStat().updateUpload(1000000);

  std::shared_ptr<MockPieceStorage> pieceStorage(new MockPieceStorage());
  pieceStorage->setCompletedLength(1000000);

  ShareRatioSeedCriteria cri(1.0, dctx);
  cri.setPieceStorage(pieceStorage);

  REQUIRE(cri.evaluate());

  cri.setRatio(2.0);
  REQUIRE(!cri.evaluate());
  // check div by zero
  dctx->getFirstFileEntry()->setLength(0);
  REQUIRE(!cri.evaluate());
}

} // namespace aria2
