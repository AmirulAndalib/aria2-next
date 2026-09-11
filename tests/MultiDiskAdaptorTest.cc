#include "TimeA2.h"
#include "a2functional.h"
#include <algorithm>
#include <cstdint>
#include <ctime>
#include <memory>
#include <vector>
#include "MultiDiskAdaptor.h"

#include <string>
#include <cerrno>
#include <cstring>

#include "a2doctest.h"

#include "FileEntry.h"
#include "TestUtil.h"
#include "WrDiskCacheEntry.h"

namespace aria2 {

class MultiDiskAdaptorTest {
protected:
  std::unique_ptr<MultiDiskAdaptor> adaptor;

public:
  MultiDiskAdaptorTest()
  {
    adaptor = make_unique<MultiDiskAdaptor>();
    adaptor->setPieceLength(2);
  }
};

std::vector<std::shared_ptr<FileEntry>> createEntries()
{
  std::vector<std::shared_ptr<FileEntry>> entries{
      std::make_shared<FileEntry>(A2_TEST_OUT_DIR "/file0.txt", 0, 0),
      std::make_shared<FileEntry>(A2_TEST_OUT_DIR "/file1.txt", 15, 0),
      std::make_shared<FileEntry>(A2_TEST_OUT_DIR "/file2.txt", 7, 15),
      std::make_shared<FileEntry>(A2_TEST_OUT_DIR "/file3.txt", 0, 22),
      std::make_shared<FileEntry>(A2_TEST_OUT_DIR "/file4.txt", 2, 22),
      std::make_shared<FileEntry>(A2_TEST_OUT_DIR "/file5.txt", 0, 24),
      std::make_shared<FileEntry>(A2_TEST_OUT_DIR "/file6.txt", 3, 24),
      std::make_shared<FileEntry>(A2_TEST_OUT_DIR "/file7.txt", 0, 27),
      std::make_shared<FileEntry>(A2_TEST_OUT_DIR "/file8.txt", 2, 27),
  };
  //           1    1    2    2    3
  // 0....5....0....5....0....5....0
  // ++--++--++--++--++--++--++--++--
  // | file0
  // *************** file1
  //                ******* file2
  //                       | file3
  //                       ** flie4
  //                         | file5
  //                         *** file6
  //                            |file7
  //                            ** file8
  for (const auto& i : entries) {
    File(i->getPath()).remove();
  }
  return entries;
}

TEST_CASE_FIXTURE(MultiDiskAdaptorTest,
                  "MultiDiskAdaptorTest.testResetDiskWriterEntries")
{
  {
    auto fileEntries = createEntries();
    adaptor->setFileEntries(fileEntries.begin(), fileEntries.end());
    // In openFile(), resetDiskWriterEntries() are called.
    adaptor->openFile();

    auto& entries = adaptor->getDiskWriterEntries();
    REQUIRE(entries[0]->getDiskWriter());
    REQUIRE(entries[1]->getDiskWriter());
    REQUIRE(entries[2]->getDiskWriter());
    REQUIRE(entries[3]->getDiskWriter());
    REQUIRE(entries[4]->getDiskWriter());
    REQUIRE(entries[5]->getDiskWriter());

    adaptor->closeFile();
  }
  {
    auto fileEntries = createEntries();
    fileEntries[0]->setRequested(false);
    adaptor->setFileEntries(fileEntries.begin(), fileEntries.end());
    // In openFile(), resetDiskWriterEntries() are called.
    adaptor->openFile();

    auto& entries = adaptor->getDiskWriterEntries();
    // Because entries[1] spans entries[0]
    REQUIRE(entries[0]->getDiskWriter());
    REQUIRE(entries[1]->getDiskWriter());
    REQUIRE(entries[2]->getDiskWriter());
    REQUIRE(entries[3]->getDiskWriter());
    REQUIRE(entries[4]->getDiskWriter());
    REQUIRE(entries[5]->getDiskWriter());

    adaptor->closeFile();
  }
  {
    auto fileEntries = createEntries();
    fileEntries[0]->setRequested(false);
    fileEntries[1]->setRequested(false);
    adaptor->setFileEntries(fileEntries.begin(), fileEntries.end());
    // In openFile(), resetDiskWriterEntries() are called.
    adaptor->openFile();

    auto& entries = adaptor->getDiskWriterEntries();
    REQUIRE(!entries[0]->getDiskWriter());
    // Because entries[2] spans entries[1]
    REQUIRE(entries[1]->getDiskWriter());
    REQUIRE(entries[1]->needsFileAllocation());
    REQUIRE(entries[2]->getDiskWriter());
    REQUIRE(entries[3]->getDiskWriter());
    REQUIRE(entries[4]->getDiskWriter());
    REQUIRE(entries[5]->getDiskWriter());

    adaptor->closeFile();
  }
  {
    auto fileEntries = createEntries();
    fileEntries[3]->setRequested(false);
    adaptor->setFileEntries(fileEntries.begin(), fileEntries.end());
    // In openFile(), resetDiskWriterEntries() are called.
    adaptor->openFile();

    auto& entries = adaptor->getDiskWriterEntries();
    REQUIRE(entries[0]->getDiskWriter());
    REQUIRE(entries[1]->getDiskWriter());
    REQUIRE(entries[2]->getDiskWriter());
    // Because entries[4] spans entries[3]
    REQUIRE(entries[3]->getDiskWriter());
    REQUIRE(entries[3]->needsFileAllocation());
    REQUIRE(entries[4]->getDiskWriter());
    REQUIRE(entries[5]->getDiskWriter());

    adaptor->closeFile();
  }
  {
    auto fileEntries = createEntries();
    fileEntries[4]->setRequested(false);
    adaptor->setFileEntries(fileEntries.begin(), fileEntries.end());
    // In openFile(), resetDiskWriterEntries() are called.
    adaptor->openFile();

    auto& entries = adaptor->getDiskWriterEntries();
    REQUIRE(entries[0]->getDiskWriter());
    REQUIRE(entries[1]->getDiskWriter());
    REQUIRE(entries[2]->getDiskWriter());
    REQUIRE(entries[3]->getDiskWriter());
    // entries[3] is 0 length. No overrap with entries[4]
    REQUIRE(!entries[4]->getDiskWriter());
    REQUIRE(entries[5]->getDiskWriter());

    adaptor->closeFile();
  }
  {
    auto fileEntries = createEntries();
    fileEntries[3]->setRequested(false);
    fileEntries[4]->setRequested(false);
    adaptor->setFileEntries(fileEntries.begin(), fileEntries.end());
    // In openFile(), resetDiskWriterEntries() are called.
    adaptor->openFile();

    auto& entries = adaptor->getDiskWriterEntries();
    REQUIRE(entries[0]->getDiskWriter());
    REQUIRE(entries[1]->getDiskWriter());
    REQUIRE(entries[2]->getDiskWriter());
    REQUIRE(!entries[3]->getDiskWriter());
    REQUIRE(!entries[4]->getDiskWriter());
    REQUIRE(entries[5]->getDiskWriter());

    adaptor->closeFile();
  }
  {
    auto fileEntries = createEntries();
    for (size_t i = 5; i < 9; ++i) {
      fileEntries[i]->setRequested(false);
    }
    adaptor->setFileEntries(fileEntries.begin(), fileEntries.end());
    // In openFile(), resetDiskWriterEntries() are called.
    adaptor->openFile();

    auto& entries = adaptor->getDiskWriterEntries();
    REQUIRE(entries[0]->getDiskWriter());
    REQUIRE(entries[1]->getDiskWriter());
    REQUIRE(entries[2]->getDiskWriter());
    REQUIRE(entries[3]->getDiskWriter());
    REQUIRE(entries[4]->getDiskWriter());
    REQUIRE(!entries[5]->getDiskWriter());

    adaptor->closeFile();
  }
  {
    auto fileEntries = createEntries();
    for (size_t i = 1; i < 9; ++i) {
      fileEntries[i]->setRequested(false);
    }
    adaptor->setFileEntries(fileEntries.begin(), fileEntries.end());
    adaptor->openFile();
    auto& entries = adaptor->getDiskWriterEntries();
    REQUIRE(entries[0]->getDiskWriter());
    REQUIRE(!entries[1]->getDiskWriter());
    REQUIRE(!entries[2]->getDiskWriter());
    REQUIRE(!entries[3]->getDiskWriter());
    REQUIRE(!entries[4]->getDiskWriter());
    REQUIRE(!entries[5]->getDiskWriter());

    adaptor->closeFile();
  }
  {
    auto fileEntries = createEntries();
    for (size_t i = 2; i < 9; ++i) {
      fileEntries[i]->setRequested(false);
    }
    adaptor->setFileEntries(fileEntries.begin(), fileEntries.end());
    adaptor->openFile();
    auto& entries = adaptor->getDiskWriterEntries();
    REQUIRE(entries[0]->getDiskWriter());
    REQUIRE(entries[1]->getDiskWriter());
    // entries[1] spans entries[2]
    REQUIRE(entries[2]->getDiskWriter());
    REQUIRE(!entries[2]->needsFileAllocation());
    REQUIRE(!entries[3]->getDiskWriter());
    REQUIRE(!entries[4]->getDiskWriter());
    REQUIRE(!entries[5]->getDiskWriter());

    adaptor->closeFile();
  }
  {
    auto fileEntries = createEntries();
    for (size_t i = 0; i < 6; ++i) {
      fileEntries[i]->setRequested(false);
    }
    fileEntries[8]->setRequested(false);
    adaptor->setFileEntries(fileEntries.begin(), fileEntries.end());
    adaptor->openFile();
    auto& entries = adaptor->getDiskWriterEntries();
    REQUIRE(!entries[0]->getDiskWriter());
    REQUIRE(!entries[1]->getDiskWriter());
    REQUIRE(!entries[2]->getDiskWriter());
    REQUIRE(!entries[3]->getDiskWriter());
    REQUIRE(!entries[4]->getDiskWriter());
    // entries[6] spans entries[5] in the current implementation.
    REQUIRE(entries[5]->getDiskWriter());
    REQUIRE(entries[6]->getDiskWriter());
    REQUIRE(entries[7]->getDiskWriter());
    // entries[6] spans entries[8]
    REQUIRE(entries[8]->getDiskWriter());
    adaptor->closeFile();
  }
}

namespace {
void readPrefix(const std::string& filename, char* buffer, int length)
{
  const auto content = aria2::readFile(filename);
  REQUIRE(content.size() >= static_cast<size_t>(length));
  std::copy_n(content.data(), length, buffer);
}
} // namespace

TEST_CASE_FIXTURE(MultiDiskAdaptorTest, "MultiDiskAdaptorTest.testWriteData")
{
  auto fileEntries = createEntries();
  adaptor->setFileEntries(std::begin(fileEntries), std::end(fileEntries));

  adaptor->openFile();
  std::string msg = "12345";
  adaptor->writeData((const unsigned char*)msg.c_str(), msg.size(), 0);
  adaptor->closeFile();

  REQUIRE(File(A2_TEST_OUT_DIR "/file0.txt").isFile());
  char buf[128];
  readPrefix(A2_TEST_OUT_DIR "/file1.txt", buf, 5);
  buf[5] = '\0';
  REQUIRE_EQ(msg, std::string(buf));

  adaptor->openFile();
  std::string msg2 = "67890ABCDEF";
  adaptor->writeData((const unsigned char*)msg2.c_str(), msg2.size(), 5);
  adaptor->closeFile();

  readPrefix(A2_TEST_OUT_DIR "/file1.txt", buf, 15);
  buf[15] = '\0';
  REQUIRE_EQ(std::string("1234567890ABCDE"), std::string(buf));
  readPrefix(A2_TEST_OUT_DIR "/file2.txt", buf, 1);
  buf[1] = '\0';
  REQUIRE_EQ(std::string("F"), std::string(buf));

  adaptor->openFile();
  std::string msg3 = "12345123456712";
  adaptor->writeData((const unsigned char*)msg3.c_str(), msg3.size(), 10);
  adaptor->closeFile();

  readPrefix(A2_TEST_OUT_DIR "/file1.txt", buf, 15);
  buf[15] = '\0';
  REQUIRE_EQ(std::string("123456789012345"), std::string(buf));
  readPrefix(A2_TEST_OUT_DIR "/file2.txt", buf, 7);
  buf[7] = '\0';
  REQUIRE_EQ(std::string("1234567"), std::string(buf));

  REQUIRE(File(A2_TEST_OUT_DIR "/file3.txt").isFile());

  readPrefix(A2_TEST_OUT_DIR "/file4.txt", buf, 2);
  buf[2] = '\0';
  REQUIRE_EQ(std::string("12"), std::string(buf));

  REQUIRE(File(A2_TEST_OUT_DIR "/file5.txt").isFile());
}

TEST_CASE_FIXTURE(MultiDiskAdaptorTest, "MultiDiskAdaptorTest.testReadData")
{
  auto entries = std::vector<std::shared_ptr<FileEntry>>{
      std::make_shared<FileEntry>(A2_TEST_DIR "/file1r.txt", 15, 0),
      std::make_shared<FileEntry>(A2_TEST_DIR "/file2r.txt", 7, 15),
      std::make_shared<FileEntry>(A2_TEST_DIR "/file3r.txt", 3, 22)};

  adaptor->setFileEntries(std::begin(entries), std::end(entries));
  adaptor->enableReadOnly();
  adaptor->openFile();
  unsigned char buf[128];
  adaptor->readData(buf, 15, 0);
  buf[15] = '\0';
  REQUIRE_EQ(std::string("1234567890ABCDE"), std::string((char*)buf));
  adaptor->readData(buf, 10, 6);
  buf[10] = '\0';
  REQUIRE_EQ(std::string("7890ABCDEF"), std::string((char*)buf));
  adaptor->readData(buf, 4, 20);
  buf[4] = '\0';
  REQUIRE_EQ(std::string("KLMN"), std::string((char*)buf));
  adaptor->readData(buf, 25, 0);
  buf[25] = '\0';
  REQUIRE_EQ(std::string("1234567890ABCDEFGHIJKLMNO"), std::string((char*)buf));
}

TEST_CASE_FIXTURE(MultiDiskAdaptorTest,
                  "MultiDiskAdaptorTest.testCutTrailingGarbage")
{
  std::string dir = A2_TEST_OUT_DIR;
  std::string prefix = "aria2_MultiDiskAdaptorTest_testCutTrailingGarbage_";
  auto fileEntries = std::vector<std::shared_ptr<FileEntry>>{
      std::make_shared<FileEntry>(dir + "/" + prefix + "1", 256, 0),
      std::make_shared<FileEntry>(dir + "/" + prefix + "2", 512, 256)};
  for (const auto& i : fileEntries) {
    createFile(i->getPath(), i->getLength() + 100);
  }

  MultiDiskAdaptor adaptor;
  adaptor.setFileEntries(std::begin(fileEntries), std::end(fileEntries));
  adaptor.setPieceLength(1);

  adaptor.openFile();

  adaptor.cutTrailingGarbage();

  REQUIRE_EQ((int64_t)256, File(fileEntries[0]->getPath()).size());
  REQUIRE_EQ((int64_t)512, File(fileEntries[1]->getPath()).size());
}

TEST_CASE_FIXTURE(MultiDiskAdaptorTest, "MultiDiskAdaptorTest.testSize")
{
  std::string dir = A2_TEST_OUT_DIR;
  std::string prefix = "aria2_MultiDiskAdaptorTest_testSize_";
  auto fileEntries = std::vector<std::shared_ptr<FileEntry>>{
      std::make_shared<FileEntry>(dir + "/" + prefix + "1", 1, 0),
      std::make_shared<FileEntry>(dir + "/" + prefix + "2", 1, 1)};
  for (const auto& i : fileEntries) {
    createFile(i->getPath(), i->getLength());
  }

  MultiDiskAdaptor adaptor;
  adaptor.setFileEntries(std::begin(fileEntries), std::end(fileEntries));
  adaptor.setPieceLength(1);

  adaptor.openFile();

  REQUIRE_EQ((int64_t)2, adaptor.size());
}

TEST_CASE_FIXTURE(MultiDiskAdaptorTest, "MultiDiskAdaptorTest.testUtime")
{
  std::string storeDir =
      A2_TEST_OUT_DIR "/aria2_MultiDiskAdaptorTest_testUtime";
  auto entries = std::vector<std::shared_ptr<FileEntry>>{
      std::make_shared<FileEntry>(storeDir + "/requested", 0, 0),
      std::make_shared<FileEntry>(storeDir + "/notFound", 0, 0),
      std::make_shared<FileEntry>(storeDir + "/notRequested", 0, 0),
      std::make_shared<FileEntry>(storeDir + "/anotherRequested", 0, 0),
  };

  createFile(entries[0]->getPath(), entries[0]->getLength());
  File(entries[1]->getPath()).remove();
  createFile(entries[2]->getPath(), entries[2]->getLength());
  createFile(entries[3]->getPath(), entries[3]->getLength());

  entries[2]->setRequested(false);

  MultiDiskAdaptor adaptor;
  adaptor.setFileEntries(std::begin(entries), std::end(entries));

  time_t atime = (time_t)100000;
  time_t mtime = (time_t)200000;

  REQUIRE_EQ((size_t)2, adaptor.utime(Time(atime), Time(mtime)));

  REQUIRE_EQ((time_t)mtime,
             File(entries[0]->getPath()).getModifiedTime().getTimeFromEpoch());

  REQUIRE_EQ((time_t)mtime,
             File(entries[3]->getPath()).getModifiedTime().getTimeFromEpoch());

  REQUIRE((time_t)mtime !=
          File(entries[2]->getPath()).getModifiedTime().getTimeFromEpoch());
}

TEST_CASE_FIXTURE(MultiDiskAdaptorTest, "MultiDiskAdaptorTest.testWriteCache")
{
  std::string storeDir =
      A2_TEST_OUT_DIR "/aria2_MultiDiskAdaptorTest_testWriteCache";
  auto entries = std::vector<std::shared_ptr<FileEntry>>{
      std::make_shared<FileEntry>(storeDir + "/file1", 16385, 0),
      std::make_shared<FileEntry>(storeDir + "/file2", 4098, 16385)};
  for (const auto& i : entries) {
    File(i->getPath()).remove();
  }
  auto adaptor = std::make_shared<MultiDiskAdaptor>();
  adaptor->setFileEntries(std::begin(entries), std::end(entries));
  WrDiskCacheEntry cache{adaptor};
  std::string data1(16383, '1'), data2(100, '2'), data3(4000, '3');
  cache.cacheData(createDataCell(0, data1.c_str()));
  cache.cacheData(createDataCell(data1.size(), data2.c_str()));
  cache.cacheData(createDataCell(data1.size() + data2.size(), data3.c_str()));
  adaptor->openFile();
  adaptor->writeCache(&cache);
  for (int i = 0; i < 2; ++i) {
    REQUIRE_EQ(entries[i]->getLength(), File(entries[i]->getPath()).size());
  }
  REQUIRE_EQ(data1 + data2.substr(0, 2), readFile(entries[0]->getPath()));
  REQUIRE_EQ(data2.substr(2) + data3, readFile(entries[1]->getPath()));

  adaptor->closeFile();
  for (int i = 0; i < 2; ++i) {
    File(entries[i]->getPath()).remove();
  }
  cache.clear();
  cache.cacheData(createDataCell(123, data2.c_str()));
  adaptor->openFile();
  adaptor->writeCache(&cache);
  REQUIRE_EQ((int64_t)(123 + data2.size()), File(entries[0]->getPath()).size());
  REQUIRE_EQ(data2, readFile(entries[0]->getPath()).substr(123));
}

} // namespace aria2
