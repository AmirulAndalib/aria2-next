#include "ApplicationStatePath.h"

#include "a2doctest.h"

#include "Option.h"
#include "prefs.h"
#include "util.h"

namespace aria2 {

class ApplicationStatePathTest {
public:
  void testDefaultDirectory();
  void testProtocolPaths();
};

A2_TEST(ApplicationStatePathTest, testDefaultDirectory)
A2_TEST(ApplicationStatePathTest, testProtocolPaths)

void ApplicationStatePathTest::testDefaultDirectory()
{
  const auto path = state::defaultDirectory();
  REQUIRE(!path.empty());
  REQUIRE(util::endsWith(path, "/aria2-next"));
}

void ApplicationStatePathTest::testProtocolPaths()
{
  Option option;
  option.put(PREF_STATE_DIR, "/var/lib/aria2-next");
  REQUIRE_EQ(std::string("/var/lib/aria2-next/bittorrent/session"),
             state::btSessionFile(&option));
  REQUIRE_EQ(std::string("/var/lib/aria2-next/bittorrent/torrents"),
             state::btResumeDirectory(&option));
  REQUIRE_EQ(std::string("/var/lib/aria2-next/ed2k/state.db"),
             state::ed2kDatabaseFile(&option));
  REQUIRE_EQ(std::string("/var/lib/aria2-next/stream/state.db"),
             state::streamDatabaseFile(&option));
}

} // namespace aria2
