#include "BtSession.h"
#include "BtDiscovery.h"
#include "BtResumeStore.h"
#include "BtSettings.h"

#include "a2doctest.h"

#include "BufferedFile.h"
#include "File.h"
#include "Option.h"
#include "OptionParser.h"
#include "prefs.h"

#include <libtorrent/address.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>

namespace aria2 {

class BtSessionTest {
public:
  void testSessionStateRoundTrip();
  void testDesktopSettings();
  void testDiscoveryRecovery();
  void testTrackerOwnership();
  void testResumeStore();
};

A2_TEST(BtSessionTest, testSessionStateRoundTrip)
A2_TEST(BtSessionTest, testDesktopSettings)
A2_TEST(BtSessionTest, testDiscoveryRecovery)
A2_TEST(BtSessionTest, testTrackerOwnership)
A2_TEST(BtSessionTest, testResumeStore)

void BtSessionTest::testResumeStore()
{
  Option option;
  OptionParser::getInstance()->parseDefaultValues(option);
  option.put(PREF_BT_SESSION_STATE_FILE,
             A2_TEST_OUT_DIR "/bt-state/bittorrent.session");
  const auto path = BtResumeStore::path(&option, "0123456789abcdef");
  REQUIRE_EQ(std::string(A2_TEST_OUT_DIR
                         "/bt-state/torrents/0123456789abcdef.fastresume"),
             path);
  BtResumeStore::write(path, "resume", 6);
  REQUIRE_EQ(std::string("resume"), BtResumeStore::read(path));
  File(path).remove();
}

void BtSessionTest::testDesktopSettings()
{
  Option option;
  OptionParser::getInstance()->parseDefaultValues(option);
  option.put(PREF_LISTEN_PORT, "0");
  option.put(PREF_INTERFACE, "lo0");
  option.put(PREF_ENABLE_DHT, A2_V_FALSE);
  option.put(PREF_BT_PORT_MAPPING, A2_V_FALSE);

  const auto defaultConfig = makeBtConfig(&option);
  REQUIRE_EQ(std::string("0.0.0.0:0,[::]:0"),
             defaultConfig.listenInterfaces);
  option.put(PREF_BT_INTERFACE, "en0");
  const auto config = makeBtConfig(&option);
  const auto& settings = config.settings;
  REQUIRE_EQ(libtorrent::settings_pack::pe_forced,
             settings.get_int(libtorrent::settings_pack::out_enc_policy));
  REQUIRE_EQ(libtorrent::settings_pack::pe_enabled,
             settings.get_int(libtorrent::settings_pack::in_enc_policy));
  REQUIRE_EQ(libtorrent::settings_pack::pe_both,
             settings.get_int(libtorrent::settings_pack::allowed_enc_level));
  REQUIRE(!settings.get_bool(libtorrent::settings_pack::prefer_rc4));
  REQUIRE_EQ(20,
             settings.get_int(libtorrent::settings_pack::unchoke_slots_limit));
  REQUIRE(settings.get_bool(libtorrent::settings_pack::smooth_connects));
  REQUIRE_EQ(30,
             settings.get_int(libtorrent::settings_pack::connection_speed));
  REQUIRE_EQ(30, settings.get_int(
                     libtorrent::settings_pack::torrent_connect_boost));
  REQUIRE_EQ(3,
             settings.get_int(libtorrent::settings_pack::handshake_timeout));
  REQUIRE_EQ(1, settings.get_int(
                    libtorrent::settings_pack::min_reconnect_time));
  REQUIRE_EQ(3,
             settings.get_int(libtorrent::settings_pack::max_failcount));
  REQUIRE_EQ(1,
             settings.get_int(libtorrent::settings_pack::request_queue_time));
  REQUIRE_EQ(4096, settings.get_int(
                       libtorrent::settings_pack::max_out_request_queue));
  REQUIRE_EQ(2, settings.get_int(
                    libtorrent::settings_pack::whole_pieces_threshold));
  REQUIRE_EQ(50, settings.get_int(
                     libtorrent::settings_pack::max_concurrent_http_announces));
  REQUIRE(settings.get_bool(libtorrent::settings_pack::announce_to_all_tiers));
  REQUIRE(
      !settings.get_bool(libtorrent::settings_pack::announce_to_all_trackers));
  REQUIRE_EQ(30, config.trackerCompletionTimeout);
  REQUIRE_EQ(10, config.trackerReceiveTimeout);
  REQUIRE_EQ(std::string("en0:0"), config.listenInterfaces);
  REQUIRE_EQ(std::string("en0"), config.outgoingInterfaces);

  option.put(PREF_BT_ENCRYPTION, V_REQUIRED);
  const auto required = makeBtConfig(&option).settings;
  REQUIRE_EQ(libtorrent::settings_pack::pe_forced,
             required.get_int(libtorrent::settings_pack::out_enc_policy));
  REQUIRE_EQ(libtorrent::settings_pack::pe_forced,
             required.get_int(libtorrent::settings_pack::in_enc_policy));

  option.put(PREF_BT_ENCRYPTION, V_DISABLED);
  const auto disabled = makeBtConfig(&option).settings;
  REQUIRE_EQ(libtorrent::settings_pack::pe_disabled,
             disabled.get_int(libtorrent::settings_pack::out_enc_policy));
  REQUIRE_EQ(libtorrent::settings_pack::pe_disabled,
             disabled.get_int(libtorrent::settings_pack::in_enc_policy));
}

void BtSessionTest::testDiscoveryRecovery()
{
  BtDiscoveryController controller;
  BtDiscoveryState state;
  controller.activate(state, 3);
  REQUIRE_EQ(std::string("discovering"),
             std::string(btDiscoveryPhaseName(state.phase)));
  REQUIRE(controller.requestTrackerRetry(
      state, true, 1, "http://tracker.example/announce"));
  REQUIRE(!controller.requestTrackerRetry(
      state, true, 2, "http://tracker.example/announce"));
  REQUIRE(state.trackerRetryUsed);
  REQUIRE_EQ(std::string("http://tracker.example/announce"),
             state.retryTracker);
  controller.trackerReplied(state, 6);
  REQUIRE_EQ(6, state.trackerPeersReceived);
  REQUIRE_EQ(std::string("discovering"),
             std::string(btDiscoveryPhaseName(state.phase)));
  controller.peersObserved(state, 1);
  REQUIRE_EQ(std::string("healthy"),
             std::string(btDiscoveryPhaseName(state.phase)));
  controller.peersObserved(state, 0);
  REQUIRE_EQ(std::string("discovering"),
             std::string(btDiscoveryPhaseName(state.phase)));
}

void BtSessionTest::testTrackerOwnership()
{
  Option option;
  OptionParser::getInstance()->parseDefaultValues(option);
  option.put(PREF_BT_TRACKER, "udp://tracker.example:6969/announce");
  auto download = BtDownload::fromFile(A2_TEST_DIR "/test.torrent", {});
  download->configure(&option);
  REQUIRE_EQ(std::string("global"), download->trackerSource(
                                        "udp://tracker.example:6969/announce"));
}

void BtSessionTest::testSessionStateRoundTrip()
{
  const std::string path = A2_TEST_OUT_DIR "/bittorrent.session";
  File(path).remove();

  Option option;
  OptionParser::getInstance()->parseDefaultValues(option);
  REQUIRE_EQ(V_BOTH, option.get(PREF_BT_TRANSPORT));
  REQUIRE_EQ(V_PREFERRED, option.get(PREF_BT_ENCRYPTION));
  REQUIRE_EQ(30, option.getAsInt(PREF_BT_TRACKER_COMPLETION_TIMEOUT));
  REQUIRE_EQ(10, option.getAsInt(PREF_BT_TRACKER_RECEIVE_TIMEOUT));
  option.put(PREF_BT_SESSION_STATE_FILE, path);
  option.put(PREF_LISTEN_PORT, "0");
  option.put(PREF_ENABLE_DHT, A2_V_FALSE);
  option.put(PREF_BT_PORT_MAPPING, A2_V_FALSE);

  libtorrent::session_params params;
  params.dht_state.nodes.emplace_back(
      libtorrent::make_address("192.0.2.1"), 6881);
  const auto encoded = libtorrent::write_session_params_buf(
      params, libtorrent::session::save_dht_state);
  {
    BufferedFile file(path.c_str(), BufferedFile::WRITE);
    REQUIRE(file);
    REQUIRE_EQ(encoded.size(), file.write(encoded.data(), encoded.size()));
  }
  const auto original = BtResumeStore::read(path);
  {
    BtSession session(&option);
  }
  REQUIRE_EQ(original, BtResumeStore::read(path));
  File(path).remove();
}

} // namespace aria2
