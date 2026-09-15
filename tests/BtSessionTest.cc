#include "BtSession.h"
#include "ApplicationStatePath.h"
#include "BtDownload.h"
#include "BtStateStore.h"
#include "BtSettings.h"

#include "a2doctest.h"

#include "BufferedFile.h"
#include "File.h"
#include "DownloadContext.h"
#include "DownloadEngine.h"
#include "MetadataInfo.h"
#include "RequestGroup.h"
#include "RequestGroupMan.h"
#include "SessionSerializer.h"
#include "SelectEventPoll.h"
#include "Option.h"
#include "OptionParser.h"
#include "prefs.h"
#include "util.h"
#include "wallclock.h"

#include <libtorrent/address.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/write_resume_data.hpp>

#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>

namespace aria2 {

class BtSessionTest {
public:
  void testSessionStateRoundTrip();
  void testFileSelectionResumeState();
  void testNativeFileSelectionApply();
  void testPausedRestoreHydration();
  void testDesktopSettings();
  void testTrackerOwnership();
  void testTrackerTierNormalization();
  void testStateStore();
};

A2_TEST(BtSessionTest, testSessionStateRoundTrip)
A2_TEST(BtSessionTest, testFileSelectionResumeState)
A2_TEST(BtSessionTest, testNativeFileSelectionApply)
A2_TEST(BtSessionTest, testPausedRestoreHydration)
A2_TEST(BtSessionTest, testDesktopSettings)
A2_TEST(BtSessionTest, testTrackerOwnership)
A2_TEST(BtSessionTest, testTrackerTierNormalization)
A2_TEST(BtSessionTest, testStateStore)

void BtSessionTest::testFileSelectionResumeState()
{
  Option option;
  OptionParser::getInstance()->parseDefaultValues(option);
  option.put(PREF_DIR, A2_TEST_OUT_DIR "/bt-selection");
  option.put(PREF_ENABLE_RPC, A2_V_TRUE);
  option.put(PREF_PAUSE_METADATA, A2_V_TRUE);
  option.put(PREF_STATE_DIR, A2_TEST_OUT_DIR "/bt-selection");

  libtorrent::error_code error;
  auto params =
      libtorrent::load_torrent_file(A2_TEST_DIR "/test.torrent", error, {});
  REQUIRE(!error);
  const auto magnet = libtorrent::make_magnet_uri(params);
  auto probe = BtDownload::fromMagnet(magnet);
  const auto identity = !probe->snapshot().infoHashV1.empty()
                            ? probe->snapshot().infoHashV1
                            : probe->snapshot().infoHashV2;
  BtStateStore stateStore(&option);
  const auto resumePath = stateStore.resumePath(identity);
  const auto resume = libtorrent::write_resume_data_buf(params);
  BtStateStore::writeResume(resumePath, resume.data(), resume.size());

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
  REQUIRE_EQ(BtSnapshot::State::Paused,
             awaiting->getBtDownload()->snapshot().state);
  REQUIRE_EQ(BtSnapshot::FileSelectionState::Awaiting,
             awaiting->getBtDownload()->snapshot().fileSelectionState);
  REQUIRE(!awaiting->getBtDownload()->snapshot().selectedComplete);
  REQUIRE_EQ(0, awaiting->getBtDownload()->snapshot().progressPpm);
  REQUIRE_EQ((size_t)2, awaiting->getBtDownload()->snapshot().files.size());
  awaiting->getBtDownload()->applyTransportState(
      BtSnapshot::State::Downloading);
  REQUIRE(awaiting->getBtDownload()->awaitingFileSelection());
  REQUIRE_EQ((size_t)2, awaiting->getBtDownload()->snapshot().files.size());

  auto independentOption = std::make_shared<Option>(option);
  independentOption->remove(PREF_PAUSE);
  auto independent = makeGroup(independentOption);
  REQUIRE(independent->getBtDownload()->awaitingFileSelection());

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
  REQUIRE(independent->getBtDownload()->awaitingFileSelection());
  awaiting->getBtDownload()->beginFileSelectionApply();
  awaiting->setPauseRequested(false);
  REQUIRE(!awaitingOption->getAsBool(PREF_PAUSE_METADATA));
  REQUIRE(awaiting->getBtDownload()->fileSelectionApplying());
  awaiting->getBtDownload()->applyFileProgress({64, 0});
  REQUIRE_EQ(selectedLength,
             awaiting->getBtDownload()->snapshot().totalLength);
  REQUIRE_EQ((int64_t)0,
             awaiting->getBtDownload()->snapshot().completedLength);
  awaiting->getBtDownload()->completeFileSelectionApply();
  awaiting->getBtDownload()->beginProgressRefresh();
  awaiting->getBtDownload()->applyFileProgress({0, 0});
  REQUIRE(!awaiting->getBtDownload()->fileSelectionApplying());
  const auto selectedSession = serialize(
      awaiting, A2_TEST_OUT_DIR "/bt-selection/selected.session");
  REQUIRE(selectedSession.find(" pause=true\n") == std::string::npos);
  REQUIRE(selectedSession.find(" select-file=2\n") != std::string::npos);
  REQUIRE(selectedSession.find(" pause-metadata=false\n") != std::string::npos);

  params.active_time = 120;
  params.finished_time = 45;
  params.seeding_time = 0;
  params.completed_time = 1;
  const auto completedResume = libtorrent::write_resume_data_buf(params);
  BtStateStore::writeResume(resumePath, completedResume.data(),
                            completedResume.size());

  auto selectedOption = std::make_shared<Option>(*awaitingOption);
  auto restored = makeGroup(selectedOption);
  REQUIRE(!restored->isPauseRequested());
  REQUIRE(!restored->getBtDownload()->awaitingFileSelection());
  REQUIRE(restored->getBtDownload()->snapshot().selectedComplete);
  REQUIRE(!restored->getBtDownload()->snapshot().complete);
  REQUIRE_EQ(120, restored->getBtDownload()->snapshot().activeTime);
  REQUIRE_EQ(45, restored->getBtDownload()->snapshot().finishedTime);
  REQUIRE_EQ(0, restored->getBtDownload()->snapshot().seedingTime);
  REQUIRE_EQ((size_t)2, restored->getBtDownload()->snapshot().files.size());
  REQUIRE(!restored->getBtDownload()->snapshot().files[0].selected);
  REQUIRE(restored->getBtDownload()->snapshot().files[1].selected);

  File(resumePath).remove();
}

void BtSessionTest::testNativeFileSelectionApply()
{
  auto option = std::make_shared<Option>();
  OptionParser::getInstance()->parseDefaultValues(*option);
  option->put(PREF_DIR, A2_TEST_OUT_DIR "/bt-native-selection");
  option->put(PREF_STATE_DIR, A2_TEST_OUT_DIR "/bt-native-selection");
  File(option->get(PREF_DIR)).mkdirs();

  auto group = std::make_shared<RequestGroup>(GroupId::create(), option);
  auto download = BtDownload::fromFile(A2_TEST_DIR "/test.torrent", {});
  auto context = std::make_shared<DownloadContext>();
  download->configure(option.get());
  download->populateDownloadContext(context, option.get());
  download->updateSelection(context);
  group->setDownloadContext(context);
  group->setBtDownload(download);
  download->initialize(group.get());

  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(option.get());
  BtSession session(option.get());

  auto command = session.start(download, group.get(), &engine);
  const auto waitUntil = [&session](const auto& ready) {
    for (int attempt = 0; attempt < 2000; ++attempt) {
      session.poll();
      if (ready()) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
  };
  REQUIRE(waitUntil([&]() {
    try {
      session.forceAnnounce(download);
      return true;
    }
    catch (...) {
      return false;
    }
  }));

  session.requestStop(download, BtDownload::StopReason::Pause);
  REQUIRE(waitUntil([&]() { return download->stopped(); }));

  download->beginFileSelectionPause();
  REQUIRE(download->awaitingFileSelection());
  option->put(PREF_SELECT_FILE, "2");
  auto selected = util::parseIntSegments("2");
  selected.normalize();
  context->setFileFilter(std::move(selected));
  download->updateSelection(context);
  download->submitFileSelection(option.get());
  download->beginFileSelectionApply();
  group->setPauseRequested(false);

  command = session.start(download, group.get(), &engine);
  REQUIRE(download->fileSelectionApplying());
  REQUIRE(waitUntil([&]() { return !download->fileSelectionApplying(); }));
  REQUIRE_EQ(BtSnapshot::FileSelectionState::None,
             download->snapshot().fileSelectionState);
  REQUIRE(!download->failed());

  const auto partfile = option->get(PREF_DIR) + "/." +
                        download->snapshot().infoHashV1 + ".parts";
  {
    BufferedFile file(partfile.c_str(), BufferedFile::WRITE);
    REQUIRE(file);
    REQUIRE_EQ((size_t)5, file.write("stale", 5));
  }
  REQUIRE(File(partfile).exists());
  session.discard(download);
  REQUIRE(waitUntil([&]() { return download->stopped(); }));
  for (int attempt = 0; attempt < 50; ++attempt) {
    session.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  REQUIRE(!File(partfile).exists());
}

void BtSessionTest::testPausedRestoreHydration()
{
  auto option = std::make_shared<Option>();
  OptionParser::getInstance()->parseDefaultValues(*option);
  option->put(PREF_DIR, A2_TEST_OUT_DIR "/bt-paused-restore");
  option->put(PREF_ENABLE_RPC, A2_V_TRUE);
  option->put(PREF_ENABLE_DHT, A2_V_FALSE);
  option->put(PREF_BT_ENABLE_LPD, A2_V_FALSE);
  option->put(PREF_ENABLE_PEER_EXCHANGE, A2_V_FALSE);
  option->put(PREF_BT_SEED_UNVERIFIED, A2_V_TRUE);
  option->put(PREF_STATE_DIR, A2_TEST_OUT_DIR "/bt-paused-restore");

  const auto root = option->get(PREF_DIR) + "/aria2-test";
  File(root + "/aria2/src").mkdirs();
  const auto writeFile = [](const std::string& path, size_t size) {
    BufferedFile file(path.c_str(), BufferedFile::WRITE);
    REQUIRE(file);
    const std::string data(size, '\0');
    REQUIRE_EQ(size, file.write(data.data(), data.size()));
  };
  writeFile(root + "/aria2/src/aria2c", 284);
  writeFile(root + "/aria2-0.2.2.tar.bz2", 100);

  libtorrent::error_code error;
  auto params =
      libtorrent::load_torrent_file(A2_TEST_DIR "/test.torrent", error, {});
  REQUIRE(!error);
  params.save_path = option->get(PREF_DIR);
  params.have_pieces.resize(params.ti->num_pieces(), true);
  params.flags |= libtorrent::torrent_flags::seed_mode;
  params.flags |= libtorrent::torrent_flags::paused;
  params.flags &= ~libtorrent::torrent_flags::auto_managed;
  params.completed_time = 1;
  params.active_time = 90;
  params.finished_time = 30;
  params.seeding_time = 15;

  auto probe = BtDownload::fromFile(A2_TEST_DIR "/test.torrent", {});
  const auto identity = !probe->snapshot().infoHashV1.empty()
                            ? probe->snapshot().infoHashV1
                            : probe->snapshot().infoHashV2;
  BtStateStore stateStore(option.get());
  const auto resumePath = stateStore.resumePath(identity);
  const auto resume = libtorrent::write_resume_data_buf(params);
  BtStateStore::writeResume(resumePath, resume.data(), resume.size());

  auto group = std::make_shared<RequestGroup>(GroupId::create(), option);
  auto download = BtDownload::fromFile(A2_TEST_DIR "/test.torrent", {});
  auto context = std::make_shared<DownloadContext>();
  download->configure(option.get());
  download->populateDownloadContext(context, option.get());
  group->setDownloadContext(context);
  group->setBtDownload(download);
  group->setPauseRequested(true);
  download->initialize(group.get());

  REQUIRE_EQ((int64_t)384, download->snapshot().completedLength);
  REQUIRE(download->snapshot().complete);
  REQUIRE_EQ(BtSnapshot::State::Paused, download->snapshot().state);

  BtSession session(option.get());
  session.restorePaused(download, group.get());
  const auto waitUntil = [&session](const auto& ready) {
    for (int attempt = 0; attempt < 3000; ++attempt) {
      global::wallclock().reset();
      session.poll();
      if (ready()) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
  };
  REQUIRE(waitUntil([&]() { return download->stopped(); }));
  REQUIRE_EQ(BtSnapshot::State::Paused, download->snapshot().state);
  REQUIRE_EQ((int64_t)384, download->snapshot().completedLength);
  REQUIRE(download->snapshot().complete);

  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(option.get());
  group->setPauseRequested(false);
  auto command = session.start(download, group.get(), &engine);
  const bool resumed = waitUntil([&]() {
    return download->snapshot().state == BtSnapshot::State::Seeding;
  });
  REQUIRE(resumed);
  REQUIRE_EQ((int64_t)384, download->snapshot().completedLength);

  session.discard(download);
  REQUIRE(waitUntil([&]() { return download->stopped(); }));
}

void BtSessionTest::testStateStore()
{
  Option option;
  OptionParser::getInstance()->parseDefaultValues(option);
  option.put(PREF_STATE_DIR, A2_TEST_OUT_DIR "/bt-state");
  BtStateStore store(&option);
  const auto identity = std::string(40, '1');
  const auto path = store.resumePath(identity);
  REQUIRE_EQ(std::string(A2_TEST_OUT_DIR
                         "/bt-state/bittorrent/torrents/"
                         "1111111111111111111111111111111111111111.fastresume"),
             path);
  BtStateStore::writeResume(path, "resume", 6);
  REQUIRE_EQ(std::string("resume"), BtStateStore::readResume(path));

  const auto metadata = store.storeMetadata("metadata");
  const auto foreign = util::applyDir(store.directory(), "user.torrent");
  {
    BufferedFile file(foreign.c_str(), BufferedFile::WRITE);
    REQUIRE(file);
    REQUIRE_EQ((size_t)4, file.write("user", 4));
  }
  store.collect({metadata});
  REQUIRE(File(metadata).exists());
  REQUIRE(!File(path).exists());
  REQUIRE(File(foreign).exists());
  store.collect({});
  REQUIRE(!File(metadata).exists());
  REQUIRE(File(foreign).exists());
  File(foreign).remove();
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
  REQUIRE_EQ(std::string("qBittorrent/5.2.3"),
             defaultConfig.settings.get_str(
                 libtorrent::settings_pack::user_agent));
  REQUIRE_EQ(std::string("-qB5230-"),
             defaultConfig.settings.get_str(
                 libtorrent::settings_pack::peer_fingerprint));
  REQUIRE(defaultConfig.settings
              .get_str(libtorrent::settings_pack::handshake_client_version)
              .empty());
  option.put(PREF_BT_USER_AGENT, "CustomClient/1.0");
  option.put(PREF_BT_PEER_ID_PREFIX, "-CC1000-");
  const auto customIdentity = makeBtConfig(&option).settings;
  REQUIRE_EQ(std::string("CustomClient/1.0"),
             customIdentity.get_str(libtorrent::settings_pack::user_agent));
  REQUIRE_EQ(
      std::string("-CC1000-"),
      customIdentity.get_str(libtorrent::settings_pack::peer_fingerprint));
  REQUIRE_EQ(std::string("0.0.0.0:0,[::]:0"),
             defaultConfig.listenInterfaces);
  REQUIRE(defaultConfig.outgoingInterfaces.empty());
  option.put(PREF_BT_INTERFACE, "en0");
  const auto config = makeBtConfig(&option);
  const auto& settings = config.settings;
  REQUIRE_EQ(libtorrent::settings_pack::pe_enabled,
             settings.get_int(libtorrent::settings_pack::out_enc_policy));
  REQUIRE_EQ(libtorrent::settings_pack::pe_enabled,
             settings.get_int(libtorrent::settings_pack::in_enc_policy));
  REQUIRE_EQ(libtorrent::settings_pack::pe_both,
             settings.get_int(libtorrent::settings_pack::allowed_enc_level));
  REQUIRE(settings.get_bool(
      libtorrent::settings_pack::prefer_encrypted_connections));
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
  REQUIRE_EQ(128, settings.get_int(
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
  REQUIRE_EQ(10, config.trackerCompletionTimeout);
  REQUIRE_EQ(10, config.trackerReceiveTimeout);
  REQUIRE_EQ(std::string("en0:0"), config.listenInterfaces);
  REQUIRE_EQ(std::string("en0"), config.outgoingInterfaces);

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
  REQUIRE(!required.get_bool(
      libtorrent::settings_pack::prefer_encrypted_connections));

  option.put(PREF_BT_ENCRYPTION, V_DISABLED);
  const auto disabled = makeBtConfig(&option).settings;
  REQUIRE_EQ(libtorrent::settings_pack::pe_disabled,
             disabled.get_int(libtorrent::settings_pack::out_enc_policy));
  REQUIRE_EQ(libtorrent::settings_pack::pe_disabled,
             disabled.get_int(libtorrent::settings_pack::in_enc_policy));
  REQUIRE(!disabled.get_bool(
      libtorrent::settings_pack::prefer_encrypted_connections));
}

void BtSessionTest::testTrackerOwnership()
{
  Option option;
  OptionParser::getInstance()->parseDefaultValues(option);
  option.put(PREF_BT_TRACKER,
             "udp://one.example:6969/announce,"
             "https://two.example/announce,"
             "http://three.example/announce");
  auto magnet = BtDownload::fromMagnet(
      "magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567");
  magnet->configure(&option);
  const auto& globalTiers = magnet->snapshot().announceList;
  REQUIRE_EQ((size_t)3, globalTiers.size());
  REQUIRE_EQ((size_t)1, globalTiers[0].size());
  REQUIRE_EQ((size_t)1, globalTiers[1].size());
  REQUIRE_EQ((size_t)1, globalTiers[2].size());
  REQUIRE_EQ(std::string("global"), magnet->trackerSource(
                                        "udp://one.example:6969/announce"));

  auto download = BtDownload::fromFile(A2_TEST_DIR "/test.torrent", {});
  download->configure(&option);
  REQUIRE_EQ(std::string("global"), download->trackerSource(
                                        "udp://one.example:6969/announce"));

  std::vector<libtorrent::create_file_entry> files;
  files.emplace_back("private.bin", 1);
  libtorrent::create_torrent torrent(
      std::move(files), 16_k, libtorrent::create_torrent::v1_only);
  torrent.set_hash(libtorrent::piece_index_t{0},
                   libtorrent::sha1_hash::max());
  torrent.set_priv(true);
  const auto encoded = torrent.generate_buf();
  auto privateDownload = BtDownload::fromBuffer(
      std::string(encoded.data(), encoded.size()), {});
  privateDownload->configure(&option);
  REQUIRE_EQ(std::string("unknown"), privateDownload->trackerSource(
                                         "udp://one.example:6969/announce"));
}

void BtSessionTest::testTrackerTierNormalization()
{
  std::vector<libtorrent::create_file_entry> files;
  files.emplace_back("tracker-tier-limit.bin", 1);
  libtorrent::create_torrent torrent(
      std::move(files), 16_k, libtorrent::create_torrent::v1_only);
  torrent.set_hash(libtorrent::piece_index_t{0},
                   libtorrent::sha1_hash::max());
  for (int tier = 0; tier <= 256; ++tier) {
    torrent.add_tracker("http://tracker-" + std::to_string(tier) +
                            ".example/announce",
                        tier);
  }
  const auto encoded = torrent.generate_buf();

  Option option;
  OptionParser::getInstance()->parseDefaultValues(option);
  auto download = BtDownload::fromBuffer(
      std::string(encoded.data(), encoded.size()), {});
  download->configure(&option);

  const auto& nativeTiers = download->snapshot().announceList;
  REQUIRE_EQ((size_t)256, nativeTiers.size());
  REQUIRE_EQ((size_t)2, nativeTiers.back().size());

  option.put(PREF_BT_TRACKER,
             "udp://global-one.example:6969/announce,"
             "https://global-two.example/announce");
  download->configure(&option);
  const auto& tiersWithGlobal = download->snapshot().announceList;
  REQUIRE_EQ((size_t)256, tiersWithGlobal.size());
  REQUIRE_EQ((size_t)3, tiersWithGlobal[254].size());
  REQUIRE_EQ((size_t)2, tiersWithGlobal[255].size());
  REQUIRE_EQ(std::string("udp://global-one.example:6969/announce"),
             tiersWithGlobal[255][0]);
  REQUIRE_EQ(std::string("global"), download->trackerSource(
                                        "https://global-two.example/announce"));
}

void BtSessionTest::testSessionStateRoundTrip()
{
  const std::string stateDirectory = A2_TEST_OUT_DIR "/bt-session-state";

  Option option;
  OptionParser::getInstance()->parseDefaultValues(option);
  REQUIRE_EQ(V_BOTH, option.get(PREF_BT_TRANSPORT));
  REQUIRE_EQ(V_PREFERRED, option.get(PREF_BT_ENCRYPTION));
  REQUIRE_EQ(10, option.getAsInt(PREF_BT_TRACKER_COMPLETION_TIMEOUT));
  REQUIRE_EQ(10, option.getAsInt(PREF_BT_TRACKER_RECEIVE_TIMEOUT));
  option.put(PREF_STATE_DIR, stateDirectory);
  const auto path = state::btSessionFile(&option);
  File(File(path).getDirname()).mkdirs();
  File(path).remove();
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
  const auto original = BtStateStore::readResume(path);
  {
    BtSession session(&option);
  }
  REQUIRE_EQ(original, BtStateStore::readResume(path));
  File(path).remove();
}

} // namespace aria2
