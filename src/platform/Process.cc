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
#ifdef _WIN32
#  include <windows.h>
#endif
#include "platform/Process.h"
#include "GroupId.h"
#include "prefs.h"
#include <csignal>
#include <memory>
#ifdef _WIN32
#  include <windows.h>
#endif
#include "common.h" // IWYU pragma: keep
#include "platform/NativeText.h"
#include "support/Text.h"
#include "support/Numbers.h"
#include "RequestGroup.h"
#include "DownloadContext.h"
#include "FileEntry.h"
#include "Option.h"
#include "Log.h"
#include "fmt.h"
#include "a2functional.h"
#include <array>
#include <mutex>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <cassert>

namespace aria2::util {

#if _WIN32
namespace {
std::mutex win_signal_lock;

static signal_handler_t win_int_handler = nullptr;
static signal_handler_t win_term_handler = nullptr;

static void win_ign_handler(int) {}

static BOOL WINAPI HandlerRoutine(DWORD ctrlType)
{
  void (*handler)(int) = nullptr;
  switch (ctrlType) {
  case CTRL_C_EVENT:
  case CTRL_BREAK_EVENT: {
    // Handler will be called on a new/different thread.
    std::lock_guard<std::mutex> lock(win_signal_lock);
    handler = win_int_handler;
  }

    if (handler) {
      handler(SIGINT);
      return TRUE;
    }
    return FALSE;

  case CTRL_LOGOFF_EVENT:
  case CTRL_CLOSE_EVENT:
  case CTRL_SHUTDOWN_EVENT: {
    // Handler will be called on a new/different thread.
    std::lock_guard<std::mutex> lock(win_signal_lock);
    handler = win_term_handler;
    ;
  }
    if (handler) {
      handler(SIGTERM);
      return TRUE;
    }
    return FALSE;
  }
  return FALSE;
}
} // namespace
#endif

void setGlobalSignalHandler(int sig, SignalMask* mask, signal_handler_t handler,
                            int flags)
{
#if _WIN32
  if (sig == SIGINT || sig == SIGTERM) {
    // Handler will be called on a new/different thread.
    std::lock_guard<std::mutex> lock(win_signal_lock);

    if (handler == SIG_DFL) {
      handler = nullptr;
    }
    else if (handler == SIG_IGN) {
      handler = win_ign_handler;
    }
    // Not yet in use: add console handler.
    if (handler && !win_int_handler && !win_term_handler) {
      ::SetConsoleCtrlHandler(HandlerRoutine, TRUE);
    }
    if (sig == SIGINT) {
      win_int_handler = handler;
    }
    else {
      win_term_handler = handler;
    }
    // No handlers set: remove.
    if (!win_int_handler && !win_term_handler) {
      ::SetConsoleCtrlHandler(HandlerRoutine, FALSE);
    }
    return;
  }
#endif

#ifdef HAVE_SIGACTION
  struct sigaction sigact;
  sigact.sa_handler = handler;
  sigact.sa_flags = flags;
  sigact.sa_mask = *mask;
  if (sigaction(sig, &sigact, nullptr) == -1) {
    auto errNum = errno;
    A2_LOG_ERROR(fmt("sigaction() failed for signal %d: %s", sig,
                     safeStrerror(errNum).c_str()));
  }
#else
  if (signal(sig, handler) == SIG_ERR) {
    auto errNum = errno;
    A2_LOG_ERROR(fmt("signal() failed for signal %d: %s", sig,
                     safeStrerror(errNum).c_str()));
  }
#endif // HAVE_SIGACTION
}
namespace {

void executeHook(const std::string& command, a2_gid_t gid, size_t numFiles,
                 const std::string& firstFilename)
{
  const std::string gidStr = GroupId::toHex(gid);
  const std::string numFilesStr = util::uitos(numFiles);
#ifndef __MINGW32__
  A2_LOG_DEBUG(fmt("Executing user command: %s %s %s %s", command.c_str(),
                   gidStr.c_str(), numFilesStr.c_str(), firstFilename.c_str()));
  pid_t cpid = fork();
  if (cpid == 0) {
    // child!
    execlp(command.c_str(), command.c_str(), gidStr.c_str(),
           numFilesStr.c_str(), firstFilename.c_str(),
           reinterpret_cast<char*>(0));
    perror(("Could not execute user command: " + command).c_str());
    _exit(EXIT_FAILURE);
    return;
  }

  if (cpid == -1) {
    A2_LOG_ERROR("fork() failed. Cannot execute user command.");
  }
  return;

#else // __MINGW32__
  PROCESS_INFORMATION pi;
  STARTUPINFOW si;

  memset(&si, 0, sizeof(si));
  si.cb = sizeof(STARTUPINFO);
  memset(&pi, 0, sizeof(pi));
  bool batch = util::iendsWith(command, ".bat");
  std::string cmdline;
  std::string cmdexe;

  // XXX batch handling, in particular quoting, correct?
  if (batch) {
    const char* p = getenv("windir");
    if (p) {
      cmdexe = p;
      cmdexe += "\\system32\\cmd.exe";
    }
    else {
      A2_LOG_DEBUG("Failed to get windir environment variable."
                   " Executing batch file will fail.");
      // TODO Might be useless.
      cmdexe = "cmd.exe";
    }
    cmdline += "/C \"";
  }
  cmdline += "\"";
  cmdline += command;
  cmdline += "\"";
  cmdline += " ";
  cmdline += gidStr;
  cmdline += " ";
  cmdline += numFilesStr;
  cmdline += " \"";
  cmdline += firstFilename;
  cmdline += "\"";
  if (batch) {
    cmdline += "\"";
  }
  auto wcharCmdline = utf8ToWChar(cmdline);
  A2_LOG_DEBUG(fmt("Executing user command: %s", cmdline.c_str()));
  DWORD rc = CreateProcessW(batch ? utf8ToWChar(cmdexe).c_str() : nullptr,
                            wcharCmdline.data(), nullptr, nullptr, false, 0,
                            nullptr, 0, &si, &pi);

  if (!rc) {
    A2_LOG_ERROR("CreateProcess() failed. Cannot execute user command.");
  }
  else {
    // Closing our handles does not terminate the detached child process.
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
  return;

#endif
}

} // namespace

void executeHookByOptName(const std::shared_ptr<RequestGroup>& group,
                          const Option* option, PrefPtr pref)
{
  executeHookByOptName(group.get(), option, pref);
}

void executeHookByOptName(const RequestGroup* group, const Option* option,
                          PrefPtr pref)
{
  const std::string& cmd = option->get(pref);
  if (!cmd.empty()) {
    const std::shared_ptr<DownloadContext> dctx = group->getDownloadContext();
    std::string firstFilename;
    size_t numFiles = 0;
    if (!group->inMemoryDownload()) {
      std::shared_ptr<FileEntry> file = dctx->getFirstRequestedFileEntry();
      if (file) {
        firstFilename = file->getPath();
      }
      numFiles = dctx->countRequestedFileEntry();
    }
    executeHook(cmd, group->getGID(), numFiles, firstFilename);
  }
}
#ifdef __MINGW32__
std::string formatLastError(int errNum)
{
  std::array<char, 4_k> buf;
  if (FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    nullptr, errNum,
                    // Default language
                    MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
                    static_cast<LPTSTR>(buf.data()), buf.size(),
                    nullptr) == 0) {
    return "";
  }

