#include "common.h"

#include <string>
#include <memory>

#include "WrDiskCacheEntry.h"
#include "GroupId.h"

namespace aria2 {

class MessageDigest;
class RequestGroupMan;
class RequestGroup;
class Option;
struct DownloadResult;

void createFile(const std::string& filename, size_t length);

std::string readFile(const std::string& path);

std::string fromHex(const std::string& s);

// Returns hex digest of contents of file denoted by filename.
std::string fileHexDigest(MessageDigest* ctx, const std::string& filename);

WrDiskCacheEntry::DataCell* createDataCell(int64_t goff, const char* data,
                                           size_t offset = 0);

std::shared_ptr<RequestGroup> findReservedGroup(RequestGroupMan* rgman,
                                                a2_gid_t gid);

std::shared_ptr<RequestGroup> getReservedGroup(RequestGroupMan* rgman,
                                               size_t index);

std::shared_ptr<RequestGroup>
createRequestGroup(int32_t pieceLength, int64_t totalLength,
                   const std::string& path, const std::string& uri,
                   const std::shared_ptr<Option>& opt);

std::shared_ptr<DownloadResult> createDownloadResult(error_code::Value result,
                                                     const std::string& uri);

namespace {
template <typename V, typename T> bool derefFind(const V& v, const T& t)
{
  for (auto i : v) {
    if (*i == *t) {
      return true;
    }
  }
  return false;
}
} // namespace

} // namespace aria2
