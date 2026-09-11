/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include <cstddef>
#include "support/Text.h"
#include "support/Encoding.h"
#include "support/ContentDisposition.h"
#include "a2functional.h"
#include <cstring>
#include <string>
#include <cassert>
#include "a2doctest.h"
#include "TestUtil.h"

namespace aria2 {

TEST_CASE("UtilTest1.testGetContentDispositionFilename")
{
  std::string val;

  val = "attachment; filename=\"aria2.tar.bz2\"";
  REQUIRE_EQ(std::string("aria2.tar.bz2"),
             util::getContentDispositionFilename(val, false));

  val = "attachment; filename=\"aria2.tar.bz2\";";
  REQUIRE_EQ(std::string("aria2.tar.bz2"),
             util::getContentDispositionFilename(val, false));

  val = "attachment; filename=aria2.tar.bz2;";
  REQUIRE_EQ(std::string("aria2.tar.bz2"),
             util::getContentDispositionFilename(val, false));

  val = "attachment; filename=\"\"";
  REQUIRE_EQ(std::string(""), util::getContentDispositionFilename(val, false));

  val = "attachment; filename=\"";
  REQUIRE_EQ(std::string(""), util::getContentDispositionFilename(val, false));

  val = "attachment; filename= \" aria2.tar.bz2 \"";
  REQUIRE_EQ(std::string(" aria2.tar.bz2 "),
             util::getContentDispositionFilename(val, false));

  val = "attachment; filename=dir/file";
  REQUIRE_EQ(std::string(""), util::getContentDispositionFilename(val, false));

  val = "attachment; filename=dir\\file";
  REQUIRE_EQ(std::string(""), util::getContentDispositionFilename(val, false));

  val = "attachment; filename=\"dir/file\"";
  REQUIRE_EQ(std::string(""), util::getContentDispositionFilename(val, false));

  val = "attachment; filename=\"dir\\\\file\"";
  REQUIRE_EQ(std::string(""), util::getContentDispositionFilename(val, false));

  val = "attachment; filename=\"/etc/passwd\"";
  REQUIRE_EQ(std::string(""), util::getContentDispositionFilename(val, false));

  val = "attachment; filename=\"..\"";
  REQUIRE_EQ(std::string(""), util::getContentDispositionFilename(val, false));

  val = "attachment; filename=..";
  REQUIRE_EQ(std::string(""), util::getContentDispositionFilename(val, false));

  // Unescaping %2E%2E%2F produces "../". But since we won't unescape,
  // we just accept it as is.
  val = "attachment; filename=\"%2E%2E%2Ffoo.html\"";
  REQUIRE_EQ(std::string("%2E%2E%2Ffoo.html"),
             util::getContentDispositionFilename(val, false));

  // iso-8859-1 string will be converted to utf-8.
  val = "attachment; filename*=iso-8859-1''foo-%E4.html";
  REQUIRE_EQ(std::string("foo-ä.html"),
             util::getContentDispositionFilename(val, false));

  val = "attachment; filename*= UTF-8''foo-%c3%a4.html";
  REQUIRE_EQ(std::string("foo-ä.html"),
             util::getContentDispositionFilename(val, false));

  // iso-8859-1 string will be converted to utf-8.
  val = "attachment; filename=\"foo-%E4.html\"";
  val = util::percentDecode(val.begin(), val.end());
  REQUIRE_EQ(std::string("foo-ä.html"),
             util::getContentDispositionFilename(val, false));

  // allow utf-8 in filename if default_utf8 is set.
  val = "attachment; filename=\"foo-ä.html\"";
  REQUIRE_EQ(std::string("foo-ä.html"),
             util::getContentDispositionFilename(val, true));

  // return empty if default_utf8 is set but invalid utf8.
  val = "attachment; filename=\"foo-\xc2\x02.html\"";
  REQUIRE_EQ(std::string(""), util::getContentDispositionFilename(val, true));
}

TEST_CASE("UtilTest1.testParseContentDisposition1")
{
  char dest[1_k];
  size_t destlen = sizeof(dest);
  const char* cs;
  size_t cslen;
  std::string val;

  // test cases from http://greenbytes.de/tech/tc2231/
  // inlonly
  val = "inline";
  REQUIRE_EQ((ssize_t)0,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // inlonlyquoted
  val = "\"inline\"";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // inlwithasciifilename
  val = "inline; filename=\"foo.html\"";
  REQUIRE_EQ((ssize_t)8,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("foo.html"), std::string(&dest[0], &dest[8]));

  // inlwithfnattach
  val = "inline; filename=\"Not an attachment!\"";
  REQUIRE_EQ((ssize_t)18,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("Not an attachment!"),
             std::string(&dest[0], &dest[18]));

  // inlwithasciifilenamepdf
  val = "inline; filename=\"foo.pdf\"";
  REQUIRE_EQ((ssize_t)7,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("foo.pdf"), std::string(&dest[0], &dest[7]));

  // attwithasciifilename25
  val = "attachment; filename=\"0000000000111111111122222\"";
  REQUIRE_EQ((ssize_t)25,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("0000000000111111111122222"),
             std::string(&dest[0], &dest[25]));

  // attwithasciifilename35
  val = "attachment; filename=\"00000000001111111111222222222233333\"";
  REQUIRE_EQ((ssize_t)35,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("00000000001111111111222222222233333"),
             std::string(&dest[0], &dest[35]));

  // attwithasciifnescapedchar
  val = "attachment; filename=\"f\\oo.html\"";
  REQUIRE_EQ((ssize_t)8,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("foo.html"), std::string(&dest[0], &dest[8]));

  // attwithasciifnescapedquote
  val = "attachment; filename=\"\\\"quoting\\\" tested.html\"";
  REQUIRE_EQ((ssize_t)21,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("\"quoting\" tested.html"),
             std::string(&dest[0], &dest[21]));

  // attwithquotedsemicolon
  val = "attachment; filename=\"Here's a semicolon;.html\"";
  REQUIRE_EQ((ssize_t)24,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("Here's a semicolon;.html"),
             std::string(&dest[0], &dest[24]));

  // attwithfilenameandextparam
  val = "attachment; foo=\"bar\"; filename=\"foo.html\"";
  REQUIRE_EQ((ssize_t)8,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("foo.html"), std::string(&dest[0], &dest[8]));

  // attwithfilenameandextparamescaped
  val = "attachment; foo=\"\\\"\\\\\";filename=\"foo.html\"";
  REQUIRE_EQ((ssize_t)8,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("foo.html"), std::string(&dest[0], &dest[8]));

  // attwithasciifilenameucase
  val = "attachment; FILENAME=\"foo.html\"";
  REQUIRE_EQ((ssize_t)8,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("foo.html"), std::string(&dest[0], &dest[8]));

  // attwithasciifilenamenq
  val = "attachment; filename=foo.html";
  REQUIRE_EQ((ssize_t)8,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("foo.html"), std::string(&dest[0], &dest[8]));

  // attwithtokfncommanq
  val = "attachment; filename=foo,bar.html";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attwithasciifilenamenqs
  val = "attachment; filename=foo.html ;";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attemptyparam
  val = "attachment; ;filename=foo";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attwithasciifilenamenqws
  val = "attachment; filename=foo bar.html";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attwithfntokensq
  val = "attachment; filename='foo.bar'";
  REQUIRE_EQ((ssize_t)9,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("'foo.bar'"), std::string(&dest[0], &dest[9]));

  // attwithisofnplain
  // attachment; filename="foo-ä.html"
  val = "attachment; filename=\"foo-%E4.html\"";
  val = util::percentDecode(val.begin(), val.end());
  REQUIRE_EQ((ssize_t)10,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("foo-ä.html"),
             util::iso8859p1ToUtf8(std::string(&dest[0], &dest[10])));

  // attwithutf8fnplain
  // attachment; filename="foo-Ã¤.html"
  val = "attachment; filename=\"foo-%C3%A4.html\"";
  val = util::percentDecode(val.begin(), val.end());
  REQUIRE_EQ((ssize_t)11,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("foo-Ã¤.html"),
             util::iso8859p1ToUtf8(std::string(&dest[0], &dest[11])));

  // attwithfnrawpctenca
  val = "attachment; filename=\"foo-%41.html\"";
  REQUIRE_EQ((ssize_t)12,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("foo-%41.html"), std::string(&dest[0], &dest[12]));

  // attwithfnusingpct
  val = "attachment; filename=\"50%.html\"";
  REQUIRE_EQ((ssize_t)8,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("50%.html"), std::string(&dest[0], &dest[8]));

  // attwithfnrawpctencaq
  val = "attachment; filename=\"foo-%\\41.html\"";
  REQUIRE_EQ((ssize_t)12,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("foo-%41.html"), std::string(&dest[0], &dest[12]));

  // attwithnamepct
  val = "attachment; name=\"foo-%41.html\"";
  REQUIRE_EQ((ssize_t)0,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attwithfilenamepctandiso
  // attachment; filename="ä-%41.html"
  val = "attachment; filename=\"%E4-%2541.html\"";
  val = util::percentDecode(val.begin(), val.end());
  REQUIRE_EQ((ssize_t)10,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("ä-%41.html"),
             util::iso8859p1ToUtf8(std::string(&dest[0], &dest[10])));

  // attwithfnrawpctenclong
  val = "attachment; filename=\"foo-%c3%a4-%e2%82%ac.html\"";
  REQUIRE_EQ((ssize_t)25,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("foo-%c3%a4-%e2%82%ac.html"),
             std::string(&dest[0], &dest[25]));

  // attwithasciifilenamews1
  val = "attachment; filename =\"foo.html\"";
  REQUIRE_EQ((ssize_t)8,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("foo.html"), std::string(&dest[0], &dest[8]));

  // attwith2filenames
  val = "attachment; filename=\"foo.html\"; filename=\"bar.html\"";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attfnbrokentoken
  val = "attachment; filename=foo[1](2).html";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attfnbrokentokeniso
  val = "attachment; filename=foo-%E4.html";
  val = util::percentDecode(val.begin(), val.end());
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attfnbrokentokenutf
  // attachment; filename=foo-Ã¤.html
  val = "attachment; filename=foo-ä.html";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attmissingdisposition
  val = "filename=foo.html";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attmissingdisposition2
  val = "x=y; filename=foo.html";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attmissingdisposition3
  val = "\"foo; filename=bar;baz\"; filename=qux";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attmissingdisposition4
  val = "filename=foo.html, filename=bar.html";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // emptydisposition
  val = "; filename=foo.html";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // doublecolon
  val = ": inline; attachment; filename=foo.html";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attandinline
  val = "inline; attachment; filename=foo.html";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attandinline2
  val = "attachment; inline; filename=foo.html";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attbrokenquotedfn
  val = "attachment; filename=\"foo.html\".txt";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attbrokenquotedfn2
  val = "attachment; filename=\"bar";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attbrokenquotedfn3
  val = "attachment; filename=foo\"bar;baz\"qux";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attmultinstances
  val = "attachment; filename=foo.html, attachment; filename=bar.html";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
}

} // namespace aria2
