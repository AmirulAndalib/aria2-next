#include "BtSession.h"
#include "BtDownload.h"
#include "BtResumeStore.h"
#include "BtSettings.h"

#include "a2doctest.h"

#include "BufferedFile.h"
#include "File.h"
#include "DownloadContext.h"
#include "MetadataInfo.h"
#include "RequestGroup.h"
#include "RequestGroupMan.h"
#include "SessionSerializer.h"
#include "Option.h"
#include "OptionParser.h"
#include "prefs.h"
#include "util.h"

#include <libtorrent/address.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/write_resume_data.hpp>

#include <fstream>
#include <sstream>

namespace aria2 {

class BtSessionTest {
public:
  void testSessionStateRoundTrip();
  void testTransferStatAggregation();
  void testFileSelectionResumeState();
  void testDesktopSettings();
  void testTrackerOwnership();
  void testResumeStore();
};

A2_TEST(BtSessionTest, testSessionStateRoundTrip)
A2_TEST(BtSessionTest, testTransferStatAggregation)
A2_TEST(BtSessionTest, testFileSelectionResumeState)
A2_TEST(BtSessionTest, testDesktopSettings)
A2_TEST(BtSessionTest, testTrackerOwnership)
A2_TEST(BtSessionTest, testResumeStore)

void BtSessionTest::testTransferStatAggregation()
{
  BtSessionTransferStat stat;
  stat.updateSessionPayload(4000, 500);
  stat.update(1, 100, 20, 1000, true);
  stat.update(2, 300, 40, 2000, true);
  REQUIRE_EQ(400, stat.snapshot().downloadSpeed);
  REQUIRE_EQ(60, stat.snapshot().uploadSpeed);
  REQUIRE_EQ((int64_t)4000, stat.snapshot().sessionDownloadLength);
  REQUIRE_EQ((int64_t)500, stat.snapshot().sessionUploadLength);
  REQUIRE_EQ((int64_t)3000, stat.snapshot().allTimeUploadLength);

  stat.update(1, 150, 25, 1100, true);
  REQUIRE_EQ(450, stat.snapshot().downloadSpeed);
  REQUIRE_EQ(65, stat.snapshot().uploadSpeed);
  REQUIRE_EQ((int64_t)3100, stat.snapshot().allTimeUploadLength);

  stat.suspend(1);
  REQUIRE_EQ(300, stat.snapshot().downloadSpeed);
  REQUIRE_EQ(40, stat.snapshot().uploadSpeed);
  stat.suspend(2);
  REQUIRE_EQ(0, stat.snapshot().downloadSpeed);
  REQUIRE_EQ(0, stat.snapshot().uploadSpeed);

  stat.update(2, 200, 30, 2100, true);
  stat.retire(2);
  REQUIRE_EQ(0, stat.snapshot().downloadSpeed);
  REQUIRE_EQ(0, stat.snapshot().uploadSpeed);
  REQUIRE_EQ((int64_t)3200, stat.snapshot().allTimeUploadLength);

  stat.update(1, 50, 10, 1200, true);
  stat.clearSpeeds();
  REQUIRE_EQ(0, stat.snapshot().downloadSpeed);
  REQUIRE_EQ(0, stat.snapshot().uploadSpeed);
  REQUIRE_EQ((int64_t)3300, stat.snapshot().allTimeUploadLength);
}

void BtSessionTest::testFileSelectionResumeState()
{
  Option option;
  OptionParser::getInstance()->parseDefaultValues(option);
  option.put(PREF_DIR, A2_TEST_OUT_DIR "/bt-selection");
  option.put(PREF_ENABLE_RPC, A2_V_TRUE);
  option.put(PREF_PAUSE_METADATA, A2_V_TRUE);
  option.put(PREF_BT_SESSION_STATE_FILE,
             A2_TEST_OUT_DIR "/bt-selection/bittorrent.session");

  libtorrent::error_code error;
  auto params =
      libtorrent::load_torrent_file(A2_TEST_DIR "/test.torrent", error, {});
  REQUIRE(!error);
  const auto magnet = libtorrent::make_magnet_uri(params);
  auto probe = BtDownload::fromMagnet(magnet);
  const auto identity = !probe->snapshot().infoHashV1.empty()
                            ? probe->snapshot().infoHashV1
                            : probe->snapshot().infoHashV2;
  const auto resumePath = BtResumeStore::path(&option, identity);
  const auto resume = libtorrent::write_resume_data_buf(params);
  BtResumeStore::write(resumePath, resume.data(), resume.size());

  auto makeGroup = [&magnet](const std::shared_ptr<Option>& taskOption) {
    auto group = std::make_shared<RequestGroup>(GroupId::create(), taskOption);
    auto download = BtDownload::fromMagnet(magnet);
    auto context = std::make_shared<DownloadContext>(16_k, 0);
    download->configure(taskOption.get());
    download->populateDownloadContext(context, taskOption.get());
    group->setDownloadContext(context);
    group->setBtDownload(download);
    group->setMetadataInfo(
        std::make_shared<MetadataInfo>(group->getGroupId(), magnet));
    download->initialize(group.get());
    return group;
  };

  auto awaitingOption = std::make_shared<Option>(option);
  awaitingOption->remove(PREF_PAUSE);
  auto awaiting = makeGroup(awaitingOption);
  REQUIRE(awaiting->isPauseRequested());
  REQUIRE(awaiting->getBtDownload()->awaitingFileSelection());
  REQUIRE_EQ(BtSnapshot::State::AwaitingFileSelection,
             awaiting->getBtDownload()->snapshot().state);
  REQUIRE_EQ((size_t)2, awaiting->getBtDownload()->snapshot().files.size());
  awaiting->getBtDownload()->applyTransportState(
      BtSnapshot::State::Downloading);
  REQUIRE(awaiting->getBtDownload()->awaitingFileSelection());
  REQUIRE_EQ((size_t)2, awaiting->getBtDownload()->snapshot().files.size());

  const auto serialize = [](const std::shared_ptr<RequestGroup>& group,
                            const std::string& path) {
    RequestGroupMan manager({group}, 1, group->getOption().get());
    SessionSerializer serializer(&manager);
    REQUIRE(serializer.save(path));
    std::ifstream input(path, std::ios::binary);
    std::ostringstream output;
    output << input.rdbuf();
    File(path).remove();
    return output.str();
  };
  const auto awaitingSession = serialize(
      awaiting, A2_TEST_OUT_DIR "/bt-selection/awaiting.session");
  REQUIRE(awaitingSession.find(" pause=true\n") != std::string::npos);
  REQUIRE(awaitingSession.find(" pause-metadata=true\n") != std::string::npos);

  awaitingOption->put(PREF_SELECT_FILE, "2");
  const auto selectedLength =
      awaiting->getBtDownload()->snapshot().files[1].length;
  auto selected = util::parseIntSegments("2");
  selected.normalize();
  awaiting->getDownloadContext()->setFileFilter(std::move(selected));
  awaiting->getBtDownload()->updateSelection(awaiting->getDownloadContext());
  REQUIRE_EQ(selectedLength, awaiting->getBtDownload()->snapshot().totalLength);
  REQUIRE_EQ((int64_t)0,
             awaiting->getBtDownload()->snapshot().completedLength);
  awaiting->getBtDownload()->submitFileSelection(awaitingOption.get());
  awaiting->getBtDownload()->prepareFileSelectionResume();
  awaiting->setPauseRequested(false);
  REQUIRE(!awaitingOption->getAsBool(PREF_PAUSE_METADATA));
  const auto selectedSession = serialize(
      awaiting, A2_TEST_OUT_DIR "/bt-selection/selected.session");
  REQUIRE(selectedSession.find(" pause=true\n") == std::string::npos);
  REQUIRE(selectedSession.find(" select-file=2\n") != std::string::npos);
  REQUIRE(selectedSession.find(" pause-metadata=false\n") != std::string::npos);

  auto selectedOption = std::make_shared<Option>(*awaitingOption);
  auto restored = makeGroup(selectedOption);
  REQUIRE(!restored->isPauseRequested());
  REQUIRE(!restored->getBtDownload()->awaitingFileSelection());
  REQUIRE_EQ((size_t)2, restored->getBtDownload()->snapshot().files.size());
  REQUIRE(!restored->getBtDownload()->snapshot().files[0].selected);
  REQUIRE(restored->getBtDownload()->snapshot().files[1].selected);

  File(resumePath).remove();
}

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

