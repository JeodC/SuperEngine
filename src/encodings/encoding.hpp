// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2025 Serina Sakurai
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
//
// -----------------------------------------------------------------------

#pragma once

// iconv from libc replaces boost::locale::conv::between (which on Linux
// pulls in libicu* — ~30 MB transitive dependency unsuitable for handheld
// runtime bundles). iconv supports SHIFT-JIS / EUC-JP / GBK / EUC-KR /
// ISO-2022-JP — every encoding RealLive games actually use — and is in
// glibc on every supported Linux target.

#include <iconv.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

enum class Encoding { Ascii, UTF16, CP932, CP936, CP949 };

inline std::string EncodingToString(Encoding enc) {
  switch (enc) {
    case Encoding::Ascii:
      return "ASCII";
    case Encoding::UTF16:
      return "UTF-16LE";
    case Encoding::CP932:
      return "CP932";  // Japanese code page (SHIFT-JIS superset)
    case Encoding::CP936:
      return "CP936";  // Simplified Chinese code page
    case Encoding::CP949:
      return "CP949";  // Korean code page
    default:
      return "Unknown";
  }
}

inline std::string ConvertEncoding(const std::string& input,
                                   std::string from,
                                   std::string to) {
  iconv_t cd = iconv_open(to.c_str(), from.c_str());
  if (cd == reinterpret_cast<iconv_t>(-1))
    throw std::runtime_error("Conversion error: iconv_open(" + to + ", " +
                             from + ") failed: " + std::strerror(errno));

  // Worst-case expansion: any input byte → 4 UTF-8 bytes. Grow if needed.
  std::string out;
  out.resize(std::max<size_t>(input.size() * 4, 64));

  const char* in_ptr = input.data();
  size_t in_left = input.size();
  char* out_ptr = out.data();
  size_t out_left = out.size();

  while (in_left > 0) {
    size_t r = iconv(cd, const_cast<char**>(&in_ptr), &in_left, &out_ptr,
                     &out_left);
    if (r == static_cast<size_t>(-1)) {
      if (errno == E2BIG) {
        // Output buffer too small — double it and continue.
        size_t produced = out.size() - out_left;
        out.resize(out.size() * 2);
        out_ptr = out.data() + produced;
        out_left = out.size() - produced;
        continue;
      }
      iconv_close(cd);
      throw std::runtime_error("Conversion error: iconv(" + from + " -> " +
                               to + ") failed: " + std::strerror(errno));
    }
  }

  iconv_close(cd);
  out.resize(out.size() - out_left);
  return out;
}

inline std::string ConvertEncoding(const std::string& input,
                                   Encoding from,
                                   Encoding to) {
  return ConvertEncoding(input, EncodingToString(from), EncodingToString(to));
}
