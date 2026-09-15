/* <!-- copyright */
/*
 * aria2 - The high speed download utility
 *
 * Copyright (C) 2006 Tatsuhiro Tsujikawa
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
/* copyright --> */
#include "common.h"
#include "error_code.h"
#include <cstddef>
#include <memory>
#include <ostream>
#include <string>
#include <vector>
#include "RequestGroupMan.h"
#include "OutputFile.h"
#include "FileEntry.h"
#include "Option.h"
#include "download_helper.h"
#include "support/Numbers.h"
#include "prefs.h"
#include <cassert>
#include <iomanip>
#include <sstream>

namespace aria2 {

enum DownloadResultStatus {
  A2_STATUS_OK,
  A2_STATUS_INPR,
  A2_STATUS_RM,
  A2_STATUS_ERR
};

namespace {
const char* getStatusStr(DownloadResultStatus status, bool useColor)
{
  // status string is formatted in 4 characters wide.
  switch (status) {
  case (A2_STATUS_OK):
    if (useColor) {
      return "\033[1;32mOK\033[0m  ";
    }
    else {
      return "OK  ";
    }
  case (A2_STATUS_INPR):
    if (useColor) {
      return "\033[1;34mINPR\033[0m";
    }
    else {
      return "INPR";
    }
  case (A2_STATUS_RM):
    if (useColor) {
      return "\033[1mRM\033[0m  ";
    }
    else {
      return "RM  ";
    }
  case (A2_STATUS_ERR):
    if (useColor) {
      return "\033[1;31mERR\033[0m ";
    }
    else {
      return "ERR ";
    }
  default:
    return "";
  }
}

} // namespace

void RequestGroupMan::showDownloadResults(OutputFile& o, bool full) const
{
  int pathRowSize = 55;
  // Download Results:
  // idx|stat|path/length
  // ===+====+=======================================================================
  o.printf("\n%s"
           "\ngid   |stat|avg speed  |",
           _("Download Results:"));
  if (full) {
    o.write("  %|path/URI"
            "\n======+====+===========+===+");
    pathRowSize -= 4;
  }
  else {
    o.write("path/URI"
            "\n======+====+===========+");
  }
  std::string line(pathRowSize, '=');
  o.printf("%s\n", line.c_str());
  bool useColor = o.supportsColor() && option_->getAsBool(PREF_ENABLE_COLOR);
  int ok = 0;
  int err = 0;
  int inpr = 0;
  int rm = 0;
  for (auto& dr : downloadResults_) {

    if (dr->belongsTo != 0) {
      continue;
    }
    const char* status;
    switch (dr->result) {
    case error_code::FINISHED:
      status = getStatusStr(A2_STATUS_OK, useColor);
      ++ok;
      break;
    case error_code::IN_PROGRESS:
      status = getStatusStr(A2_STATUS_INPR, useColor);
      ++inpr;
      break;
    case error_code::REMOVED:
      status = getStatusStr(A2_STATUS_RM, useColor);
      ++rm;
      break;
    default:
      status = getStatusStr(A2_STATUS_ERR, useColor);
      ++err;
    }
    if (full) {
      formatDownloadResultFull(o, status, dr);
    }
    else {
      o.write(formatDownloadResult(status, dr).c_str());
      o.write("\n");
    }
  }
  if (ok > 0 || err > 0 || inpr > 0 || rm > 0) {
    o.printf("\n%s\n", _("Status Legend:"));
    if (ok > 0) {
      o.write(_("(OK):download completed."));
    }
    if (err > 0) {
      o.write(_("(ERR):error occurred."));
    }
    if (inpr > 0) {
      o.write(_("(INPR):download in-progress."));
    }
    if (rm > 0) {
      o.write(_("(RM):download removed."));
    }
    o.write("\n");
  }
}

namespace {
void formatDownloadResultCommon(
    std::ostream& o, const char* status,
    const std::shared_ptr<DownloadResult>& downloadResult)
{
  o << std::setw(3) << downloadResult->gid->toAbbrevHex() << "|" << std::setw(4)
    << status << "|";
  if (downloadResult->sessionTime.count() > 0) {
    o << std::setw(8)
      << util::abbrevSize(downloadResult->sessionDownloadLength * 1000 /
                          downloadResult->sessionTime.count())
      << "B/s";
  }
  else {
    o << std::setw(11);
    o << "n/a";
  }
  o << "|";
}

} // namespace

void RequestGroupMan::formatDownloadResultFull(
    OutputFile& out, const char* status,
    const std::shared_ptr<DownloadResult>& downloadResult) const
{
  bool head = true;
  const std::vector<std::shared_ptr<FileEntry>>& fileEntries =
      downloadResult->fileEntries;
  for (size_t index = 0; index < fileEntries.size(); ++index) {
    const auto& f = fileEntries[index];
    if (!f->isRequested()) {
      continue;
    }
    std::stringstream o;
    if (head) {
      formatDownloadResultCommon(o, status, downloadResult);
      head = false;
    }
    else {
      o << "   |    |           |";
    }
    if (f->getLength() == 0 ||
        index >= downloadResult->fileCompletedLengths.size()) {
      o << "  -|";
    }
    else {
      const auto completedLength = downloadResult->fileCompletedLengths[index];
      o << std::setw(3) << 100 * completedLength / f->getLength() << "|";
    }
    writeFilePath(o, f, downloadResult->inMemoryDownload);
    o << "\n";
    out.write(o.str().c_str());
  }
  if (head) {
    std::stringstream o;
    formatDownloadResultCommon(o, status, downloadResult);
    o << "  -|n/a\n";
    out.write(o.str().c_str());
  }
}

std::string RequestGroupMan::formatDownloadResult(
    const char* status,
    const std::shared_ptr<DownloadResult>& downloadResult) const
{
  std::stringstream o;
  formatDownloadResultCommon(o, status, downloadResult);
  const std::vector<std::shared_ptr<FileEntry>>& fileEntries =
      downloadResult->fileEntries;
  writeFilePath(fileEntries.begin(), fileEntries.end(), o,
                downloadResult->inMemoryDownload);
  return o.str();
}

} // namespace aria2
