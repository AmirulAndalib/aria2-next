#include <iterator>
#include <memory>
#include <string>
#include "ValueBase.h"

#include <cstring>

#include "a2doctest.h"

#include "support/Encoding.h"

namespace aria2 {

TEST_CASE("ValueBaseTest.testString")
{
  String s(std::string("aria2"));
  REQUIRE_EQ(std::string("aria2"), s.s());

  unsigned char dataWithNull[] = {0xf0, '\0', 0x0f};
  String sWithNull(dataWithNull, sizeof(dataWithNull));
  REQUIRE(memcmp(dataWithNull, sWithNull.s().c_str(), sizeof(dataWithNull)) ==
          0);

  String zero("");
  REQUIRE_EQ(std::string(""), zero.s());

  String z;
  REQUIRE_EQ(std::string(""), z.s());

  const unsigned char uc[] = {0x08, 0x19, 0x2a, 0x3b};
  String data(uc, sizeof(uc));
  REQUIRE_EQ(util::toHex(uc, sizeof(uc)),
             util::toHex(data.uc(), data.s().size()));
}

TEST_CASE("ValueBaseTest.testDowncast")
{
  Integer integer(100);
  const Integer* x = downcast<Integer>(&integer);
  REQUIRE(x);
  REQUIRE_EQ(static_cast<Integer::ValueType>(100), x->i());
  REQUIRE(!downcast<String>(&integer));
  std::shared_ptr<Integer> si(new Integer(101));
  const Integer* x2 = downcast<Integer>(si);
  REQUIRE_EQ(static_cast<Integer::ValueType>(101), x2->i());

  String str("foo");
  const String* x3 = downcast<String>(&str);
  REQUIRE_EQ(static_cast<String::ValueType>("foo"), x3->s());

  List list;
  const List* x4 = downcast<List>(&list);
  REQUIRE(x4);

  Dict dict;
  const Dict* x5 = downcast<Dict>(&dict);
  REQUIRE(x5);
}

TEST_CASE("ValueBaseTest.testDict")
{
  Dict dict;
  REQUIRE(dict.empty());

  dict.put("ki", Integer::g(7));
  dict.put("ks", String::g("abc"));

  REQUIRE_EQ(static_cast<size_t>(2), dict.size());
  REQUIRE(dict.containsKey("ki"));
  REQUIRE_EQ(static_cast<Integer::ValueType>(7),
             downcast<Integer>(dict["ki"])->i());
  REQUIRE(dict.containsKey("ks"));
  REQUIRE_EQ(std::string("abc"), downcast<String>(dict["ks"])->s());

  REQUIRE(!dict["kn"]); // This does not adds kn key
  REQUIRE_EQ(static_cast<size_t>(2), dict.size());

  auto& ref = dict;
  ref["kn2"]; // This doesn't add kn2 key.
  REQUIRE_EQ(static_cast<size_t>(2), ref.size());
  REQUIRE(!ref.containsKey("kn2"));

  dict.removeKey("ks");
  REQUIRE_EQ(static_cast<size_t>(1), dict.size());
  REQUIRE(!dict.containsKey("ks"));

  auto ki = dict.popValue("ki");
  REQUIRE_EQ(Integer::ValueType{7}, downcast<Integer>(ki)->i());
  REQUIRE(dict.empty());
  REQUIRE(!dict.containsKey("ki"));
}

TEST_CASE("ValueBaseTest.testDictIter")
{
  Dict dict;
  dict.put("alpha2", String::g("alpha2"));
  dict.put("charlie", String::g("charlie"));
  dict.put("bravo", String::g("bravo"));
  dict.put("alpha", String::g("alpha"));

  Dict::ValueType::iterator i = dict.begin();
  REQUIRE_EQ(std::string("alpha"), (*i++).first);
  REQUIRE_EQ(std::string("alpha2"), (*i++).first);
  REQUIRE_EQ(std::string("bravo"), (*i++).first);
  REQUIRE_EQ(std::string("charlie"), (*i++).first);
  REQUIRE(dict.end() == i);

  const Dict& ref = dict;
  Dict::ValueType::const_iterator ci = ref.begin();
  REQUIRE_EQ(std::string("alpha"), (*ci++).first);
  std::advance(ci, 3);
  REQUIRE(ref.end() == ci);
}

TEST_CASE("ValueBaseTest.testList")
{
  List list;
  REQUIRE(list.empty());
  list << Integer::g(7) << String::g("aria2");

  REQUIRE_EQ(static_cast<size_t>(2), list.size());
  REQUIRE_EQ(static_cast<Integer::ValueType>(7),
             downcast<Integer>(list[0])->i());
  REQUIRE_EQ(static_cast<String::ValueType>("aria2"),
             downcast<String>(list[1])->s());

  const List& ref = list;
  REQUIRE_EQ(static_cast<Integer::ValueType>(7),
             downcast<Integer>(ref[0])->i());
  REQUIRE_EQ(static_cast<String::ValueType>("aria2"),
             downcast<String>(ref[1])->s());
}

TEST_CASE("ValueBaseTest.testListIter")
{
  List list;
  list << String::g("alpha2") << String::g("charlie") << String::g("bravo")
       << String::g("alpha");

  List::ValueType::iterator i = list.begin();
  REQUIRE_EQ(static_cast<String::ValueType>("alpha2"),
             downcast<String>(*i++)->s());
  REQUIRE_EQ(static_cast<String::ValueType>("charlie"),
             downcast<String>(*i++)->s());
  REQUIRE_EQ(static_cast<String::ValueType>("bravo"),
             downcast<String>(*i++)->s());
  REQUIRE_EQ(static_cast<String::ValueType>("alpha"),
             downcast<String>(*i++)->s());
  REQUIRE(list.end() == i);

  const List& ref = list;
  List::ValueType::const_iterator ci = ref.begin();
  REQUIRE_EQ(static_cast<String::ValueType>("alpha2"),
             downcast<String>(*ci++)->s());
  std::advance(ci, 3);
  REQUIRE(ref.end() == ci);
}

} // namespace aria2
