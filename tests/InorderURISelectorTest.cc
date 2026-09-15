#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "InorderURISelector.h"

#include "a2doctest.h"

#include "common.h"
#include "FileEntry.h"

namespace aria2 {

class InorderURISelectorTest {
protected:
  FileEntry fileEntry_;

  std::shared_ptr<InorderURISelector> sel;

public:
  InorderURISelectorTest()
  {
    fileEntry_.setUris(
        {"http://alpha/file", "sftp://alpha/file", "http://bravo/file"});

    sel.reset(new InorderURISelector());
  }
};

TEST_CASE_FIXTURE(InorderURISelectorTest, "InorderURISelectorTest.testSelect")
{
  std::vector<std::pair<size_t, std::string>> usedHosts;
  REQUIRE_EQ(std::string("http://alpha/file"),
             sel->select(&fileEntry_, usedHosts));
  REQUIRE_EQ(std::string("sftp://alpha/file"),
             sel->select(&fileEntry_, usedHosts));
  REQUIRE_EQ(std::string("http://bravo/file"),
             sel->select(&fileEntry_, usedHosts));
  REQUIRE_EQ(std::string(""), sel->select(&fileEntry_, usedHosts));
}

} // namespace aria2