  const auto defaultConfig =
      makeBtConfig(&option, {"192.0.2.10", "2001:db8::10"});
  REQUIRE_EQ(std::string("192.0.2.10:0,[2001:db8::10]:0"),
             defaultConfig.listenInterfaces);
  REQUIRE(defaultConfig.outgoingInterfaces.empty());
  REQUIRE(defaultConfig.automaticRoute);
  option.put(PREF_BT_INTERFACE, "en0");
  const auto config = makeBtConfig(&option);
  const auto& settings = config.settings;
  REQUIRE_EQ(libtorrent::settings_pack::pe_enabled,
             settings.get_int(libtorrent::settings_pack::out_enc_policy));
  REQUIRE_EQ(libtorrent::settings_pack::pe_enabled,
             settings.get_int(libtorrent::settings_pack::in_enc_policy));
  REQUIRE_EQ(libtorrent::settings_pack::pe_both,
             settings.get_int(libtorrent::settings_pack::allowed_enc_level));
  REQUIRE(!settings.get_bool(libtorrent::settings_pack::prefer_rc4));
  REQUIRE_EQ(20,
             settings.get_int(libtorrent::settings_pack::unchoke_slots_limit));
  REQUIRE_EQ(15,
             settings.get_int(libtorrent::settings_pack::peer_connect_timeout));
  REQUIRE_EQ(10,
             settings.get_int(libtorrent::settings_pack::handshake_timeout));
  REQUIRE_EQ(60, settings.get_int(
                    libtorrent::settings_pack::min_reconnect_time));
  REQUIRE(!settings.has_val(libtorrent::settings_pack::request_queue_time));
  REQUIRE_EQ(3,
             settings.get_int(libtorrent::settings_pack::max_failcount));
  REQUIRE_EQ(3,
             settings.get_int(libtorrent::settings_pack::request_queue_time));
  REQUIRE_EQ(500, settings.get_int(
                       libtorrent::settings_pack::max_out_request_queue));
  REQUIRE_EQ(2000, settings.get_int(
                        libtorrent::settings_pack::max_allowed_in_request_queue));
  REQUIRE_EQ(30,
             settings.get_int(libtorrent::settings_pack::connection_speed));
  REQUIRE_EQ(100 * 1024 * 1024,
             settings.get_int(
                 libtorrent::settings_pack::max_queued_disk_bytes));
  REQUIRE_EQ(2048,
             settings.get_int(libtorrent::settings_pack::checking_mem_usage));
  REQUIRE_EQ(libtorrent::settings_pack::fastest_upload,
             settings.get_int(
                 libtorrent::settings_pack::seed_choking_algorithm));
  REQUIRE(!settings.get_bool(
      libtorrent::settings_pack::apply_ip_filter_to_trackers));
  REQUIRE(!settings.get_bool(libtorrent::settings_pack::apply_filter_to_dht));
  REQUIRE_EQ(20, settings.get_int(
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
  REQUIRE(!config.automaticRoute);

  option.put(PREF_BT_MAX_OUT_REQUEST_QUEUE, "1500");
  option.put(PREF_BT_DISK_WRITE_CACHE, "write-through");
  option.put(PREF_BT_BLOCKLIST_SCOPE, "all");
  const auto tuned = makeBtConfig(&option).settings;
  REQUIRE_EQ(1500, tuned.get_int(
                       libtorrent::settings_pack::max_out_request_queue));
  REQUIRE_EQ(libtorrent::settings_pack::write_through,
             tuned.get_int(libtorrent::settings_pack::disk_io_write_mode));
  REQUIRE(tuned.get_bool(
      libtorrent::settings_pack::apply_ip_filter_to_trackers));
  REQUIRE(tuned.get_bool(libtorrent::settings_pack::apply_filter_to_dht));

  std::vector<libtorrent::download_priority_t> priorities(
      2, libtorrent::default_priority);
  applyBtFilePrioritySpec(priorities, "1=off,2=top");
  REQUIRE_EQ(libtorrent::dont_download, priorities[0]);
  REQUIRE_EQ(libtorrent::top_priority, priorities[1]);
  REQUIRE_THROWS(applyBtFilePrioritySpec(priorities, "3=normal"));

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
