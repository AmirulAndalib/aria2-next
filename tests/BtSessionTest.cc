#include "BtSession.h"

#include "a2doctest.h"

#include "BufferedFile.h"
#include "File.h"
#include "Option.h"
#include "OptionParser.h"
#include "prefs.h"

namespace aria2 {

class BtSessionTest {
public:
  void testSessionStateRoundTrip();
};

A2_TEST(BtSessionTest, testSessionStateRoundTrip)

void BtSessionTest::testSessionStateRoundTrip()
{
  const std::string path = A2_TEST_OUT_DIR "/bittorrent.session";
  File(path).remove();

  Option option;
  OptionParser::getInstance()->parseDefaultValues(option);
  REQUIRE_EQ(V_BOTH, option.get(PREF_BT_TRANSPORT));
  REQUIRE_EQ(V_ENABLED, option.get(PREF_BT_ENCRYPTION));
  REQUIRE_EQ(30, option.getAsInt(PREF_BT_TRACKER_CONNECT_TIMEOUT));
  REQUIRE_EQ(10, option.getAsInt(PREF_BT_TRACKER_TIMEOUT));
  option.put(PREF_BT_SESSION_STATE_FILE, path);
  option.put(PREF_LISTEN_PORT, "0");
  option.put(PREF_ENABLE_DHT, A2_V_FALSE);
  option.put(PREF_BT_PORT_MAPPING, A2_V_FALSE);

  {
    BtSession session(&option);
  }
  REQUIRE(File(path).isFile());
  REQUIRE(File(path).size() > 0);

  {
    BufferedFile file(path.c_str(), BufferedFile::WRITE);
    REQUIRE(file);
    REQUIRE_EQ((size_t)7, file.write("invalid", 7));
  }
  {
    BtSession session(&option);
  }
  REQUIRE(File(path).size() > 7);
  File(path).remove();
}

} // namespace aria2
