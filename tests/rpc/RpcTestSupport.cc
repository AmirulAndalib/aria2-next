/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#include "File.h"
#include "RpcTestSupport.h"
#include "ValueBase.h"
#include <memory>
#include <utility>
#include <vector>
#include "RpcMethod.h"

#include "a2doctest.h"

#include "ApplicationStatePath.h"
#include "DownloadEngine.h"
#include "SelectEventPoll.h"
#include "Option.h"
#include "RequestGroupMan.h"
#include "RequestGroup.h"
#include "rpc/RpcMethods.h"
#include "rpc/RpcStatus.h"
#include "OptionParser.h"
#include "OptionHandler.h"
#include "RpcRequest.h"
#include "prefs.h"
#include "TestUtil.h"
#include "DownloadContext.h"
#include "a2functional.h"
#include "download_helper.h"
#include "DefaultPieceStorage.h"
#include "Ed2kAttribute.h"
#ifdef ENABLE_BITTORRENT
#  include "BtDownload.h"
#  include "BtSession.h"
#  include "BtStateStore.h"
#endif // ENABLE_BITTORRENT

namespace aria2::rpc {
RpcMethodTest::RpcMethodTest()
{
  option_ = std::make_shared<Option>();
  option_->put(PREF_DIR, A2_TEST_OUT_DIR "/aria2_RpcMethodTest");
  option_->put(PREF_STATE_DIR, A2_TEST_OUT_DIR "/aria2_RpcMethodTest/state");
  option_->put(PREF_PIECE_LENGTH, "1048576");
  option_->put(PREF_MAX_DOWNLOAD_RESULT, "10");
  option_->put(PREF_BT_MAX_OPEN_FILES, "100");
  option_->put(PREF_BT_IO_THREADS, "10");
  option_->put(PREF_BT_HASHING_THREADS, "1");
  File(option_->get(PREF_DIR)).mkdirs();
  e_ = make_unique<DownloadEngine>(make_unique<SelectEventPoll>());
  e_->setOption(option_.get());
  e_->setRequestGroupMan(make_unique<RequestGroupMan>(
      std::vector<std::shared_ptr<RequestGroup>>{}, 1, option_.get()));
}

std::string getString(const Dict* dict, const std::string& key)
{
  return downcast<String>(dict->get(key))->s();
}

RpcRequest createReq(std::string methodName)
{
  return {std::move(methodName), List::g()};
}
} // namespace aria2::rpc
