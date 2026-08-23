#include "Log.h"

#include <sstream>

#include "a2doctest.h"

#include "BufferedFile.h"
#include "File.h"

namespace aria2 {

class LogTest {

public:
  void setUp();
  void tearDown();

  void testRotationKeepsStrictBounds();
  void testStartupEnforcesNativeBounds();
  void testOversizedRecordIsBounded();
  void testSanitizersProtectLogIntegrity();
  void testSourceLocationIsPortable();
  void testLevelFilteringAndReconfiguration();

private:
  std::string path_;
  logging::Settings originalSettings_;

  logging::Settings settings(size_t maxSize, size_t maxFiles) const;
  void removeLogs();
  void writeFile(const std::string& path, size_t size);
  std::string readFile(const std::string& path);
};

A2_TEST(LogTest, testRotationKeepsStrictBounds)
A2_TEST(LogTest, testStartupEnforcesNativeBounds)
A2_TEST(LogTest, testOversizedRecordIsBounded)
A2_TEST(LogTest, testSanitizersProtectLogIntegrity)
A2_TEST(LogTest, testSourceLocationIsPortable)
A2_TEST(LogTest, testLevelFilteringAndReconfiguration)

void LogTest::setUp()
{
  path_ = A2_TEST_OUT_DIR "/aria2_LogTest.log";
  originalSettings_ = logging::getSettings();
  logging::shutdown();
  removeLogs();
}

void LogTest::tearDown()
{
  logging::shutdown();
  removeLogs();
  logging::configure(originalSettings_);
}

logging::Settings LogTest::settings(size_t maxSize, size_t maxFiles) const
{
  logging::Settings settings;
  settings.file = path_;
  settings.maxFileSize = maxSize;
  settings.maxFiles = maxFiles;
  settings.fileLevel = spdlog::level::trace;
  settings.consoleOutput = false;
  return settings;
}

void LogTest::removeLogs()
{
  File(path_).remove();
  for (size_t i = 1; i <= logging::MAX_FILES; ++i) {
    File(path_ + "." + std::to_string(i)).remove();
    File(A2_TEST_OUT_DIR "/aria2_LogTest." + std::to_string(i) + ".log")
        .remove();
  }
}

void LogTest::writeFile(const std::string& path, size_t size)
{
  BufferedFile file(path.c_str(), BufferedFile::WRITE);
  REQUIRE(file);
  const std::string data(size, 'x');
  REQUIRE_EQ(size, file.write(data.data(), data.size()));
}

std::string LogTest::readFile(const std::string& path)
{
  BufferedFile file(path.c_str(), BufferedFile::READ);
  REQUIRE(file);
  std::stringstream output;
  file.transfer(output);
  return output.str();
}

void LogTest::testRotationKeepsStrictBounds()
{
  logging::configure(settings(128, 2));
  for (size_t i = 0; i < 12; ++i) {
    A2_LOG_TRACE(std::string(48, static_cast<char>('a' + i)));
  }
  logging::flush();

  const std::string history = A2_TEST_OUT_DIR "/aria2_LogTest.1.log";
  REQUIRE(File(path_).exists());
  REQUIRE(File(history).exists());
  REQUIRE(!File(A2_TEST_OUT_DIR "/aria2_LogTest.2.log").exists());
  REQUIRE(File(path_).size() <= 128);
  REQUIRE(File(history).size() <= 128);
  REQUIRE(File(path_).size() + File(history).size() <= 256);
}

void LogTest::testStartupEnforcesNativeBounds()
{
  const std::string nativeHistory =
      A2_TEST_OUT_DIR "/aria2_LogTest.1.log";
  writeFile(path_, 256);
  writeFile(nativeHistory, 256);
  writeFile(A2_TEST_OUT_DIR "/aria2_LogTest.2.log", 8);

  logging::configure(settings(64, 2));
  logging::flush();

  REQUIRE(File(path_).exists());
  REQUIRE_EQ((int64_t)0, File(path_).size());
  REQUIRE(!File(nativeHistory).exists());
  REQUIRE(!File(A2_TEST_OUT_DIR "/aria2_LogTest.2.log").exists());
}

void LogTest::testOversizedRecordIsBounded()
{
  logging::configure(settings(96, 1));
  A2_LOG_ERROR(std::string(4096, 'x'));
  logging::flush();

  REQUIRE(File(path_).exists());
  REQUIRE(File(path_).size() <= 96);
  REQUIRE(!File(A2_TEST_OUT_DIR "/aria2_LogTest.1.log").exists());
}

void LogTest::testSanitizersProtectLogIntegrity()
{
  REQUIRE_EQ(std::string("line1\\nline2\\t?"),
             logging::sanitizeText("line1\nline2\t\x01"));
  REQUIRE_EQ(std::string("https://example.com/file?<redacted>"),
             logging::sanitizeUri(
                 "https://user:password@example.com/file?token=secret#part"));

  const auto summary = logging::summarizeHttpMessage(
      "GET /jsonrpc?token=secret HTTP/1.1\r\n"
      "Authorization: Basic secret\r\n"
      "X-Private-Token: secret\r\n"
      "Content-Length: 12\r\n");
  REQUIRE(summary.find("GET /jsonrpc?<redacted> HTTP/1.1") !=
          std::string::npos);
  REQUIRE(summary.find("Content-Length=12") != std::string::npos);
  REQUIRE(summary.find("secret") == std::string::npos);
  REQUIRE(summary.find("Authorization") == std::string::npos);
  REQUIRE(summary.find("X-Private-Token") == std::string::npos);
}

void LogTest::testSourceLocationIsPortable()
{
  logging::configure(settings(4096, 1));
  A2_LOG_INFO("first line\nsecond line");
  logging::flush();

  const auto output = readFile(path_);
  REQUIRE(output.find("LogTest.cc:") != std::string::npos);
  REQUIRE(output.find(__FILE__) == std::string::npos);
  REQUIRE(output.find("first line\\nsecond line") != std::string::npos);
}

void LogTest::testLevelFilteringAndReconfiguration()
{
  auto infoSettings = settings(4096, 1);
  infoSettings.fileLevel = spdlog::level::info;
  logging::configure(infoSettings);
  A2_LOG_DEBUG("hidden debug record");
  A2_LOG_INFO("visible info record");
  logging::flush();

  auto output = readFile(path_);
  REQUIRE(output.find("hidden debug record") == std::string::npos);
  REQUIRE(output.find("visible info record") != std::string::npos);

  auto debugSettings = infoSettings;
  debugSettings.fileLevel = spdlog::level::debug;
  logging::configure(debugSettings);
  A2_LOG_DEBUG("visible debug record");
  logging::flush();

  output = readFile(path_);
  REQUIRE(output.find("visible debug record") != std::string::npos);
}

} // namespace aria2
