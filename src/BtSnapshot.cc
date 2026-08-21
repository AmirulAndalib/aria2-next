/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2026 The aria2-next contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
/* copyright --> */
#include "BtSnapshot.h"

namespace aria2 {

const char* btStateName(BtSnapshot::State state)
{
  switch (state) {
  case BtSnapshot::State::Adding:
    return "adding";
  case BtSnapshot::State::DownloadingMetadata:
    return "downloadingMetadata";
  case BtSnapshot::State::AwaitingFileSelection:
    return "awaitingFileSelection";
  case BtSnapshot::State::Checking:
    return "checking";
  case BtSnapshot::State::Downloading:
    return "downloading";
  case BtSnapshot::State::Finished:
    return "finished";
  case BtSnapshot::State::Seeding:
    return "seeding";
  case BtSnapshot::State::Paused:
    return "paused";
  case BtSnapshot::State::Stopping:
    return "stopping";
  case BtSnapshot::State::Stopped:
    return "stopped";
  case BtSnapshot::State::Error:
    return "error";
  }
  return "error";
}

} // namespace aria2
