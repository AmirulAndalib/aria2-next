#include "SocketCore.h"

#include <cstring>
#include <iostream>
#include "a2doctest.h"

#include "a2functional.h"
#include "Exception.h"

namespace aria2 {

class SocketCoreTest {


public:
  void setUp() {}

  void tearDown() {}

  void testWriteAndReadDatagram();
  void testGetSocketError();
  void testInetNtop();
  void testInetPton();
  void testGetBinAddr();
};

A2_TEST(SocketCoreTest, testWriteAndReadDatagram)
A2_TEST(SocketCoreTest, testGetSocketError)
A2_TEST(SocketCoreTest, testInetNtop)
A2_TEST(SocketCoreTest, testInetPton)
A2_TEST(SocketCoreTest, testGetBinAddr)

void SocketCoreTest::testWriteAndReadDatagram()
{
  try {
    SocketCore s(SOCK_DGRAM);
    s.bind(0);
    SocketCore c(SOCK_DGRAM);
    c.bind(0);

    auto remoteEndpoint = s.getAddrInfo();

    std::string message1 = "hello world.";
    c.writeData(message1.c_str(), message1.size(), "localhost",
                remoteEndpoint.port);
    std::string message2 = "chocolate coated pie";
    c.writeData(message2.c_str(), message2.size(), "localhost",
                remoteEndpoint.port);

    char readbuffer[100];

    {
      ssize_t rlength =
          s.readDataFrom(readbuffer, sizeof(readbuffer), remoteEndpoint);
      // commented out because ip address may vary
      // REQUIRE_EQ(std::std::string("127.0.0.1"),
      //                      remoteEndpoint.addr);
      REQUIRE_EQ((ssize_t)message1.size(), rlength);
      readbuffer[rlength] = '\0';
      REQUIRE_EQ(message1, std::string(readbuffer));
    }
    {
      ssize_t rlength =
          s.readDataFrom(readbuffer, sizeof(readbuffer), remoteEndpoint);
      REQUIRE_EQ((ssize_t)message2.size(), rlength);
      readbuffer[rlength] = '\0';
      REQUIRE_EQ(message2, std::string(readbuffer));
    }
  }
  catch (Exception& e) {
    std::cerr << e.stackTrace() << std::endl;
    FAIL("exception thrown");
  }
}

void SocketCoreTest::testGetSocketError()
{
  SocketCore s;
  s.bind(0);
  // See there is no error at this point
  REQUIRE_EQ(std::string(""), s.getSocketError());
}

void SocketCoreTest::testInetNtop()
{
  char dest[NI_MAXHOST];
  {
    std::string s = "192.168.0.1";
    addrinfo* res;
    REQUIRE_EQ(0, callGetaddrinfo(&res, s.c_str(), nullptr, AF_INET,
                                            SOCK_STREAM, 0, 0));
    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> resDeleter(res,
                                                                  freeaddrinfo);
    sockaddr_in addr;
    memcpy(&addr, res->ai_addr, sizeof(addr));
    REQUIRE_EQ(0,
                         inetNtop(AF_INET, &addr.sin_addr, dest, sizeof(dest)));
    REQUIRE_EQ(s, std::string(dest));
  }
  {
    std::string s = "2001:db8::2:1";
    addrinfo* res;
    REQUIRE_EQ(0, callGetaddrinfo(&res, s.c_str(), nullptr, AF_INET6,
                                            SOCK_STREAM, 0, 0));
    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> resDeleter(res,
                                                                  freeaddrinfo);
    sockaddr_in6 addr;
    memcpy(&addr, res->ai_addr, sizeof(addr));
    REQUIRE_EQ(
        0, inetNtop(AF_INET6, &addr.sin6_addr, dest, sizeof(dest)));
    REQUIRE_EQ(s, std::string(dest));
  }
}

void SocketCoreTest::testInetPton()
{
  {
    const char ipaddr[] = "192.168.0.1";
    in_addr ans;
    REQUIRE_EQ((size_t)4, net::getBinAddr(&ans, ipaddr));
    in_addr dest;
    REQUIRE_EQ(0, inetPton(AF_INET, ipaddr, &dest));
    REQUIRE(memcmp(&ans, &dest, sizeof(ans)) == 0);
  }
  {
    const char ipaddr[] = "2001:db8::2:1";
    in6_addr ans;
    REQUIRE_EQ((size_t)16, net::getBinAddr(&ans, ipaddr));
    in6_addr dest;
    REQUIRE_EQ(0, inetPton(AF_INET6, ipaddr, &dest));
    REQUIRE(memcmp(&ans, &dest, sizeof(ans)) == 0);
  }
  unsigned char dest[16];
  REQUIRE_EQ(-1, inetPton(AF_INET, "localhost", &dest));
  REQUIRE_EQ(-1, inetPton(AF_INET6, "localhost", &dest));
}

void SocketCoreTest::testGetBinAddr()
{
  struct DefaultAIFlagsReset {
    ~DefaultAIFlagsReset() { setDefaultAIFlags(0); }
  } reset;
  setDefaultAIFlags(AI_ADDRCONFIG);

  unsigned char dest[16];
  unsigned char ans1[] = {192, 168, 0, 1};
  REQUIRE_EQ((size_t)4, net::getBinAddr(dest, "192.168.0.1"));
  REQUIRE(std::equal(&dest[0], &dest[4], &ans1[0]));

  unsigned char ans2[] = {0x20u, 0x01u, 0x0du, 0xb8u, 0x00u, 0x00u,
                          0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
                          0x00u, 0x02u, 0x00u, 0x01u};
  REQUIRE_EQ((size_t)16, net::getBinAddr(dest, "2001:db8::2:1"));
  REQUIRE(std::equal(&dest[0], &dest[16], &ans2[0]));

  unsigned char ans3[] = {0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
                          0x00u, 0x00u, 0x00u, 0x00u, 0xffu, 0xffu,
                          0xc0u, 0x00u, 0x02u, 0x01u};
  REQUIRE_EQ((size_t)16, net::getBinAddr(dest, "::ffff:192.0.2.1"));
  REQUIRE(std::equal(&dest[0], &dest[16], &ans3[0]));

  REQUIRE_EQ((size_t)0, net::getBinAddr(dest, "localhost"));
}

} // namespace aria2
