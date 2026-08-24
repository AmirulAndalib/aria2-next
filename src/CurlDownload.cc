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
#include "CurlDownload.h"

#include <utility>

#include "CurlDownloadImpl.h"

#include <algorithm>

namespace aria2 {

CurlDownload::CurlDownload(std::vector<std::string> uris)
    : impl_(new CurlDownloadImpl)
{
  for (auto& uri : uris) {
    if (std::find(impl_->uris.begin(), impl_->uris.end(), uri) ==
        impl_->uris.end()) {
      impl_->uris.push_back(std::move(uri));
    }
  }
}

CurlDownload::~CurlDownload()
{
  if (impl_->headers) {
    curl_slist_free_all(impl_->headers);
  }
  if (impl_->file) {
    fclose(impl_->file);
  }
  if (impl_->handle) {
    curl_easy_cleanup(impl_->handle);
  }
}

void CurlDownload::synchronizeUris(const std::vector<std::string>& uris)
{
  std::vector<std::string> next;
  for (const auto& uri : uris) {
    if (std::find(next.begin(), next.end(), uri) == next.end()) {
      next.push_back(uri);
    }
  }
  impl_->uris = std::move(next);
  const auto current =
      std::find(impl_->uris.begin(), impl_->uris.end(), impl_->currentUri);
  impl_->uriIndex = current == impl_->uris.end()
                        ? (impl_->uris.empty() ? 0 : impl_->uris.size() - 1)
                        : static_cast<size_t>(current - impl_->uris.begin());
}

} // namespace aria2
