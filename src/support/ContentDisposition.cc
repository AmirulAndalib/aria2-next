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
#include "support/ContentDisposition.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include "common.h" // IWYU pragma: keep
#include "support/Utf8Dfa.h"
#include "support/Text.h"
#include "support/Encoding.h"
#include "support/FilePath.h"
#include "a2functional.h"
#include <array>

namespace aria2::util {

typedef enum {
  CD_BEFORE_DISPOSITION_TYPE,
  CD_AFTER_DISPOSITION_TYPE,
  CD_DISPOSITION_TYPE,
  CD_BEFORE_DISPOSITION_PARM_NAME,
  CD_AFTER_DISPOSITION_PARM_NAME,
  CD_DISPOSITION_PARM_NAME,
  CD_BEFORE_VALUE,
  CD_AFTER_VALUE,
  CD_QUOTED_STRING,
  CD_TOKEN,
  CD_BEFORE_EXT_VALUE,
  CD_CHARSET,
  CD_LANGUAGE,
  CD_VALUE_CHARS,
  CD_VALUE_CHARS_PCT_ENCODED1,
  CD_VALUE_CHARS_PCT_ENCODED2
} content_disposition_parse_state;

typedef enum {
  CD_FILENAME_FOUND = 1,
  CD_EXT_FILENAME_FOUND = 1 << 1,
  CD_VALUE_COMPLETE = 1 << 2,
  CD_FINAL_EMPTY_PARAMETER_ALLOWED = 1 << 3
} content_disposition_parse_flag;

typedef enum {
  CD_ENC_UNKNOWN,
  CD_ENC_UTF8,
  CD_ENC_ISO_8859_1
} content_disposition_charset;

ssize_t parse_content_disposition(char* dest, size_t destlen,
                                  const char** charsetp, size_t* charsetlenp,
                                  const char* in, size_t len, bool defaultUTF8)
{
  const char *p = in, *eop = in + len, *mark_first = nullptr,
             *mark_last = nullptr;
  int state = CD_BEFORE_DISPOSITION_TYPE;
  int in_file_parm = 0;
  int flags = 0;
  int quoted_seen = 0;
  int charset = 0;
  /* To suppress warnings */
  char* dp = dest;
  size_t dlen = destlen;
  uint32_t dfa_state = utf8::ACCEPT;
  uint32_t dfa_code = 0;
  uint8_t pctval = 0;

  *charsetp = nullptr;
  *charsetlenp = 0;

  for (; p != eop; ++p) {
    switch (state) {
    case CD_BEFORE_DISPOSITION_TYPE:
      if (inRFC2616HttpToken(*p)) {
        state = CD_DISPOSITION_TYPE;
      }
      else if (!isLws(*p)) {
        return -1;
      }
      break;
    case CD_AFTER_DISPOSITION_TYPE:
    case CD_DISPOSITION_TYPE:
      if (*p == ';') {
        state = CD_BEFORE_DISPOSITION_PARM_NAME;
      }
      else if (isLws(*p)) {
        state = CD_AFTER_DISPOSITION_TYPE;
      }
      else if (state == CD_AFTER_DISPOSITION_TYPE || !inRFC2616HttpToken(*p)) {
        return -1;
      }
      break;
    case CD_BEFORE_DISPOSITION_PARM_NAME:
      if (inRFC2616HttpToken(*p)) {
        mark_first = p;
        state = CD_DISPOSITION_PARM_NAME;
      }
      else if (!isLws(*p)) {
        return -1;
      }
      break;
    case CD_AFTER_DISPOSITION_PARM_NAME:
    case CD_DISPOSITION_PARM_NAME:
      if (*p == '=') {
        if (state == CD_DISPOSITION_PARM_NAME) {
          mark_last = p;
        }
        in_file_parm = 0;
        if (strieq(mark_first, mark_last, "filename*")) {
          if ((flags & CD_EXT_FILENAME_FOUND) == 0) {
            in_file_parm = 1;
          }
          else {
            return -1;
          }
          state = CD_BEFORE_EXT_VALUE;
        }
        else if (strieq(mark_first, mark_last, "filename")) {
          if (flags & CD_FILENAME_FOUND) {
            return -1;
          }
          if ((flags & CD_EXT_FILENAME_FOUND) == 0) {
            in_file_parm = 1;
          }
          state = CD_BEFORE_VALUE;
        }
        else {
          /* ext-token must be characters in token, followed by "*" */
          if (mark_first != mark_last - 1 && *(mark_last - 1) == '*') {
            state = CD_BEFORE_EXT_VALUE;
          }
          else {
            state = CD_BEFORE_VALUE;
          }
        }
        if (in_file_parm) {
          dp = dest;
          dlen = destlen;
        }
        flags &= ~(CD_VALUE_COMPLETE | CD_FINAL_EMPTY_PARAMETER_ALLOWED);
      }
      else if (isLws(*p)) {
        mark_last = p;
        state = CD_AFTER_DISPOSITION_PARM_NAME;
      }
      else if (state == CD_AFTER_DISPOSITION_PARM_NAME ||
               !inRFC2616HttpToken(*p)) {
        return -1;
      }
      break;
    case CD_BEFORE_VALUE:
      if (*p == '"') {
        quoted_seen = 0;
        state = CD_QUOTED_STRING;
        if (defaultUTF8) {
          dfa_state = utf8::ACCEPT;
          dfa_code = 0;
        }
      }
      else if (inRFC2616HttpToken(*p)) {
        if (in_file_parm) {
          if (dlen == 0) {
            return -1;
          }
          else {
            *dp++ = *p;
            --dlen;
          }
        }
        state = CD_TOKEN;
      }
      else if (!isLws(*p)) {
        return -1;
      }
      break;
    case CD_AFTER_VALUE:
      if (*p == ';') {
        if (flags & CD_VALUE_COMPLETE) {
          flags |= CD_FINAL_EMPTY_PARAMETER_ALLOWED;
        }
        state = CD_BEFORE_DISPOSITION_PARM_NAME;
      }
      else {
        flags &= ~CD_VALUE_COMPLETE;
        if (!isLws(*p)) {
          return -1;
        }
      }
      break;
    case CD_QUOTED_STRING:
      if (*p == '\\' && quoted_seen == 0) {
        quoted_seen = 1;
      }
      else if (*p == '"' && quoted_seen == 0) {
        if (defaultUTF8 && dfa_state != utf8::ACCEPT) {
          return -1;
        }
        if (in_file_parm) {
          flags |= CD_FILENAME_FOUND;
        }
        flags |= CD_VALUE_COMPLETE;
        state = CD_AFTER_VALUE;
      }
      else {
        /* TEXT which is OCTET except CTLs, but including LWS. Accept
           ISO-8859-1 chars, or UTF-8 if defaultUTF8 is set */
        quoted_seen = 0;
        if (defaultUTF8) {
          if (utf8::decode(&dfa_state, &dfa_code, (unsigned char)*p) ==
              utf8::REJECT) {
            return -1;
          }
        }
        else if (!isIso8859p1(*p)) {
          return -1;
        }
        if (in_file_parm) {
          if (dlen == 0) {
            return -1;
          }
          else {
            *dp++ = *p;
            --dlen;
          }
        }
      }
      break;
    case CD_TOKEN:
      if (inRFC2616HttpToken(*p)) {
        if (in_file_parm) {
          if (dlen == 0) {
            return -1;
          }
          else {
            *dp++ = *p;
            --dlen;
          }
        }
      }
      else if (*p == ';') {
        if (in_file_parm) {
          flags |= CD_FILENAME_FOUND;
        }
        flags |= CD_FINAL_EMPTY_PARAMETER_ALLOWED;
        state = CD_BEFORE_DISPOSITION_PARM_NAME;
      }
      else if (isLws(*p)) {
        if (in_file_parm) {
          flags |= CD_FILENAME_FOUND;
        }
        state = CD_AFTER_VALUE;
      }
      else {
        return -1;
      }
      break;
    case CD_BEFORE_EXT_VALUE:
      if (*p == '\'') {
        /* Empty charset is not allowed */
        return -1;
      }
      else if (inRFC2978MIMECharset(*p)) {
        mark_first = p;
        state = CD_CHARSET;
      }
      else if (!isLws(*p)) {
        return -1;
      }
      break;
    case CD_CHARSET:
      if (*p == '\'') {
        mark_last = p;
        *charsetp = mark_first;
        *charsetlenp = mark_last - mark_first;
        if (strieq(mark_first, mark_last, "utf-8")) {
          charset = CD_ENC_UTF8;
          dfa_state = utf8::ACCEPT;
          dfa_code = 0;
        }
        else if (strieq(mark_first, mark_last, "iso-8859-1")) {
          charset = CD_ENC_ISO_8859_1;
        }
        else {
          charset = CD_ENC_UNKNOWN;
        }
        state = CD_LANGUAGE;
      }
      else if (!inRFC2978MIMECharset(*p)) {
        return -1;
      }
      break;
    case CD_LANGUAGE:
      if (*p == '\'') {
        if (in_file_parm) {
          dp = dest;
          dlen = destlen;
        }
        state = CD_VALUE_CHARS;
      }
      else if (*p != '-' && !isAlpha(*p) && !isDigit(*p)) {
        return -1;
      }
      break;
    case CD_VALUE_CHARS:
      if (inRFC5987AttrChar(*p)) {
        if (charset == CD_ENC_UTF8) {
          if (utf8::decode(&dfa_state, &dfa_code,
                           static_cast<unsigned char>(*p)) == utf8::REJECT) {
            return -1;
          }
        }
        if (in_file_parm) {
          if (dlen == 0) {
            return -1;
          }
          else {
            *dp++ = *p;
            --dlen;
          }
        }
      }
      else if (*p == '%') {
        if (in_file_parm) {
          if (dlen == 0) {
            return -1;
          }
        }
        pctval = 0;
        state = CD_VALUE_CHARS_PCT_ENCODED1;
      }
      else if (*p == ';' || isLws(*p)) {
        if (charset == CD_ENC_UTF8 && dfa_state != utf8::ACCEPT) {
          return -1;
        }
        if (in_file_parm) {
          flags |= CD_EXT_FILENAME_FOUND;
        }
        if (*p == ';') {
          flags |= CD_FINAL_EMPTY_PARAMETER_ALLOWED;
          state = CD_BEFORE_DISPOSITION_PARM_NAME;
        }
        else {
          state = CD_AFTER_VALUE;
        }
      }
      else if (!inRFC5987AttrChar(*p)) {
        return -1;
      }
      break;
    case CD_VALUE_CHARS_PCT_ENCODED1:
      if (isHexDigit(*p)) {
        pctval |= hexCharToUInt(*p) << 4;
        state = CD_VALUE_CHARS_PCT_ENCODED2;
      }
      else {
        return -1;
      }
      break;
    case CD_VALUE_CHARS_PCT_ENCODED2:
      if (isHexDigit(*p)) {
        pctval |= hexCharToUInt(*p);
        if (charset == CD_ENC_UTF8) {
          if (utf8::decode(&dfa_state, &dfa_code, pctval) == utf8::REJECT) {
            return -1;
          }
        }
        else if (charset == CD_ENC_ISO_8859_1) {
          if (!isIso8859p1(pctval)) {
            return -1;
          }
        }
        if (in_file_parm) {
          *dp++ = pctval;
          --dlen;
        }
        state = CD_VALUE_CHARS;
      }
      else {
        return -1;
      }
      break;
    }
  }
  switch (state) {
  case CD_BEFORE_DISPOSITION_TYPE:
  case CD_AFTER_DISPOSITION_TYPE:
  case CD_DISPOSITION_TYPE:
  case CD_AFTER_VALUE:
  case CD_TOKEN:
    return destlen - dlen;
  case CD_BEFORE_DISPOSITION_PARM_NAME:
    if ((flags & CD_FINAL_EMPTY_PARAMETER_ALLOWED) &&
        (flags & (CD_FILENAME_FOUND | CD_EXT_FILENAME_FOUND))) {
      return destlen - dlen;
    }
    return -1;
  case CD_VALUE_CHARS:
    if (charset == CD_ENC_UTF8 && dfa_state != utf8::ACCEPT) {
      return -1;
    }
    return destlen - dlen;
  default:
    return -1;
  }
}

std::string getContentDispositionFilename(const std::string& header,
                                          bool defaultUTF8)
{
  std::array<char, 1_k> cdval;
  size_t cdvallen = cdval.size();
  const char* charset;
  size_t charsetlen;
  ssize_t rv =
      parse_content_disposition(cdval.data(), cdvallen, &charset, &charsetlen,
                                header.c_str(), header.size(), defaultUTF8);
  if (rv == -1) {
    return "";
  }

  std::string res;
  if ((charset && strieq(charset, charset + charsetlen, "iso-8859-1")) ||
      (!charset && !defaultUTF8)) {
    res = iso8859p1ToUtf8(cdval.data(), rv);
  }
  else {
    res.assign(cdval.data(), rv);
  }
  if (!detectDirTraversal(res) &&
      res.find_first_of("/\\") == std::string::npos) {
    return res;
  }
  return "";
}

} // namespace aria2::util
