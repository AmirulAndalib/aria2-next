#include "TimeA2.h"
#include "ServerStat.h"

#include "a2doctest.h"

namespace aria2 {

TEST_CASE("ServerStatTest.testSetStatus")
{
  ServerStat ss("localhost", "http");
  REQUIRE_EQ(ServerStat::OK, ss.getStatus());
  ss.setStatus("ERROR");
  REQUIRE_EQ(ServerStat::A2_ERROR, ss.getStatus());
  // See undefined status string will not change current status.
  ss.setStatus("__BADSTATUS");
  REQUIRE_EQ(ServerStat::A2_ERROR, ss.getStatus());
  ss.setStatus("OK");
  REQUIRE_EQ(ServerStat::OK, ss.getStatus());
  // See undefined status string will not change current status.
  ss.setStatus("__BADSTATUS");
  REQUIRE_EQ(ServerStat::OK, ss.getStatus());
}

TEST_CASE("ServerStatTest.testToString")
{
  ServerStat localhost_http("localhost", "http");
  localhost_http.setDownloadSpeed(90000);
  localhost_http.setLastUpdated(Time(1000));
  localhost_http.setSingleConnectionAvgSpeed(101);
  localhost_http.setMultiConnectionAvgSpeed(102);
  localhost_http.setCounter(5);

  REQUIRE_EQ(std::string("host=localhost, protocol=http, dl_speed=90000,"
                         " sc_avg_speed=101, mc_avg_speed=102,"
                         " last_updated=1000, counter=5, status=OK"),
             localhost_http.toString());

  ServerStat localhost_ftp("localhost", "ftp");
  localhost_ftp.setDownloadSpeed(10000);
  localhost_ftp.setLastUpdated(Time(1210000000));
  localhost_ftp.setStatus("ERROR");

  REQUIRE_EQ(std::string("host=localhost, protocol=ftp, dl_speed=10000,"
                         " sc_avg_speed=0, mc_avg_speed=0,"
                         " last_updated=1210000000, counter=0, status=ERROR"),
             localhost_ftp.toString());
}

} // namespace aria2
