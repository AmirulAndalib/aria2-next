/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include <iterator>
#include <vector>
#include "support/Text.h"
#include "a2iterator.h"
#include <cstring>
#include <string>
#include <cassert>
#include "a2doctest.h"
#include "TestUtil.h"
#include <array>

namespace aria2 {

TEST_CASE("UtilTest1.testDivide")
{
  std::string s = "name=value";
  auto p1 = util::divide(std::begin(s), std::end(s), '=');
  REQUIRE_EQ(std::string("name"), std::string(p1.first.first, p1.first.second));
  REQUIRE_EQ(std::string("value"),
             std::string(p1.second.first, p1.second.second));
  s = " name = value ";
  p1 = util::divide(std::begin(s), std::end(s), '=');
  REQUIRE_EQ(std::string("name"), std::string(p1.first.first, p1.first.second));
  REQUIRE_EQ(std::string("value"),
             std::string(p1.second.first, p1.second.second));
  s = "=value";
  p1 = util::divide(std::begin(s), std::end(s), '=');
  REQUIRE_EQ(std::string(""), std::string(p1.first.first, p1.first.second));
  REQUIRE_EQ(std::string("value"),
             std::string(p1.second.first, p1.second.second));
  s = "name=";
  p1 = util::divide(std::begin(s), std::end(s), '=');
  REQUIRE_EQ(std::string("name"), std::string(p1.first.first, p1.first.second));
  REQUIRE_EQ(std::string(""), std::string(p1.second.first, p1.second.second));
  s = "name";
  p1 = util::divide(std::begin(s), std::end(s), '=');
  REQUIRE_EQ(std::string("name"), std::string(p1.first.first, p1.first.second));
  REQUIRE_EQ(std::string(""), std::string(p1.second.first, p1.second.second));
}

TEST_CASE("UtilTest1.testSplit")
{
  std::vector<std::string> v;
  std::string s = "k1; k2;; k3";
  util::split(s.begin(), s.end(), std::back_inserter(v), ';', true);
  REQUIRE_EQ((size_t)3, v.size());
  std::vector<std::string>::iterator itr = v.begin();
  REQUIRE_EQ(std::string("k1"), *itr++);
  REQUIRE_EQ(std::string("k2"), *itr++);
  REQUIRE_EQ(std::string("k3"), *itr++);

  v.clear();

  s = "k1; k2; k3";
  util::split(s.begin(), s.end(), std::back_inserter(v), ';');
  REQUIRE_EQ((size_t)3, v.size());
  itr = v.begin();
  REQUIRE_EQ(std::string("k1"), *itr++);
  REQUIRE_EQ(std::string(" k2"), *itr++);
  REQUIRE_EQ(std::string(" k3"), *itr++);

  v.clear();

  s = "k=v";
  util::split(s.begin(), s.end(), std::back_inserter(v), ';', false, true);
  REQUIRE_EQ((size_t)1, v.size());
  itr = v.begin();
  REQUIRE_EQ(std::string("k=v"), *itr++);

  v.clear();

  s = ";;k1;;k2;";
  util::split(s.begin(), s.end(), std::back_inserter(v), ';', false, true);
  REQUIRE_EQ((size_t)6, v.size());
  itr = v.begin();
  REQUIRE_EQ(std::string(""), *itr++);
  REQUIRE_EQ(std::string(""), *itr++);
  REQUIRE_EQ(std::string("k1"), *itr++);
  REQUIRE_EQ(std::string(""), *itr++);
  REQUIRE_EQ(std::string("k2"), *itr++);
  REQUIRE_EQ(std::string(""), *itr++);

  v.clear();

  s = ";;k1;;k2;";
  util::split(s.begin(), s.end(), std::back_inserter(v), ';');
  REQUIRE_EQ((size_t)2, v.size());
  itr = v.begin();
  REQUIRE_EQ(std::string("k1"), *itr++);
  REQUIRE_EQ(std::string("k2"), *itr++);

  v.clear();

  s = "k; ";
  util::split(s.begin(), s.end(), std::back_inserter(v), ';');
  REQUIRE_EQ((size_t)2, v.size());
  itr = v.begin();
  REQUIRE_EQ(std::string("k"), *itr++);
  REQUIRE_EQ(std::string(" "), *itr++);

  v.clear();

  s = " ";
  util::split(s.begin(), s.end(), std::back_inserter(v), ';', true, true);
  REQUIRE_EQ((size_t)1, v.size());
  REQUIRE_EQ(std::string(""), v[0]);

  v.clear();

  s = " ";
  util::split(s.begin(), s.end(), std::back_inserter(v), ';', true);
  REQUIRE_EQ((size_t)0, v.size());

  v.clear();

  s = " ";
  util::split(s.begin(), s.end(), std::back_inserter(v), ';');
  REQUIRE_EQ((size_t)1, v.size());
  REQUIRE_EQ(std::string(" "), v[0]);

  v.clear();

  s = ";";
  util::split(s.begin(), s.end(), std::back_inserter(v), ';');
  REQUIRE_EQ((size_t)0, v.size());

  v.clear();

  s = ";";
  util::split(s.begin(), s.end(), std::back_inserter(v), ';', false, true);
  REQUIRE_EQ((size_t)2, v.size());
  itr = v.begin();
  REQUIRE_EQ(std::string(""), *itr++);
  REQUIRE_EQ(std::string(""), *itr++);

  v.clear();

  s = "";
  util::split(s.begin(), s.end(), std::back_inserter(v), ';', false, true);
  REQUIRE_EQ((size_t)1, v.size());
  REQUIRE_EQ(std::string(""), v[0]);
}

TEST_CASE("UtilTest1.testSplitIter")
{
  std::vector<Scip> v;
  std::string s = "k1; k2;; k3";
  util::splitIter(s.begin(), s.end(), std::back_inserter(v), ';', true);
  REQUIRE_EQ((size_t)3, v.size());
  REQUIRE_EQ(std::string("k1"), std::string(v[0].first, v[0].second));
  REQUIRE_EQ(std::string("k2"), std::string(v[1].first, v[1].second));
  REQUIRE_EQ(std::string("k3"), std::string(v[2].first, v[2].second));

  v.clear();

  s = "k1; k2; k3";
  util::splitIter(s.begin(), s.end(), std::back_inserter(v), ';');
  REQUIRE_EQ((size_t)3, v.size());
  REQUIRE_EQ(std::string("k1"), std::string(v[0].first, v[0].second));
  REQUIRE_EQ(std::string(" k2"), std::string(v[1].first, v[1].second));
  REQUIRE_EQ(std::string(" k3"), std::string(v[2].first, v[2].second));

  v.clear();

  s = "k=v";
  util::splitIter(s.begin(), s.end(), std::back_inserter(v), ';', false, true);
  REQUIRE_EQ((size_t)1, v.size());
  REQUIRE_EQ(std::string("k=v"), std::string(v[0].first, v[0].second));

  v.clear();

  s = ";;k1;;k2;";
  util::splitIter(s.begin(), s.end(), std::back_inserter(v), ';', false, true);
  REQUIRE_EQ((size_t)6, v.size());
  REQUIRE_EQ(std::string(""), std::string(v[0].first, v[0].second));
  REQUIRE_EQ(std::string(""), std::string(v[1].first, v[1].second));
  REQUIRE_EQ(std::string("k1"), std::string(v[2].first, v[2].second));
  REQUIRE_EQ(std::string(""), std::string(v[3].first, v[3].second));
  REQUIRE_EQ(std::string("k2"), std::string(v[4].first, v[4].second));
  REQUIRE_EQ(std::string(""), std::string(v[5].first, v[5].second));

  v.clear();

  s = ";;k1;;k2;";
  util::splitIter(s.begin(), s.end(), std::back_inserter(v), ';');
  REQUIRE_EQ((size_t)2, v.size());
  REQUIRE_EQ(std::string("k1"), std::string(v[0].first, v[0].second));
  REQUIRE_EQ(std::string("k2"), std::string(v[1].first, v[1].second));

  v.clear();

  s = "k; ";
  util::splitIter(s.begin(), s.end(), std::back_inserter(v), ';');
  REQUIRE_EQ((size_t)2, v.size());
  REQUIRE_EQ(std::string("k"), std::string(v[0].first, v[0].second));
  REQUIRE_EQ(std::string(" "), std::string(v[1].first, v[1].second));

  v.clear();

  s = " ";
  util::splitIter(s.begin(), s.end(), std::back_inserter(v), ';', true, true);
  REQUIRE_EQ((size_t)1, v.size());
  REQUIRE_EQ(std::string(""), std::string(v[0].first, v[0].second));

  v.clear();

  s = " ";
  util::splitIter(s.begin(), s.end(), std::back_inserter(v), ';', true);
  REQUIRE_EQ((size_t)0, v.size());

  v.clear();

  s = " ";
  util::splitIter(s.begin(), s.end(), std::back_inserter(v), ';');
  REQUIRE_EQ((size_t)1, v.size());
  REQUIRE_EQ(std::string(" "), std::string(v[0].first, v[0].second));

  v.clear();

  s = ";";
  util::splitIter(s.begin(), s.end(), std::back_inserter(v), ';');
  REQUIRE_EQ((size_t)0, v.size());

  v.clear();

  s = ";";
  util::splitIter(s.begin(), s.end(), std::back_inserter(v), ';', false, true);
  REQUIRE_EQ((size_t)2, v.size());
  REQUIRE_EQ(std::string(""), std::string(v[0].first, v[0].second));
  REQUIRE_EQ(std::string(""), std::string(v[1].first, v[1].second));

  v.clear();

  s = "";
  util::splitIter(s.begin(), s.end(), std::back_inserter(v), ';', false, true);
  REQUIRE_EQ((size_t)1, v.size());
  REQUIRE_EQ(std::string(""), std::string(v[0].first, v[0].second));
}

TEST_CASE("UtilTest1.testSplitIterM")
{
  const char d[] = ";";
  const char md[] = "; ";
  std::vector<Scip> v;
  std::string s = "k1; k2;; k3";
  util::splitIterM(s.begin(), s.end(), std::back_inserter(v), d, true);
  REQUIRE_EQ((size_t)3, v.size());
  REQUIRE_EQ(std::string("k1"), std::string(v[0].first, v[0].second));
  REQUIRE_EQ(std::string("k2"), std::string(v[1].first, v[1].second));
  REQUIRE_EQ(std::string("k3"), std::string(v[2].first, v[2].second));

  v.clear();

  s = "k1; k2; k3";
  util::splitIterM(s.begin(), s.end(), std::back_inserter(v), d);
  REQUIRE_EQ((size_t)3, v.size());
  REQUIRE_EQ(std::string("k1"), std::string(v[0].first, v[0].second));
  REQUIRE_EQ(std::string(" k2"), std::string(v[1].first, v[1].second));
  REQUIRE_EQ(std::string(" k3"), std::string(v[2].first, v[2].second));

  v.clear();

  s = "k1; k2; k3";
  util::splitIterM(s.begin(), s.end(), std::back_inserter(v), md);
  REQUIRE_EQ((size_t)3, v.size());
  REQUIRE_EQ(std::string("k1"), std::string(v[0].first, v[0].second));
  REQUIRE_EQ(std::string("k2"), std::string(v[1].first, v[1].second));
  REQUIRE_EQ(std::string("k3"), std::string(v[2].first, v[2].second));

  v.clear();

  s = "k1; k2; k3;";
  util::splitIterM(s.begin(), s.end(), std::back_inserter(v), md, false, true);
  REQUIRE_EQ((size_t)6, v.size());
  REQUIRE_EQ(std::string("k1"), std::string(v[0].first, v[0].second));
  REQUIRE_EQ(std::string(""), std::string(v[1].first, v[1].second));
  REQUIRE_EQ(std::string("k2"), std::string(v[2].first, v[2].second));
  REQUIRE_EQ(std::string(""), std::string(v[3].first, v[3].second));
  REQUIRE_EQ(std::string("k3"), std::string(v[4].first, v[4].second));
  REQUIRE_EQ(std::string(""), std::string(v[5].first, v[5].second));

  v.clear();

  s = "k=v";
  util::splitIterM(s.begin(), s.end(), std::back_inserter(v), d, false, true);
  REQUIRE_EQ((size_t)1, v.size());
  REQUIRE_EQ(std::string("k=v"), std::string(v[0].first, v[0].second));

  v.clear();

  s = ";;k1;;k2;";
  util::splitIterM(s.begin(), s.end(), std::back_inserter(v), d, false, true);
  REQUIRE_EQ((size_t)6, v.size());
  REQUIRE_EQ(std::string(""), std::string(v[0].first, v[0].second));
  REQUIRE_EQ(std::string(""), std::string(v[1].first, v[1].second));
  REQUIRE_EQ(std::string("k1"), std::string(v[2].first, v[2].second));
  REQUIRE_EQ(std::string(""), std::string(v[3].first, v[3].second));
  REQUIRE_EQ(std::string("k2"), std::string(v[4].first, v[4].second));
  REQUIRE_EQ(std::string(""), std::string(v[5].first, v[5].second));

  v.clear();

  s = ";;k1;;k2;";
  util::splitIterM(s.begin(), s.end(), std::back_inserter(v), d);
  REQUIRE_EQ((size_t)2, v.size());
  REQUIRE_EQ(std::string("k1"), std::string(v[0].first, v[0].second));
  REQUIRE_EQ(std::string("k2"), std::string(v[1].first, v[1].second));

  v.clear();

  s = "k; ";
  util::splitIterM(s.begin(), s.end(), std::back_inserter(v), d);
  REQUIRE_EQ((size_t)2, v.size());
  REQUIRE_EQ(std::string("k"), std::string(v[0].first, v[0].second));
  REQUIRE_EQ(std::string(" "), std::string(v[1].first, v[1].second));

  v.clear();

  s = " ";
  util::splitIterM(s.begin(), s.end(), std::back_inserter(v), d, true, true);
  REQUIRE_EQ((size_t)1, v.size());
  REQUIRE_EQ(std::string(""), std::string(v[0].first, v[0].second));

  v.clear();

  s = " ";
  util::splitIterM(s.begin(), s.end(), std::back_inserter(v), d, true);
  REQUIRE_EQ((size_t)0, v.size());

  v.clear();

  s = " ";
  util::splitIterM(s.begin(), s.end(), std::back_inserter(v), d);
  REQUIRE_EQ((size_t)1, v.size());
  REQUIRE_EQ(std::string(" "), std::string(v[0].first, v[0].second));

  v.clear();

  s = ";";
  util::splitIterM(s.begin(), s.end(), std::back_inserter(v), d);
  REQUIRE_EQ((size_t)0, v.size());

  v.clear();

  s = ";";
  util::splitIterM(s.begin(), s.end(), std::back_inserter(v), d, false, true);
  REQUIRE_EQ((size_t)2, v.size());
  REQUIRE_EQ(std::string(""), std::string(v[0].first, v[0].second));
  REQUIRE_EQ(std::string(""), std::string(v[1].first, v[1].second));

  v.clear();

  s = "";
  util::splitIterM(s.begin(), s.end(), std::back_inserter(v), d, false, true);
  REQUIRE_EQ((size_t)1, v.size());
  REQUIRE_EQ(std::string(""), std::string(v[0].first, v[0].second));
}

} // namespace aria2
