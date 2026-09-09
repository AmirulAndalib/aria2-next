#include "UriListParser.h"

#include "a2doctest.h"
#include "Option.h"
#include "prefs.h"
#include "util.h"

namespace aria2 {

TEST_CASE("UriListParser preserves entries and options with LF or CRLF")
{
  for (const std::string newline : {"\n", "\r\n"}) {
    const std::string filename = A2_TEST_OUT_DIR "/uri-list.txt";
    const auto input =
        "# comment" + newline +
        "http://localhost/index.html\thttps://mirror/index.html" + newline +
        newline + "https://localhost/archive.tar" + newline + "  dir=/tmp" +
        newline + "# comment" + newline + "\t out=archive.tar" + newline;
    REQUIRE(util::saveAs(filename, input, true));
    UriListParser parser(filename);
    std::vector<std::string> uris;
    Option options;
    REQUIRE(parser.hasNext());
    parser.parseNext(uris, options);
    const std::vector<std::string> mirrors = {"http://localhost/index.html",
                                              "https://mirror/index.html"};
    REQUIRE(uris == mirrors);
    uris.clear();
    options.clear();
    REQUIRE(parser.hasNext());
    parser.parseNext(uris, options);
    REQUIRE(uris == std::vector<std::string>{"https://localhost/archive.tar"});
    CHECK_EQ("/tmp", options.get(PREF_DIR));
    CHECK_EQ("archive.tar", options.get(PREF_OUT));
    CHECK(!parser.hasNext());
    uris.clear();
    parser.parseNext(uris, options);
    CHECK(uris.empty());
    CHECK(!parser.hasNext());
  }
}

} // namespace aria2
