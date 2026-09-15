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
#include "ApplicationStatePath.h"

#import <Foundation/Foundation.h>

#include "DlAbortEx.h"
#include "util.h"

namespace aria2 {

namespace state {

std::string defaultDirectory()
{
  @autoreleasepool {
    auto urls = [[NSFileManager defaultManager]
        URLsForDirectory:NSApplicationSupportDirectory
               inDomains:NSUserDomainMask];
    auto url = [urls firstObject];
    if (!url || ![url fileSystemRepresentation]) {
      throw DL_ABORT_EX(
          "Unable to resolve the macOS application state directory");
    }
    return util::applyDir([url fileSystemRepresentation], "aria2-next");
  }
}

} // namespace state

} // namespace aria2
