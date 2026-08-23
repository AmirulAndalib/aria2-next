#include "LegacyOptionAdapter.h"

#include "a2doctest.h"

#include <algorithm>
#include <array>
#include <getopt.h>
#include <sstream>
#include <unistd.h>

#include "Option.h"
#include "OptionParser.h"
#include "Exception.h"
#include "prefs.h"

namespace aria2 {

namespace {

const std::string* findValue(const KeyVals& options, const std::string& name)
{
  const auto result =
      std::find_if(options.begin(), options.end(),
                   [&name](const auto& item) { return item.first == name; });
  return result == options.end() ? nullptr : &result->second;
}

} // namespace

class LegacyOptionAdapterTest {
public:
  void testInventory();
  void testMappingAndCanonicalPrecedence();
  void testEncryptionGroup();
  void testLegacyValues();
  void testCommandLine();
  void testParserIngress();
  void testCommandLinePreservesSecretErasure();
  void testLegacyRpcAuthentication();
  void testProjection();
};

A2_TEST(LegacyOptionAdapterTest, testInventory)
A2_TEST(LegacyOptionAdapterTest, testMappingAndCanonicalPrecedence)
A2_TEST(LegacyOptionAdapterTest, testEncryptionGroup)
A2_TEST(LegacyOptionAdapterTest, testLegacyValues)
A2_TEST(LegacyOptionAdapterTest, testCommandLine)
A2_TEST(LegacyOptionAdapterTest, testParserIngress)
A2_TEST(LegacyOptionAdapterTest, testCommandLinePreservesSecretErasure)
A2_TEST(LegacyOptionAdapterTest, testLegacyRpcAuthentication)
A2_TEST(LegacyOptionAdapterTest, testProjection)

void LegacyOptionAdapterTest::testInventory()
{
  constexpr std::array<const char*, 39> names{{
      "bt-detach-seed-only",
      "bt-enable-hook-after-hash-check",
      "bt-force-encryption",
      "bt-hash-check-seed",
      "bt-keep-alive-interval",
      "bt-load-saved-metadata",
      "bt-lpd-interface",
      "bt-metadata-only",
      "bt-min-crypto-level",
      "bt-prioritize-piece",
      "bt-remove-unselected-file",
      "bt-request-peer-speed-limit",
      "bt-request-timeout",
      "bt-require-crypto",
      "bt-save-metadata",
      "bt-stop-timeout",
      "bt-timeout",
      "bt-tracker-connect-timeout",
      "bt-tracker-interval",
      "bt-tracker-timeout",
      "dht-entry-point",
      "dht-entry-point-host",
      "dht-entry-point-host6",
      "dht-entry-point-port",
      "dht-entry-point-port6",
      "dht-entry-point6",
      "dht-file-path",
      "dht-file-path6",
      "dht-listen-addr",
      "dht-listen-addr6",
      "dht-listen-port",
      "dht-message-timeout",
      "enable-async-dns6",
      "enable-dht6",
      "peer-agent",
      "peer-connection-timeout",
      "peer-id-prefix",
      "rpc-passwd",
      "rpc-user",
  }};
  for (const auto* name : names) {
    REQUIRE(isLegacyOption(name));
  }
  REQUIRE(!isLegacyOption("bt-encryption"));
}

void LegacyOptionAdapterTest::testLegacyValues()
{
  auto output = adaptLegacyOptions({{"log-level", "notice"},
                                    {"console-log-level", "notice"},
                                    {"bt-encryption", "enabled"}},
                                   LegacyOptionSource::Configuration);
  REQUIRE_EQ(V_INFO, *findValue(output, "log-level"));
  REQUIRE_EQ(V_INFO, *findValue(output, "console-log-level"));
  REQUIRE_EQ(V_PREFERRED, *findValue(output, "bt-encryption"));

  output = adaptLegacyOptions({{"log-level", "notice"}, {"log-level", "warn"}},
                              LegacyOptionSource::Rpc);
  REQUIRE_EQ(std::string("warn"), *findValue(output, "log-level"));

  char program[] = "aria2-next";
  char logOption[] = "--log-level";
  char logValue[] = "notice";
  char encryption[] = "--bt-encryption=enabled";
  char* argv[]{program, logOption, logValue, encryption};
  const auto commandLine =
      adaptLegacyCommandLine(4, argv, LegacyOptionSource::CommandLine);
  REQUIRE_EQ(std::string("--log-level=info"), commandLine[1]);
  REQUIRE_EQ(std::string("--bt-encryption=preferred"), commandLine[2]);
}

void LegacyOptionAdapterTest::testMappingAndCanonicalPrecedence()
{
  const KeyVals input{{"bt-detach-seed-only", "true"},
                      {"bt-lpd-interface", "en1"},
                      {"bt-tracker-timeout", "17"},
                      {"dht-entry-point", "router.example:6881"},
                      {"dht-entry-point6", "[2001:db8::1]:6881"},
                      {"dht-listen-port", "49000"},
                      {"listen-port", "50000"},
                      {"bt-save-metadata", "true"}};
  const auto output =
      adaptLegacyOptions(input, LegacyOptionSource::Configuration);
  REQUIRE_EQ(std::string("true"), *findValue(output, "detach-share-only"));
  REQUIRE_EQ(std::string("en1"), *findValue(output, "bt-interface"));
  REQUIRE_EQ(std::string("17"),
             *findValue(output, "bt-tracker-receive-timeout"));
  REQUIRE_EQ(std::string("router.example:6881,[2001:db8::1]:6881"),
             *findValue(output, "bt-dht-bootstrap-nodes"));
  REQUIRE_EQ(std::string("50000"), *findValue(output, "listen-port"));
  REQUIRE(!findValue(output, "bt-save-metadata"));
}

void LegacyOptionAdapterTest::testEncryptionGroup()
{
  auto output = adaptLegacyOptions(
      {{"bt-min-crypto-level", "arc4"}, {"bt-require-crypto", "false"}},
      LegacyOptionSource::Rpc);
  REQUIRE_EQ(V_REQUIRED, *findValue(output, "bt-encryption"));

  output = adaptLegacyOptions(
      {{"bt-force-encryption", "true"}, {"bt-encryption", "disabled"}},
      LegacyOptionSource::Rpc);
  REQUIRE_EQ(std::string("disabled"), *findValue(output, "bt-encryption"));

  REQUIRE_THROWS_AS(adaptLegacyOptions({{"bt-request-timeout", "invalid"}},
                                       LegacyOptionSource::Rpc),
                    Exception);
}

void LegacyOptionAdapterTest::testCommandLine()
{
  char program[] = "aria2-next";
  char legacy[] = "--bt-require-crypto=true";
  char canonical[] = "--bt-encryption=disabled";
  char uri[] = "magnet:?xt=urn:btih:0123456789012345678901234567890123456789";
  char* argv[]{program, legacy, canonical, uri};
  const auto output =
      adaptLegacyCommandLine(4, argv, LegacyOptionSource::CommandLine);
  REQUIRE_EQ(std::string("aria2-next"), output[0]);
  REQUIRE_EQ(std::string("--bt-encryption=required"), output[1]);
  REQUIRE_EQ(std::string("--bt-encryption=disabled"), output[2]);
  REQUIRE_EQ(std::string(uri), output[3]);
}

void LegacyOptionAdapterTest::testProjection()
{
  Option option;
  OptionParser::getInstance()->parseDefaultValues(option);
  option.put(PREF_BT_ENCRYPTION, V_REQUIRED);
  option.put(PREF_BT_TRACKER_RECEIVE_TIMEOUT, "23");
  option.put(PREF_DETACH_SHARE_ONLY, A2_V_TRUE);

  std::string value;
  REQUIRE(projectLegacyOption(&option, "bt-require-crypto", value));
  REQUIRE_EQ(A2_V_TRUE, value);
  REQUIRE(projectLegacyOption(&option, "bt-tracker-timeout", value));
  REQUIRE_EQ(std::string("23"), value);
  REQUIRE(projectLegacyOption(&option, "bt-detach-seed-only", value));
  REQUIRE_EQ(A2_V_TRUE, value);
}

void LegacyOptionAdapterTest::testParserIngress()
{
  Option configured;
  std::istringstream config("bt-require-crypto=true\n"
                            "bt-tracker-timeout=29\n"
                            "enable-async-dns6=true\n"
                            "log-level=notice\n");
  OptionParser::getInstance()->parse(configured, config);
  REQUIRE_EQ(V_REQUIRED, configured.get(PREF_BT_ENCRYPTION));
  REQUIRE_EQ(std::string("29"),
             configured.get(PREF_BT_TRACKER_RECEIVE_TIMEOUT));
  REQUIRE_EQ(V_INFO, configured.get(PREF_LOG_LEVEL));

  Option session;
  std::istringstream saved(" bt-metadata-only=true\n"
                           " bt-detach-seed-only=true\n");
  OptionParser::getInstance()->parseInternal(session, saved);
  REQUIRE_EQ(A2_V_TRUE, session.get(PREF_PAUSE_METADATA));
  REQUIRE_EQ(A2_V_TRUE, session.get(PREF_DETACH_SHARE_ONLY));
}

void LegacyOptionAdapterTest::testCommandLinePreservesSecretErasure()
{
  char program[] = "aria2-next";
  char legacy[] = "--bt-require-crypto=true";
  char secretOption[] = "--rpc-secret";
  char secretValue[] = "sensitive";
  char* argv[]{program, legacy, secretOption, secretValue};
  optind = 1;
#ifdef __APPLE__
  optreset = 1;
#endif
  std::stringstream output;
  std::vector<std::string> nonoptions;
  OptionParser::getInstance()->parseArg(output, nonoptions, 4, argv);
  REQUIRE_EQ(std::string("*********"), std::string(secretValue));
  optind = 1;
#ifdef __APPLE__
  optreset = 1;
#endif
}

void LegacyOptionAdapterTest::testLegacyRpcAuthentication()
{
  char program[] = "aria2-next";
  char user[] = "--rpc-user=operator";
  char passwordOption[] = "--rpc-passwd";
  char password[] = "sensitive";
  char* argv[]{program, user, passwordOption, password};
  REQUIRE_THROWS_AS(
      adaptLegacyCommandLine(4, argv, LegacyOptionSource::CommandLine),
      Exception);
  REQUIRE_EQ(std::string("--rpc-user=********"), std::string(user));
  REQUIRE_EQ(std::string("*********"), std::string(password));

  const auto empty = adaptLegacyOptions({{"rpc-user", ""}, {"rpc-passwd", ""}},
                                        LegacyOptionSource::Rpc);
  REQUIRE(empty.empty());
}

} // namespace aria2
