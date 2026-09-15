#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include "GZipDecodingStreamFilter.h"

#include <cassert>
#include <iostream>
#include <fstream>

#include "a2doctest.h"

#include "support/Encoding.h"
#include "a2functional.h"
#include "ByteArrayDiskWriter.h"
#include "SinkStreamFilter.h"
#include "MockSegment.h"
#include "MessageDigest.h"

namespace aria2 {

class GZipDecodingStreamFilterTest {
protected:
  class MockSegment2 : public MockSegment {
  protected:
    int64_t positionToWrite_;

  public:
    MockSegment2() : positionToWrite_(0) {}

    virtual void updateWrittenLength(int64_t bytes) override
    {
      positionToWrite_ += bytes;
    }

    virtual int64_t getPositionToWrite() const override
    {
      return positionToWrite_;
    }
  };

  std::unique_ptr<GZipDecodingStreamFilter> filter_;
  std::shared_ptr<ByteArrayDiskWriter> writer_;
  std::shared_ptr<MockSegment2> segment_;

public:
  GZipDecodingStreamFilterTest()
  {
    writer_ = std::make_shared<ByteArrayDiskWriter>();
    auto sinkFilter = make_unique<SinkStreamFilter>();
    sinkFilter->init();
    filter_ = make_unique<GZipDecodingStreamFilter>(std::move(sinkFilter));
    filter_->init();
    segment_ = std::make_shared<MockSegment2>();
  }
};

TEST_CASE_FIXTURE(GZipDecodingStreamFilterTest,
                  "GZipDecodingStreamFilterTest.testTransform")
{
  unsigned char buf[4_k];
  std::ifstream in(A2_TEST_DIR "/gzip_decode_test.gz", std::ios::binary);
  while (in) {
    in.read(reinterpret_cast<char*>(buf), sizeof(buf));
    filter_->transform(writer_, segment_, buf, in.gcount());
  }
  REQUIRE(filter_->finished());
  std::string data = writer_->getString();
  std::shared_ptr<MessageDigest> sha1(MessageDigest::sha1());
  sha1->update(data.data(), data.size());
  REQUIRE_EQ(std::string("8b577b33c0411b2be9d4fa74c7402d54a8d21f96"),
             util::toHex(sha1->digest()));
}

} // namespace aria2
