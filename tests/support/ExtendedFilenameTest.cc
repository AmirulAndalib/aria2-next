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

TEST_CASE("UtilTest1.testParseContentDisposition2")
{
  char dest[1_k];
  size_t destlen = sizeof(dest);
  const char* cs;
  size_t cslen;
  std::string val;

  // test cases from http://greenbytes.de/tech/tc2231/
  // attmissingdelim
  val = "attachment; foo=foo filename=bar";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attmissingdelim2
  val = "attachment; filename=bar foo=foo ";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attmissingdelim3
  val = "attachment filename=bar";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attreversed
  val = "filename=foo.html; attachment";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attconfusedparam
  val = "attachment; xfilename=foo.html";
  REQUIRE_EQ((ssize_t)0,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attabspath
  val = "attachment; filename=\"/foo.html\"";
  REQUIRE_EQ((ssize_t)9,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("/foo.html"), std::string(&dest[0], &dest[9]));

  // attabspathwin
  val = "attachment; filename=\"\\\\foo.html\"";
  REQUIRE_EQ((ssize_t)9,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("\\foo.html"), std::string(&dest[0], &dest[9]));

  // attcdate
  val = "attachment; creation-date=\"Wed, 12 Feb 1997 16:29:51 -0500\"";
  REQUIRE_EQ((ssize_t)0,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // dispext
  val = "foobar";
  REQUIRE_EQ((ssize_t)0,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // dispextbadfn
  val = "attachment; example=\"filename=example.txt\"";
  REQUIRE_EQ((ssize_t)0,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attwithisofn2231iso
  val = "attachment; filename*=iso-8859-1''foo-%E4.html";
  REQUIRE_EQ((ssize_t)10,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("iso-8859-1"), std::string(cs, cslen));
  REQUIRE_EQ(std::string("foo-ä.html"),
             util::iso8859p1ToUtf8(std::string(&dest[0], &dest[10])));

  // attwithfn2231utf8
  val = "attachment; filename*=UTF-8''foo-%c3%a4-%e2%82%ac.html";
  REQUIRE_EQ((ssize_t)15,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("UTF-8"), std::string(cs, cslen));
  REQUIRE_EQ(std::string("foo-ä-€.html"), std::string(&dest[0], &dest[15]));

  // attwithfn2231noc
  val = "attachment; filename*=''foo-%c3%a4-%e2%82%ac.html";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attwithfn2231utf8comp
  val = "attachment; filename*=UTF-8''foo-a%cc%88.html";
  REQUIRE_EQ((ssize_t)12,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  val = "foo-a%cc%88.html";
  REQUIRE_EQ(std::string(util::percentDecode(val.begin(), val.end())),
             std::string(&dest[0], &dest[12]));

  // attwithfn2231utf8-bad
  val = "attachment; filename*=iso-8859-1''foo-%c3%a4-%e2%82%ac.html";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attwithfn2231iso-bad
  val = "attachment; filename*=utf-8''foo-%E4.html";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attwithfn2231ws1
  val = "attachment; filename *=UTF-8''foo-%c3%a4.html";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attwithfn2231ws2
  val = "attachment; filename*= UTF-8''foo-%c3%a4.html";
  REQUIRE_EQ((ssize_t)11,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("foo-ä.html"), std::string(&dest[0], &dest[11]));

  // attwithfn2231ws3
  val = "attachment; filename* =UTF-8''foo-%c3%a4.html";
  REQUIRE_EQ((ssize_t)11,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("foo-ä.html"), std::string(&dest[0], &dest[11]));

  // attwithfn2231quot
  val = "attachment; filename*=\"UTF-8''foo-%c3%a4.html\"";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attwithfn2231quot2
  val = "attachment; filename*=\"foo%20bar.html\"";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attwithfn2231singleqmissing
  val = "attachment; filename*=UTF-8'foo-%c3%a4.html";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attwithfn2231nbadpct1
  val = "attachment; filename*=UTF-8''foo%";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attwithfn2231nbadpct2
  val = "attachment; filename*=UTF-8''f%oo.html";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attwithfn2231dpct
  val = "attachment; filename*=UTF-8''A-%2541.html";
  REQUIRE_EQ((ssize_t)10,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("A-%41.html"), std::string(&dest[0], &dest[10]));

  // attwithfn2231abspathdisguised
  val = "attachment; filename*=UTF-8''%5cfoo.html";
  REQUIRE_EQ((ssize_t)9,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("\\foo.html"), std::string(&dest[0], &dest[9]));

  // attfnboth
  val =
      "attachment; filename=\"foo-ae.html\"; filename*=UTF-8''foo-%c3%a4.html";
  REQUIRE_EQ((ssize_t)11,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("foo-ä.html"), std::string(&dest[0], &dest[11]));

  // attfnboth2
  val =
      "attachment; filename*=UTF-8''foo-%c3%a4.html; filename=\"foo-ae.html\"";
  REQUIRE_EQ((ssize_t)11,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("foo-ä.html"), std::string(&dest[0], &dest[11]));

  // attfnboth3
  val = "attachment; filename*0*=ISO-8859-15''euro-sign%3d%a4; "
        "filename*=ISO-8859-1''currency-sign%3d%a4";
  REQUIRE_EQ((ssize_t)15,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("ISO-8859-1"), std::string(cs, cslen));
  REQUIRE_EQ(std::string("currency-sign=¤"),
             util::iso8859p1ToUtf8(std::string(&dest[0], &dest[15])));

  // attnewandfn
  val = "attachment; foobar=x; filename=\"foo.html\"";
  REQUIRE_EQ((ssize_t)8,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("foo.html"), std::string(&dest[0], &dest[8]));

  // attrfc2047token
  val = "attachment; filename==?ISO-8859-1?Q?foo-=E4.html?=";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // attrfc2047quoted
  val = "attachment; filename=\"=?ISO-8859-1?Q?foo-=E4.html?=\"";
  REQUIRE_EQ((ssize_t)29,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("=?ISO-8859-1?Q?foo-=E4.html?="),
             std::string(&dest[0], &dest[29]));

  // aria2 original testcases

  // zero-length filename. token cannot be empty, so this is invalid.
  val = "attachment; filename=";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // zero-length filename. quoted-string can be empty string, so this
  // is ok.
  val = "attachment; filename=\"\"";
  REQUIRE_EQ((ssize_t)0,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // empty value is not allowed
  val = "attachment; filename=;";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // / is not valid char in token.
  val = "attachment; filename=dir/file";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));

  // value-chars is *(pct-encoded / attr-char), so empty string is
  // allowed.
  val = "attachment; filename*=UTF-8''";
  REQUIRE_EQ((ssize_t)0,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("UTF-8"), std::string(cs, cslen));

  val = "attachment; filename*=UTF-8''; filename=foo";
  REQUIRE_EQ((ssize_t)0,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("UTF-8"), std::string(cs, cslen));

  val = "attachment; filename*=UTF-8''  ; filename=foo";
  REQUIRE_EQ((ssize_t)0,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("UTF-8"), std::string(cs, cslen));

  // with language
  val = "attachment; filename*=UTF-8'japanese'konnichiwa";
  REQUIRE_EQ((ssize_t)10,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("konnichiwa"), std::string(&dest[0], &dest[10]));

  // lws before and after "="
  val = "attachment; filename = foo.html";
  REQUIRE_EQ((ssize_t)8,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("foo.html"), std::string(&dest[0], &dest[8]));

  // lws before and after "=" with quoted-string
  val = "attachment; filename = \"foo.html\"";
  REQUIRE_EQ((ssize_t)8,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("foo.html"), std::string(&dest[0], &dest[8]));

  // lws after parm
  val = "attachment; filename=foo.html  ";
  REQUIRE_EQ((ssize_t)8,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("foo.html"), std::string(&dest[0], &dest[8]));

  val = "attachment; filename=foo.html ; hello=world";
  REQUIRE_EQ((ssize_t)8,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("foo.html"), std::string(&dest[0], &dest[8]));

  val = "attachment; filename=\"foo.html\"  ";
  REQUIRE_EQ((ssize_t)8,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("foo.html"), std::string(&dest[0], &dest[8]));

  val = "attachment; filename=\"foo.html\" ; hello=world";
  REQUIRE_EQ((ssize_t)8,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("foo.html"), std::string(&dest[0], &dest[8]));

  val = "attachment; filename*=UTF-8''foo.html  ; hello=world";
  REQUIRE_EQ((ssize_t)8,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), false));
  REQUIRE_EQ(std::string("foo.html"), std::string(&dest[0], &dest[8]));

  // allow utf8 if content-disposition-default-utf8 is set
  val = "attachment; filename=\"foo-ä.html\"";
  REQUIRE_EQ((ssize_t)11,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), true));
  REQUIRE_EQ(std::string("foo-ä.html"), std::string(&dest[0], &dest[11]));

  // incomplete utf8 sequence must be rejected
  val = "attachment; filename=\"foo-\xc3.html\"";
  REQUIRE_EQ((ssize_t)-1,
             util::parse_content_disposition(dest, destlen, &cs, &cslen,
                                             val.c_str(), val.size(), true));
}

} // namespace aria2
