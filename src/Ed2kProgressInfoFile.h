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
#ifndef D_ED2K_PROGRESS_INFO_FILE_H
#define D_ED2K_PROGRESS_INFO_FILE_H

#include "ProgressInfoFile.h"

namespace aria2 {

class RequestGroup;

namespace ed2k {

class Ed2kSession;

class Ed2kProgressInfoFile : public ProgressInfoFile {
public:
  Ed2kProgressInfoFile(Ed2kSession* session, RequestGroup* group)
      : session_(session), group_(group)
  {
  }

  std::string getFilename() override;
  bool exists() override;
  void save() override;
  void load() override;
  void removeFile() override;
  void updateFilename() override {}

private:
  Ed2kSession* session_;
  RequestGroup* group_;
};

} // namespace ed2k

} // namespace aria2

#endif // D_ED2K_PROGRESS_INFO_FILE_H
