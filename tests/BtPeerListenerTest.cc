#include "BtPeerListener.h"

#include "a2doctest.h"

#include "SocketCore.h"
#include "util.h"

namespace aria2 {

namespace {

uint16_t availablePort()
{
  SocketCore socket;
  socket.bind(nullptr, 0, AF_INET);
  socket.beginListen();
  return socket.getAddrInfo().port;
}

} // namespace

TEST_CASE("BtPeerListener replaces sockets only after a successful bind")
{
  BtPeerListener listener;
  const auto firstPort = availablePort();
  REQUIRE(listener.rebind(util::uitos(firstPort), false));
  REQUIRE(listener.active());
  REQUIRE_EQ(firstPort, listener.port());

  SocketCore blocker;
  blocker.bind(nullptr, 0, AF_INET);
  blocker.beginListen();
  const auto blockedPort = blocker.getAddrInfo().port;
  REQUIRE_FALSE(listener.rebind(util::uitos(blockedPort), false));
  REQUIRE_EQ(firstPort, listener.port());

  auto secondPort = availablePort();
  while (secondPort == firstPort || secondPort == blockedPort) {
    secondPort = availablePort();
  }
  REQUIRE(listener.rebind(util::uitos(secondPort), false));
  REQUIRE_EQ(secondPort, listener.port());
}

} // namespace aria2
