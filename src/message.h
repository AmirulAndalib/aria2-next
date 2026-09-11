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
#ifndef D_MESSAGE_H
#define D_MESSAGE_H

#include "common.h"

// clang-format off

#define MSG_NO_SEGMENT_AVAILABLE "CUID#%" PRId64 " - No segment available."
#define MSG_DOWNLOAD_ABORTED "CUID#%" PRId64 " - Download aborted. URI=%s"
#define MSG_RESTARTING_DOWNLOAD "CUID#%" PRId64 " - Restarting the download. URI=%s"
#define MSG_MAX_TRY                                                     \
  "CUID#%" PRId64 " - %d times attempted, but no success. Download aborted."
#define MSG_RESOLVING_HOSTNAME "CUID#%" PRId64 " - Resolving hostname %s"
#define MSG_NAME_RESOLUTION_COMPLETE                    \
  "CUID#%" PRId64 " - Name resolution complete: %s -> %s"
#define MSG_NAME_RESOLUTION_FAILED                      \
  "CUID#%" PRId64 " - Name resolution for %s failed:%s"
#define MSG_FILE_VALIDATION_FAILURE                             \
  "CUID#%" PRId64 " - Exception caught while validating file integrity."
#define MSG_FILE_ALLOCATION_FAILURE                             \
  "CUID#%" PRId64 " - Exception caught while allocating file space."
#define MSG_LISTENING_PORT                                      \
  "CUID#%" PRId64 " - Using port %d for accepting new connections"
#define MSG_ACCEPT_FAILURE "CUID#%" PRId64 " - Error in accepting connection"
#define MSG_CONNECT_FAILED_AND_RETRY            \
  "CUID#%" PRId64 " - Could not to connect to %s:%u. Trying another address"

#define MSG_UNRECOGNIZED_URI _("Unrecognized URI or unsupported protocol: %s")
#define MSG_FILE_ALREADY_EXISTS _("File %s already exists. Enable overwrite, continue the transfer, or choose another output path.")
#define MSG_SELECTIVE_DOWNLOAD_COMPLETED _("Download of selected files was complete.")
#define MSG_DOWNLOAD_COMPLETED _("The download was complete.")
#define MSG_REMOVED_HAVE_ENTRY _("Removed %lu have entries.")
#define MSG_ALLOCATION_COMPLETED "%ld seconds to allocate %" PRId64 " byte(s)"
#define MSG_FILE_ALLOCATION_DISPATCH                    \
  "Dispatching FileAllocationCommand for CUID#%" PRId64 "."
#define MSG_METALINK_QUEUEING _("Metalink: Queueing %s for download.")
#define MSG_FILE_DOWNLOAD_COMPLETED _("Download complete: %s")
#define MSG_SEEDING_END _("Seeding is over.")
#define MSG_URI_REQUIRED _("Specify at least one URL.")
#define MSG_DAEMON_REQUIRES_WORK                                        \
  _("Daemon mode requires --enable-rpc=true or at least one download or input file.")
#define MSG_DAEMON_FAILED _("daemon failed.")
#define MSG_VERIFICATION_SUCCESSFUL _("Verification finished successfully. file=%s")
#define MSG_VERIFICATION_FAILED _("Checksum error detected. file=%s")
#define MSG_INCOMPLETE_RANGE _("Incomplete range specified. %s")
#define MSG_STRING_INTEGER_CONVERSION_FAILURE _("Failed to convert string into value: %s")
#define MSG_FILE_RENAMED _("File already exists. Renamed to %s.")
#define MSG_SHARE_RATIO_REPORT _("Your share ratio was %.1f, uploaded/downloaded=%sB/%sB")
#define MSG_WINSOCK_INIT_FAILD _("Windows socket library initialization failed")
#define MSG_TIME_HAS_PASSED _("%ld second(s) has passed. Stopping application.")
#define MSG_SIGNATURE_SAVED _("Saved signature as %s. Please note that Aria2 Next" \
                              " doesn't verify signatures.")
#define MSG_SIGNATURE_NOT_SAVED _("Saving signature as %s failed. Maybe file" \
                                  " already exists.")
#define MSG_OPENING_READABLE_SERVER_STAT_FILE_FAILED    \
  _("Failed to open ServerStat file %s for read.")
