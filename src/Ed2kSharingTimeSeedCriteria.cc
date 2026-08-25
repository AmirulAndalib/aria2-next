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
#include "Ed2kSharingTimeSeedCriteria.h"

#include "RequestGroup.h"

namespace aria2 {

Ed2kSharingTimeSeedCriteria::Ed2kSharingTimeSeedCriteria(
    RequestGroup* group, std::chrono::seconds duration)
    : group_(group), duration_(duration)
{
}

void Ed2kSharingTimeSeedCriteria::reset()
{
  group_->synchronizeEd2kSharingTime();
}

bool Ed2kSharingTimeSeedCriteria::evaluate()
{
  return group_->getEd2kSharingTime() >= duration_.count();
}

} // namespace aria2
