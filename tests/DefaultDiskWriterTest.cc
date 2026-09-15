#include "DefaultDiskWriter.h"
#include "a2doctest.h"

#include <array>

#include "a2functional.h"
#include "File.h"

namespace aria2 {

class DefaultDiskWriterTest {

private:
public:
  void setUp() {}

  void testSize();
  void testUtf8PathAndResume();
};

A2_TEST(DefaultDiskWriterTest, testSize)
A2_TEST(DefaultDiskWriterTest, testUtf8PathAndResume)

void DefaultDiskWriterTest::testSize()
{
  DefaultDiskWriter dw(A2_TEST_DIR "/4096chunk.txt");
  dw.enableReadOnly();
  dw.openExistingFile();
  REQUIRE_EQ((int64_t)4_k, dw.size());
}

void DefaultDiskWriterTest::testUtf8PathAndResume()
{
  const std::string path = A2_TEST_OUT_DIR "/下载目录/结果文件.bin";
  File(path).remove();
  {
    DefaultDiskWriter writer(path);
    writer.initAndOpenFile();
    writer.writeData(reinterpret_cast<const unsigned char*>("tail"), 4, 4);
    writer.writeData(reinterpret_cast<const unsigned char*>("head"), 4, 0);
    writer.truncate(8);
  }
  {
    DefaultDiskWriter writer(path);
    writer.openExistingFile();
    writer.writeData(reinterpret_cast<const unsigned char*>("++"), 2, 2);
    std::array<unsigned char, 8> data{};
    REQUIRE_EQ(static_cast<ssize_t>(data.size()),
               writer.readData(data.data(), data.size(), 0));
    CHECK_EQ(
        std::string("he++tail"),
        std::string(reinterpret_cast<const char*>(data.data()), data.size()));
  }
  File(path).remove();
}

} // namespace aria2
