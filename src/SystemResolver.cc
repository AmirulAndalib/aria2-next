/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2026 aria2-next contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
/* copyright --> */
#include "SystemResolver.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>

#include "Log.h"
#include "fmt.h"

namespace aria2 {

namespace asio = boost::asio;

struct SystemResolver::Impl {
  struct Operation {
    Operation(asio::io_context& context, RequestId requestId,
              std::string hostname)
        : id(requestId),
          hostname(std::move(hostname)),
          resolver(context),
          timer(context)
    {
    }

    RequestId id;
    std::string hostname;
    asio::ip::tcp::resolver resolver;
    asio::steady_timer timer;
    std::vector<std::string> addresses;
    std::string error;
    bool complete = false;
    bool timedOut = false;
    bool cancelled = false;
  };

  asio::io_context context;
  std::unordered_map<RequestId, std::shared_ptr<Operation>> operations;
  RequestId nextId = 1;
  bool completed = false;
};

SystemResolver::SystemResolver() : impl_(new Impl()) {}

SystemResolver::~SystemResolver()
{
  for (auto& entry : impl_->operations) {
    entry.second->cancelled = true;
    entry.second->resolver.cancel();
    entry.second->timer.cancel();
  }
  impl_->operations.clear();
  impl_->context.stop();
}

SystemResolver::RequestId
SystemResolver::resolve(const std::string& hostname, uint16_t port,
                        bool allowIPv6, std::chrono::seconds timeout)
{
  if (impl_->context.stopped()) {
    impl_->context.restart();
  }
  auto id = impl_->nextId++;
  if (id == 0) {
    id = impl_->nextId++;
  }
  auto operation =
      std::make_shared<Impl::Operation>(impl_->context, id, hostname);
  impl_->operations.emplace(id, operation);

  if (timeout.count() > 0) {
    operation->timer.expires_after(timeout);
    operation->timer.async_wait([operation](const boost::system::error_code& ec) {
      if (!ec && !operation->complete && !operation->cancelled) {
        operation->timedOut = true;
        operation->resolver.cancel();
      }
    });
  }

  auto complete = [this, operation](
                      const boost::system::error_code& ec,
                      asio::ip::tcp::resolver::results_type results) {
    operation->timer.cancel();
    if (operation->cancelled) {
      return;
    }
    if (ec) {
      operation->error = operation->timedOut
                             ? "Name resolution timed out"
                             : fmt("%s:%d: %s", ec.category().name(),
                                   ec.value(), ec.message().c_str());
      A2_LOG_DEBUG(fmt("component=network event=dns_failed host=%s "
                       "category=%s code=%d timeout=%s message=%s",
                       logging::sanitizeText(operation->hostname).c_str(),
                       ec.category().name(), ec.value(),
                       operation->timedOut ? "true" : "false",
                       logging::sanitizeText(operation->error).c_str()));
    }
    else {
      for (const auto& result : results) {
        auto address = result.endpoint().address().to_string();
        if (std::find(operation->addresses.begin(), operation->addresses.end(),
                      address) == operation->addresses.end()) {
          operation->addresses.push_back(std::move(address));
        }
      }
      if (operation->addresses.empty()) {
        operation->error = "No address returned";
        A2_LOG_DEBUG(fmt("component=network event=dns_empty host=%s",
                         logging::sanitizeText(operation->hostname).c_str()));
      }
    }
    operation->complete = true;
    impl_->completed = true;
  };

  const auto service = std::to_string(port);
  try {
    if (allowIPv6) {
      operation->resolver.async_resolve(hostname, service,
                                        std::move(complete));
    }
    else {
      operation->resolver.async_resolve(
          asio::ip::tcp::v4(), hostname, service,
          asio::ip::resolver_base::address_configured, std::move(complete));
    }
  }
  catch (const std::exception& error) {
    operation->timer.cancel();
    operation->error = error.what();
    A2_LOG_DEBUG(fmt("component=network event=dns_start_failed host=%s "
                     "message=%s",
                     logging::sanitizeText(hostname).c_str(),
                     logging::sanitizeText(error.what()).c_str()));
    operation->complete = true;
    impl_->completed = true;
  }
  return id;
}

SystemResolver::Status
SystemResolver::take(RequestId id, std::vector<std::string>& addresses,
                     std::string& error)
{
  const auto found = impl_->operations.find(id);
  if (found == impl_->operations.end()) {
    error = "Unknown name resolution request";
    return Status::Error;
  }
  const auto& operation = found->second;
  if (!operation->complete) {
    return Status::Pending;
  }
  addresses = std::move(operation->addresses);
  error = std::move(operation->error);
  impl_->operations.erase(found);
  return error.empty() ? Status::Success : Status::Error;
}

void SystemResolver::cancel(RequestId id)
{
  const auto found = impl_->operations.find(id);
  if (found == impl_->operations.end()) {
    return;
  }
  found->second->cancelled = true;
  found->second->resolver.cancel();
  found->second->timer.cancel();
  impl_->operations.erase(found);
}

bool SystemResolver::poll()
{
  impl_->completed = false;
  impl_->context.poll();
  return impl_->completed;
}

bool SystemResolver::hasPending() const
{
  return std::any_of(
      impl_->operations.begin(), impl_->operations.end(),
      [](const auto& entry) { return !entry.second->complete; });
}

} // namespace aria2