#define MSG_SERVER_STAT_LOADED _("ServerStat file %s loaded successfully.")
#define MSG_READING_SERVER_STAT_FILE_FAILED _("Failed to read ServerStat from" \
                                              " %s.")
#define MSG_OPENING_WRITABLE_SERVER_STAT_FILE_FAILED    \
  _("Failed to open ServerStat file %s for write.")
#define MSG_SERVER_STAT_SAVED _("ServerStat file %s saved successfully.")
#define MSG_WRITING_SERVER_STAT_FILE_FAILED _("Failed to write ServerStat to" \
                                              " %s.")
#define MSG_ESTABLISHING_CONNECTION_FAILED              \
  _("Failed to establish connection, cause: %s")
#define MSG_NETWORK_PROBLEM _("Network problem has occurred. cause:%s")
#define MSG_NO_FILES_TO_DOWNLOAD _("No files to download.")
#define MSG_SHOW_FILES _("Printing the contents of file '%s'...")
#define MSG_NOT_TORRENT_METALINK _("This file is neither Torrent nor Metalink" \
                                   " file. Skipping.")
#define MSG_CANNOT_PARSE_XML_RPC_REQUEST "Failed to parse xml-rpc request."
#define MSG_NOT_FILE _("Is '%s' a file?")
#define MSG_INTERFACE_NOT_FOUND _("Failed to find given interface %s,"  \
                                  " cause: %s")

#define EX_TIME_OUT _("Timeout.")
#define EX_FILENAME_MISMATCH _("The requested filename and the previously registered one are not same. Expected:%s Actual:%s")
#define EX_TOO_LARGE_FILE "Too large file size. size=%" PRId64 ""
#define EX_SSL_INIT_FAILURE _("SSL initialization failed: %s")
#define EX_SIZE_MISMATCH "Size mismatch Expected:%" PRId64 " Actual:%" PRId64 ""
#define EX_EOF_FROM_PEER _("Got EOF from peer.")

#define EX_FILE_OPEN _("Failed to open the file %s, cause: %s")
#define EX_FILE_WRITE _("Failed to write into the file %s, cause: %s")
#define EX_FILE_READ _("Failed to read from the file %s, cause: %s")
#define EX_FILE_SEEK _("Failed to seek the file %s, cause: %s")
#define EX_FILE_OFFSET_OUT_OF_RANGE "The offset is out of range, offset=%" PRId64 ""
#define EX_MAKE_DIR _("Failed to make the directory %s, cause: %s")

#define EX_SOCKET_SET_OPT _("Failed to set a socket option, cause: %s")
#define EX_SOCKET_BLOCKING _("Failed to set a socket as blocking, cause: %s")
#define EX_SOCKET_NONBLOCKING _("Failed to set a socket as non-blocking, cause: %s")
#define EX_SOCKET_BIND _("Failed to bind a socket, cause: %s")
#define EX_SOCKET_LISTEN _("Failed to listen to a socket, cause: %s")
#define EX_SOCKET_ACCEPT _("Failed to accept a peer connection, cause: %s")
#define EX_SOCKET_GET_NAME _("Failed to get the name of socket, cause: %s")
#define EX_RESOLVE_HOSTNAME _("Failed to resolve the hostname %s, cause: %s")
#define EX_SOCKET_CONNECT _("Failed to connect to the host %s, cause: %s")
#define EX_SOCKET_CHECK_WRITABLE _("Failed to check whether the socket is writable, cause: %s")
#define EX_SOCKET_CHECK_READABLE _("Failed to check whether the socket is readable, cause: %s")
#define EX_SOCKET_SEND _("Failed to send data, cause: %s")
#define EX_SOCKET_RECV _("Failed to receive data, cause: %s")
#define EX_SOCKET_UNKNOWN_ERROR _("Unknown socket error %d (0x%x)")
#define EX_INVALID_CHUNK_CHECKSUM "Chunk checksum validation failed. checksumIndex=%lu, offset=%" PRId64 ", expectedHash=%s, actualHash=%s"
#define EX_DUPLICATE_FILE_DOWNLOAD _("File %s is being downloaded by other command.")
#define EX_NO_RESULT_WITH_YOUR_PREFS _("No file matched with your preference.")
#define EX_EXCEPTION_CAUGHT _("Exception caught")

// clang-format on

#endif // D_MESSAGE_H
