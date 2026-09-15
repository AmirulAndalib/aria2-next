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
#ifndef D_CONTENT_DISPOSITION_H
#define D_CONTENT_DISPOSITION_H

#include "common.h"
#include <string>

namespace aria2::util {

// Parses Content-Disposition header field value |in| with its length
// |len| in a manner conforming to RFC 6266 and extracts filename
// value and copies it to the region pointed by |dest|. The |destlen|
// specifies the capacity of the |dest|. This function does not store
// NUL character after filename in |dest|. This function does not
// support RFC 2231 Continuation. If the function sees RFC 2231/5987
// encoding and charset, it stores its first pointer to |*charsetp|
// and its length in |*charsetlenp|. Otherwise, they are NULL and 0
// respectively.  In RFC 2231/5987 encoding, percent-encoded string
// will be decoded to original form and stored in |dest|.
//
// This function returns the number of written bytes in |dest| if it
// succeeds, or -1. If there is not enough room to store filename in
// |dest|, this function returns -1. If this function returns -1, the
// |dest|, |*charsetp| and |*charsetlenp| are undefined.
//
// It will handle quoted string(as in RFC 7230 section 3.2.6) as
// ISO-8859-1 by default, or UTF-8 if defaultUTF8 == true.
ssize_t parse_content_disposition(char* dest, size_t destlen,
                                  const char** charsetp, size_t* charsetlenp,
                                  const char* in, size_t len, bool defaultUTF8);

std::string getContentDispositionFilename(const std::string& header,
                                          bool defaultUTF8);

} // namespace aria2::util

#endif // D_CONTENT_DISPOSITION_H
