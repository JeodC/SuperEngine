// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2024 Serina Sakurai
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
// -----------------------------------------------------------------------

#include "encodings/utf16.hpp"

// utf8cpp is already vendored. Replaces boost::locale::conv::utf_to_utf<char>
// for the UTF-16 -> UTF-8 path, removing the boost_locale dependency.
#include "utf8.h"

std::string utf16le::Decode(std::string_view sv) {
  return Decode(sv_to_u16sv(sv));
}

std::string utf16le::Decode(std::vector<uint8_t> vec) {
  return Decode(
      std::string_view(reinterpret_cast<const char*>(vec.data()), vec.size()));
}

std::string utf16le::Decode(const std::u16string& str) {
  std::string out;
  utf8::utf16to8(str.begin(), str.end(), std::back_inserter(out));
  return out;
}

std::string utf16le::Decode(std::u16string_view sv) {
  return Decode(std::u16string(sv));
}
