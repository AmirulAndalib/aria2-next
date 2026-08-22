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
#include "BtResumeStore.h"

#include <sstream>

#include "BufferedFile.h"
#include "DlAbortEx.h"
#include "File.h"
#include "Option.h"
#include "prefs.h"
#include "util.h"

namespace aria2 {

std::string BtResumeStore::path(const Option* option,
                                const std::string& infoHash)
{
  const auto stateDirectory =
      File(option->get(PREF_BT_SESSION_STATE_FILE)).getDirname();
  return util::applyDir(util::applyDir(stateDirectory, "torrents"),
                        infoHash + ".fastresume");
}

std::string BtResumeStore::read(const std::string& path)
{
  BufferedFile file(path.c_str(), BufferedFile::READ);
  if (!file) {
    return {};
  }
  std::stringstream data;
  file.transfer(data);
  return data.str();
}

void BtResumeStore::write(const std::string& path, const char* data,
                          size_t size)
{
  const auto temporary = path + "__temp";
  File directory(File(path).getDirname());
  if (!directory.isDir() && !directory.mkdirs()) {
    throw DL_ABORT_EX("Unable to create BitTorrent resume directory");
  }
  BufferedFile file(temporary.c_str(), BufferedFile::WRITE);
  if (!file || file.write(data, size) != size || file.close() == EOF ||
      !File(temporary).renameTo(path)) {
    throw DL_ABORT_EX("Unable to save BitTorrent resume data");
  }
}

} // namespace aria2
