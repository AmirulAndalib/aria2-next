#include "BtPeerBlocklist.h"

#include <sstream>

#include "a2doctest.h"

#include "Exception.h"
#include "DownloadEngine.h"
#include "Option.h"
#include "Peer.h"
#include "PeerAbstractCommand.h"
#include "SelectEventPoll.h"
#include "BtRegistry.h"
#include "prefs.h"

namespace aria2 {

class BtPeerBlocklistTest {

public:
  void testLoadBtnRules();
  void testIdempotentReplacement();
  void testRejectInvalidReload();
  void testStopBlocklistedPeerCommand();
  void testDisconnectBlockedPeerCommands();
};

A2_TEST(BtPeerBlocklistTest, testLoadBtnRules)
A2_TEST(BtPeerBlocklistTest, testIdempotentReplacement)
A2_TEST(BtPeerBlocklistTest, testRejectInvalidReload)
A2_TEST(BtPeerBlocklistTest, testStopBlocklistedPeerCommand)
A2_TEST(BtPeerBlocklistTest, testDisconnectBlockedPeerCommands)

void BtPeerBlocklistTest::testLoadBtnRules()
{
  std::istringstream input(
      "# BTN rules\n"
      "203.0.113.25\n"
      "198.51.100.0/24\n"
      "2001:db8::1234\n"
      "2001:250:3c08:4500::/56\n"
      "::ffff:192.0.2.45\n");
  BtPeerBlocklist blocklist;

  blocklist.load(input, "memory");

  REQUIRE_EQ((size_t)5, blocklist.count());
  REQUIRE(blocklist.contains("203.0.113.25"));
  REQUIRE(!blocklist.contains("203.0.113.26"));
  REQUIRE(blocklist.contains("198.51.100.255"));
  REQUIRE(!blocklist.contains("198.51.101.0"));
  REQUIRE(blocklist.contains("2001:db8::1234"));
  REQUIRE(blocklist.contains("2001:250:3c08:45ff::1"));
  REQUIRE(!blocklist.contains("2001:250:3c08:4600::1"));
  REQUIRE(blocklist.contains("192.0.2.45"));
  REQUIRE(blocklist.contains("::ffff:192.0.2.45"));
}

void BtPeerBlocklistTest::testIdempotentReplacement()
{
  BtPeerBlocklist blocklist;

  REQUIRE(blocklist.replace({"203.0.113.0/24", "203.0.113.0/25"},
                            "first"));
  REQUIRE_EQ((size_t)1, blocklist.count());
  const auto revision = blocklist.revision();

  REQUIRE(!blocklist.replace({"203.0.113.0/24"}, "equivalent"));
  REQUIRE_EQ(revision, blocklist.revision());
  REQUIRE_EQ((size_t)1, blocklist.count());

  REQUIRE(blocklist.replace({"198.51.100.0/24"}, "changed"));
  REQUIRE_EQ(revision + 1, blocklist.revision());
}

void BtPeerBlocklistTest::testRejectInvalidReload()
{
  BtPeerBlocklist blocklist;
  std::istringstream valid("203.0.113.0/24\n");
  blocklist.load(valid, "valid");

  std::istringstream invalid("not-an-ip\n");
  REQUIRE_THROWS_AS(blocklist.load(invalid, "invalid"), Exception);

  REQUIRE_EQ((size_t)1, blocklist.count());
  REQUIRE(blocklist.contains("203.0.113.10"));
}

namespace {

struct TestPeerCommandState {
  bool blocked = false;
  bool retryRequested = false;
  bool retried = false;
};

class TestPeerCommand : public PeerAbstractCommand {
public:
  bool blocked = false;
  bool executed = false;
  bool retried = false;
  bool retryRequested = false;

  TestPeerCommand(const std::shared_ptr<Peer>& peer, DownloadEngine* engine,
                  TestPeerCommandState* state = nullptr)
      : PeerAbstractCommand(1, peer, engine), state_(state)
  {
  }

private:
  bool prepareForNextPeer(time_t wait) override
  {
    retried = true;
    if (state_) {
      state_->retried = true;
    }
    return true;
  }
  bool exitBeforeExecute() override { return false; }
  bool executeInternal() override
  {
    executed = true;
    return true;
  }
  bool onBlocked(bool retry) override
  {
    blocked = true;
    retryRequested = retry;
    if (state_) {
      state_->blocked = true;
      state_->retryRequested = retry;
    }
    return PeerAbstractCommand::onBlocked(retry);
  }

  TestPeerCommandState* state_;
};

} // namespace

void BtPeerBlocklistTest::testStopBlocklistedPeerCommand()
{
  Option option;
  option.put(PREF_BT_TIMEOUT, "180");
  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(&option);
  TestPeerCommand command(std::make_shared<Peer>("203.0.113.25", 6881),
                          &engine);
  std::istringstream input("203.0.113.0/24\n");
  engine.getBtRegistry()->getPeerBlocklist()->load(input, "memory");

  REQUIRE(command.execute());
  REQUIRE(command.blocked);
  REQUIRE(!command.executed);
  REQUIRE(command.retryRequested);
  REQUIRE(command.retried);
}

void BtPeerBlocklistTest::testDisconnectBlockedPeerCommands()
{
  Option option;
  option.put(PREF_BT_TIMEOUT, "180");
  DownloadEngine engine(make_unique<SelectEventPoll>());
  engine.setOption(&option);
  TestPeerCommandState state;
  auto command =
      make_unique<TestPeerCommand>(std::make_shared<Peer>("203.0.113.25", 6881),
                                   &engine, &state);
  engine.addCommand(std::move(command));

  REQUIRE(engine.getBtRegistry()->getPeerBlocklist()->replace(
      {"203.0.113.0/24"}, "RPC"));
  REQUIRE_EQ((size_t)1, engine.disconnectBlockedBtPeers());
  REQUIRE(state.blocked);
  REQUIRE(!state.retryRequested);
  REQUIRE(!state.retried);
  REQUIRE_EQ((size_t)0, engine.disconnectBlockedBtPeers());
}

} // namespace aria2
