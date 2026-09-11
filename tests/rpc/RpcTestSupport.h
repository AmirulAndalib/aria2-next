/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#ifndef ARIA2_TEST_RPC_SUPPORT_H
#define ARIA2_TEST_RPC_SUPPORT_H
#include "common.h"
#include "RpcRequest.h"
#include <memory>
#include <string>

namespace aria2 {
class DownloadEngine;
class Option;
class Dict;
namespace rpc {
class RpcMethodTest {
protected:
  std::shared_ptr<DownloadEngine> e_;
  std::shared_ptr<Option> option_;

public:
  RpcMethodTest();
};
std::string getString(const Dict* dict, const std::string& key);
RpcRequest createReq(std::string methodName);
} // namespace rpc
} // namespace aria2
#endif