  return buf.data();
}
#endif // __MINGW32__

void make_fd_cloexec(int fd)
{
#ifndef __MINGW32__
  int flags;

  // TODO from linux man page, fcntl() with F_GETFD or F_SETFD does
  // not return -1 with errno == EINTR.  Historically, aria2 code base
  // checks this case.  Probably, it is not needed.
  while ((flags = fcntl(fd, F_GETFD)) == -1 && errno == EINTR)
    ;
  if (flags == -1) {
    return;
  }

  while (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1 && errno == EINTR)
    ;
#endif // !__MINGW32__
}

#ifdef __MINGW32__
bool gainPrivilege(LPCTSTR privName)
{
  LUID luid;
  TOKEN_PRIVILEGES tp;

  if (!LookupPrivilegeValue(nullptr, privName, &luid)) {
    auto errNum = GetLastError();
    A2_LOG_WARN(fmt("Lookup for privilege name %s failed. cause: %s", privName,
                    util::formatLastError(errNum).c_str()));
    return false;
  }

  tp.PrivilegeCount = 1;
  tp.Privileges[0].Luid = luid;
  tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

  HANDLE token;
  if (!OpenProcessToken(GetCurrentProcess(),
                        TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
    auto errNum = GetLastError();
    A2_LOG_WARN(fmt("Getting process token failed. cause: %s",
                    util::formatLastError(errNum).c_str()));
    return false;
  }

  auto tokenCloser = defer(token, CloseHandle);

  if (!AdjustTokenPrivileges(token, FALSE, &tp, 0, NULL, NULL)) {
    auto errNum = GetLastError();
    A2_LOG_WARN(fmt("Gaining privilege %s failed. cause: %s", privName,
                    util::formatLastError(errNum).c_str()));
    return false;
  }

  // Check privilege was really gained
  DWORD bufsize = 0;
  GetTokenInformation(token, TokenPrivileges, nullptr, 0, &bufsize);
  if (bufsize == 0) {
    A2_LOG_WARN("Checking privilege failed.");
    return false;
  }

  auto buf = make_unique<char[]>(bufsize);
  if (!GetTokenInformation(token, TokenPrivileges, buf.get(), bufsize,
                           &bufsize)) {
    auto errNum = GetLastError();
    A2_LOG_WARN(fmt("Checking privilege failed. cause: %s",
                    util::formatLastError(errNum).c_str()));
    return false;
  }

  auto privs = reinterpret_cast<TOKEN_PRIVILEGES*>(buf.get());
  for (size_t i = 0; i < privs->PrivilegeCount; ++i) {
    auto& priv = privs->Privileges[i];
    if (memcmp(&priv.Luid, &luid, sizeof(luid)) != 0) {
      continue;
    }
    if (priv.Attributes == SE_PRIVILEGE_ENABLED) {
      return true;
    }

    break;
  }

  A2_LOG_WARN(fmt("Gaining privilege %s failed.", privName));

  return false;
}
#endif // __MINGW32__

std::string safeStrerror(int error)
{
  const auto message = strerror(error);
  return message ? message : "";
}

} // namespace aria2::util
