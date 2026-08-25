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
#ifndef D_ED2K_SHARING_TIME_SEED_CRITERIA_H
#define D_ED2K_SHARING_TIME_SEED_CRITERIA_H

#include "SeedCriteria.h"

#include <chrono>

namespace aria2 {

class RequestGroup;

class Ed2kSharingTimeSeedCriteria : public SeedCriteria {
public:
  Ed2kSharingTimeSeedCriteria(RequestGroup* group,
                              std::chrono::seconds duration);

  void reset() override;
  bool evaluate() override;

private:
  RequestGroup* group_;
  std::chrono::seconds duration_;
};

} // namespace aria2

#endif // D_ED2K_SHARING_TIME_SEED_CRITERIA_H
