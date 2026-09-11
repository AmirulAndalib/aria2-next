#include <cstdint>
#include "uri.h"

#include "a2doctest.h"

namespace aria2 {

namespace uri {

TEST_CASE("UriTest.testSetUri1")
{
  UriStruct us;
  bool v = parse(us, "http://aria.rednoah.com/");

  REQUIRE(v);
  REQUIRE_EQ(std::string("http"), us.protocol);
  REQUIRE_EQ((uint16_t)80, us.port);
  REQUIRE_EQ(std::string("aria.rednoah.com"), us.host);
  REQUIRE_EQ(std::string("/"), us.dir);
  REQUIRE_EQ(std::string(""), us.file);
  REQUIRE_EQ(std::string(""), us.query);
  REQUIRE_EQ(std::string(""), us.username);
  REQUIRE_EQ(std::string(""), us.password);
  REQUIRE(!us.ipv6LiteralAddress);
}

TEST_CASE("UriTest.testSetUri2")
{
  UriStruct us;
  bool v = parse(us, "http://aria.rednoah.com:8080/index.html");

  REQUIRE(v);
  REQUIRE_EQ(std::string("http"), us.protocol);
  REQUIRE_EQ((uint16_t)8080, us.port);
  REQUIRE_EQ(std::string("aria.rednoah.com"), us.host);
  REQUIRE_EQ(std::string("/"), us.dir);
  REQUIRE_EQ(std::string("index.html"), us.file);
  REQUIRE_EQ(std::string(""), us.query);
}

TEST_CASE("UriTest.testSetUri3")
{
  UriStruct us;
  bool v = parse(us, "http://aria.rednoah.com/aria2/index.html");

  REQUIRE(v);
  REQUIRE_EQ(std::string("http"), us.protocol);
  REQUIRE_EQ((uint16_t)80, us.port);
  REQUIRE_EQ(std::string("aria.rednoah.com"), us.host);
  REQUIRE_EQ(std::string("/aria2/"), us.dir);
  REQUIRE_EQ(std::string("index.html"), us.file);
  REQUIRE_EQ(std::string(""), us.query);
}

TEST_CASE("UriTest.testSetUri4")
{
  UriStruct us;
  bool v = parse(us, "http://aria.rednoah.com/aria2/aria3/index.html");

  REQUIRE(v);
  REQUIRE_EQ(std::string("http"), us.protocol);
  REQUIRE_EQ((uint16_t)80, us.port);
  REQUIRE_EQ(std::string("aria.rednoah.com"), us.host);
  REQUIRE_EQ(std::string("/aria2/aria3/"), us.dir);
  REQUIRE_EQ(std::string("index.html"), us.file);
  REQUIRE_EQ(std::string(""), us.query);
}

TEST_CASE("UriTest.testSetUri5")
{
  UriStruct us;
  bool v = parse(us, "http://aria.rednoah.com/aria2/aria3/");

  REQUIRE(v);
  REQUIRE_EQ(std::string("http"), us.protocol);
  REQUIRE_EQ((uint16_t)80, us.port);
  REQUIRE_EQ(std::string("aria.rednoah.com"), us.host);
  REQUIRE_EQ(std::string("/aria2/aria3/"), us.dir);
  REQUIRE_EQ(std::string(""), us.file);
  REQUIRE_EQ(std::string(""), us.query);
}

TEST_CASE("UriTest.testSetUri6")
{
  UriStruct us;
  bool v = parse(us, "http://aria.rednoah.com/aria2/aria3");

  REQUIRE(v);
  REQUIRE_EQ(std::string("http"), us.protocol);
  REQUIRE_EQ((uint16_t)80, us.port);
  REQUIRE_EQ(std::string("aria.rednoah.com"), us.host);
  REQUIRE_EQ(std::string("/aria2/"), us.dir);
  REQUIRE_EQ(std::string("aria3"), us.file);
  REQUIRE_EQ(std::string(""), us.query);
}

TEST_CASE("UriTest.testSetUri7")
{
  UriStruct us;
  bool v = parse(us, "http://");

  REQUIRE(!v);
}

TEST_CASE("UriTest.testSetUri8")
{
  UriStruct us;
  bool v = parse(us, "http:/aria.rednoah.com");

  REQUIRE(!v);
}

TEST_CASE("UriTest.testSetUri9")
{
  UriStruct us;
  bool v = parse(us, "h");

  REQUIRE(!v);
}

TEST_CASE("UriTest.testSetUri10")
{
  UriStruct us;
  bool v = parse(us, "");

  REQUIRE(!v);
}

TEST_CASE("UriTest.testSetUri11")
{
  UriStruct us;
  bool v = parse(us, "http://host?query/");

  REQUIRE(v);
  REQUIRE_EQ(std::string("http"), us.protocol);
  REQUIRE_EQ(std::string("host"), us.host);
  REQUIRE_EQ(std::string("/"), us.dir);
  REQUIRE_EQ(std::string(""), us.file);
  REQUIRE_EQ(std::string("?query/"), us.query);
}

TEST_CASE("UriTest.testSetUri12")
{
  UriStruct us;
  bool v = parse(us, "http://host?query");

  REQUIRE(v);
  REQUIRE_EQ(std::string("http"), us.protocol);
  REQUIRE_EQ(std::string("host"), us.host);
  REQUIRE_EQ(std::string("/"), us.dir);
  REQUIRE_EQ(std::string(""), us.file);
  REQUIRE_EQ(std::string("?query"), us.query);
}

TEST_CASE("UriTest.testSetUri13")
{
  UriStruct us;
  bool v = parse(us, "http://host/?query");

  REQUIRE(v);
  REQUIRE_EQ(std::string("http"), us.protocol);
  REQUIRE_EQ(std::string("host"), us.host);
  REQUIRE_EQ(std::string("/"), us.dir);
  REQUIRE_EQ(std::string(""), us.file);
  REQUIRE_EQ(std::string("?query"), us.query);
}

TEST_CASE("UriTest.testSetUri14")
{
  UriStruct us;
  bool v = parse(us, "http://host:8080/abc?query");

  REQUIRE(v);
  REQUIRE_EQ(std::string("http"), us.protocol);
  REQUIRE_EQ(std::string("host"), us.host);
  REQUIRE_EQ((uint16_t)8080, us.port);
  REQUIRE_EQ(std::string("/"), us.dir);
  REQUIRE_EQ(std::string("abc"), us.file);
  REQUIRE_EQ(std::string("?query"), us.query);
}

TEST_CASE("UriTest.testSetUri15")
{
  UriStruct us;
  // 2 slashes after host name and dir
  bool v = parse(us, "http://host//dir1/dir2//file");
  REQUIRE(v);
  REQUIRE_EQ(std::string("http"), us.protocol);
  REQUIRE_EQ(std::string("host"), us.host);
  REQUIRE_EQ(std::string("//dir1/dir2//"), us.dir);
  REQUIRE_EQ(std::string("file"), us.file);
  REQUIRE_EQ(std::string(""), us.query);
}

TEST_CASE("UriTest.testSetUri16")
{
  UriStruct us;
  // 2 slashes before file
  bool v = parse(us, "http://host//file");
  REQUIRE(v);
  REQUIRE_EQ(std::string("http"), us.protocol);
  REQUIRE_EQ(std::string("host"), us.host);
  REQUIRE_EQ(std::string("//"), us.dir);
  REQUIRE_EQ(std::string("file"), us.file);
  REQUIRE_EQ(std::string(""), us.query);
}

TEST_CASE("UriTest.testSetUri18")
{
  UriStruct us;
  bool v = parse(us, "http://1/");

  REQUIRE(v);
}

TEST_CASE("UriTest.testSetUri19")
{
  UriStruct us;
  // No host
  bool v = parse(us, "http://user@");

  REQUIRE(!v);
}

TEST_CASE("UriTest.testSetUri20")
{
  UriStruct us;
  bool v;
  // Invalid port
  v = parse(us, "http://localhost:65536");
  REQUIRE(!v);
  v = parse(us, "http://localhost:65535");
  REQUIRE(v);
  v = parse(us, "http://localhost:-80");
  REQUIRE(!v);
}

TEST_CASE("UriTest.testSetUri_zeroUsername")
{
  UriStruct us;
  REQUIRE(!parse(us, "sftp://@localhost/download/aria2-1.0.0.tar.bz2"));

  REQUIRE(!parse(us, "sftp://:@localhost/download/aria2-1.0.0.tar.bz2"));

  REQUIRE(!parse(us, "sftp://:pass@localhost/download/aria2-1.0.0.tar.bz2"));
}

TEST_CASE("UriTest.testSetUri_username")
{
  UriStruct us;
  REQUIRE(
      parse(us, "sftp://aria2@user@localhost/download/aria2-1.0.0.tar.bz2"));
  REQUIRE_EQ(std::string("sftp"), us.protocol);
  REQUIRE_EQ((uint16_t)22, us.port);
  REQUIRE_EQ(std::string("localhost"), us.host);
  REQUIRE_EQ(std::string("/download/"), us.dir);
  REQUIRE_EQ(std::string("aria2-1.0.0.tar.bz2"), us.file);
  REQUIRE_EQ(std::string("aria2@user"), us.username);
  REQUIRE_EQ(std::string(""), us.password);
}

TEST_CASE("UriTest.testSetUri_usernamePassword")
{
  UriStruct us;
  REQUIRE(parse(us, "sftp://aria2@user%40:aria2@pass%40@localhost/download/"
                    "aria2-1.0.0.tar.bz2"));
  REQUIRE_EQ(std::string("sftp"), us.protocol);
  REQUIRE_EQ((uint16_t)22, us.port);
  REQUIRE_EQ(std::string("pass%40@localhost"), us.host);
  REQUIRE_EQ(std::string("/download/"), us.dir);
  REQUIRE_EQ(std::string("aria2-1.0.0.tar.bz2"), us.file);
  REQUIRE_EQ(std::string("aria2@user@"), us.username);
  REQUIRE_EQ(std::string("aria2"), us.password);

  // make sure that after new uri is set, username and password are updated.
  REQUIRE(parse(us, "sftp://localhost/download/aria2-1.0.0.tar.bz2"));
  REQUIRE_EQ(std::string(""), us.username);
  REQUIRE_EQ(std::string(""), us.password);
}

TEST_CASE("UriTest.testSetUri_ipv6")
{
  UriStruct us;
  REQUIRE(!parse(us, "http://[::1"));
  REQUIRE(parse(us, "http://[::1]"));
  REQUIRE_EQ(std::string("::1"), us.host);

  REQUIRE(parse(us, "http://[::1]:8000/dir/file"));
  REQUIRE_EQ(std::string("::1"), us.host);
  REQUIRE_EQ((uint16_t)8000, us.port);
  REQUIRE_EQ(std::string("/dir/"), us.dir);
  REQUIRE_EQ(std::string("file"), us.file);
  REQUIRE(us.ipv6LiteralAddress);
}

TEST_CASE("UriTest.testInnerLink")
{
  UriStruct us;
  bool v = parse(us, "http://aria.rednoah.com/index.html#download");
  REQUIRE(v);
  REQUIRE_EQ(std::string("index.html"), us.file);
  REQUIRE_EQ(std::string(""), us.query);
}

TEST_CASE("UriTest.testConstruct")
{
  {
    UriStruct us;
    REQUIRE(parse(us, "http://host/dir/file?q=abc#foo"));
    REQUIRE_EQ(std::string("http://host/dir/file?q=abc"), construct(us));
  }
  {
    UriStruct us;
    REQUIRE(parse(us, "http://host/dir/file"));
    REQUIRE_EQ(std::string("http://host/dir/file"), construct(us));
  }
  {
    UriStruct us;
    REQUIRE(parse(us, "http://host/dir/"));
    REQUIRE_EQ(std::string("http://host/dir/"), construct(us));
  }
  {
    UriStruct us;
    REQUIRE(parse(us, "http://host/dir"));
    REQUIRE_EQ(std::string("http://host/dir"), construct(us));
  }
  {
    UriStruct us;
    REQUIRE(parse(us, "http://host/"));
    REQUIRE_EQ(std::string("http://host/"), construct(us));
  }
  {
    UriStruct us;
    REQUIRE(parse(us, "http://host"));
    REQUIRE_EQ(std::string("http://host/"), construct(us));
  }
  {
    UriStruct us;
    us.protocol = "http";
    us.host = "host";
    us.file = "foo.xml";
    REQUIRE_EQ(std::string("http://host/foo.xml"), construct(us));
  }
  {
    UriStruct us;
    REQUIRE(parse(us, "http://host:80"));
    REQUIRE_EQ(std::string("http://host/"), construct(us));
  }
  {
    UriStruct us;
    REQUIRE(parse(us, "http://host:8080"));
    REQUIRE_EQ(std::string("http://host:8080/"), construct(us));
  }
  {
    UriStruct us;
    REQUIRE(parse(us, "http://[::1]:8000/dir/file"));
    REQUIRE_EQ(std::string("http://[::1]:8000/dir/file"), construct(us));
  }
  {
    UriStruct us;
    REQUIRE(parse(us, "sftp://user%40@host/dir/file"));
    REQUIRE_EQ(std::string("sftp://user%40@host/dir/file"), construct(us));
  }
  {
    UriStruct us;
    REQUIRE(parse(us, "sftp://user:@host/dir/file"));
    REQUIRE_EQ(std::string("sftp://user:@host/dir/file"), construct(us));
  }
  {
    UriStruct us;
    REQUIRE(parse(us, "sftp://user:passwd%40@host/dir/file"));
    REQUIRE_EQ(std::string("sftp://user:passwd%40@host/dir/file"),
               construct(us));
  }
}

TEST_CASE("UriTest.testSwap")
{
  UriStruct us1;
  REQUIRE(parse(us1, "http://u1:p1@[::1]/dir1/file1?k1=v1"));
  UriStruct us2;
  REQUIRE(parse(us2, "sftp://host2/dir2/file2?k2=v2"));
  us1.swap(us2);
  REQUIRE_EQ(std::string("sftp://host2/dir2/file2?k2=v2"), construct(us1));
  REQUIRE_EQ(std::string("http://u1:p1@[::1]/dir1/file1?k1=v1"),
             construct(us2));
}

TEST_CASE("UriTest.testJoinUri")
{
  REQUIRE_EQ(std::string("http://host/dir/file"),
             joinUri("http://base/d/f", "http://host/dir/file"));

  REQUIRE_EQ(std::string("http://base/dir/file"),
             joinUri("http://base/d/f", "/dir/file"));

  REQUIRE_EQ(std::string("http://base/d/dir/file"),
             joinUri("http://base/d/f", "dir/file"));

  REQUIRE_EQ(std::string("http://base/d/"), joinUri("http://base/d/f", ""));

  REQUIRE_EQ(std::string("http://base/d/dir/file?q=k"),
             joinUri("http://base/d/f", "dir/file?q=k"));

  REQUIRE_EQ(std::string("dir/file"), joinUri("baduri", "dir/file"));

  REQUIRE_EQ(std::string("http://base/a/b/d/file"),
             joinUri("http://base/a/b/c/x", "../d/file"));

  REQUIRE_EQ(std::string("http://base/a/b/file"),
             joinUri("http://base/c/x", "../../a/b/file"));

  REQUIRE_EQ(std::string("http://base/"), joinUri("http://base/c/x", "../.."));

  REQUIRE_EQ(std::string("http://base/"), joinUri("http://base/c/x", ".."));

  REQUIRE_EQ(std::string("http://base/a/file"),
             joinUri("http://base/b/c/x", "/a/x/../file"));

  REQUIRE_EQ(std::string("http://base/file"),
             joinUri("http://base/f/?q=k", "/file"));

  REQUIRE_EQ(std::string("http://base/file?q=/"),
             joinUri("http://base/", "/file?q=/"));

  REQUIRE_EQ(std::string("http://base/file?q=v"),
             joinUri("http://base/", "/file?q=v#a?q=x"));

  REQUIRE_EQ(std::string("http://base/file"),
             joinUri("http://base/", "/file#a?q=x"));
}

TEST_CASE("UriTest.testJoinPath")
{
  REQUIRE_EQ(std::string("/b"), joinPath("/a", "/b"));
  REQUIRE_EQ(std::string("/alpha/bravo"), joinPath("/alpha", "bravo"));
  REQUIRE_EQ(std::string("/bravo"), joinPath("/a", "/alpha/../bravo"));
  REQUIRE_EQ(std::string("/alpha/charlie/"),
             joinPath("/a", "/alpha/bravo/../charlie/"));
  REQUIRE_EQ(std::string("/alpha/bravo/"), joinPath("/a", "/alpha////bravo//"));
  REQUIRE_EQ(std::string("/alpha/bravo/"), joinPath("/a", "/alpha/././bravo/"));
  REQUIRE_EQ(std::string("/alpha/bravo/"), joinPath("/a", "/alpha/bravo/./"));
  REQUIRE_EQ(std::string("/alpha/bravo/"), joinPath("/a", "/alpha/bravo/."));
  REQUIRE_EQ(std::string("/alpha/"), joinPath("/a", "/alpha/bravo/.."));
  REQUIRE_EQ(std::string("/alpha/"), joinPath("/", "../alpha/"));
  REQUIRE_EQ(std::string("/bravo/"), joinPath("/alpha", "../bravo/"));
  REQUIRE_EQ(std::string("/bravo/"), joinPath("/alpha", "../../bravo/"));
  // If neither paths do not start with '/', the resulting path also
  // does not start with '/'.
  REQUIRE_EQ(std::string("alpha/bravo"), joinPath("alpha", "bravo"));
  REQUIRE_EQ(std::string("bravo/"), joinPath("alpha", "../../bravo/"));
}

} // namespace uri

} // namespace aria2
