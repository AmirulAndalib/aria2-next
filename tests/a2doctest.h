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
#ifndef A2_DOCTEST_H
#define A2_DOCTEST_H

#include <memory>

#include <doctest/doctest.h>

namespace doctest {

template <typename T> struct StringMaker<std::shared_ptr<T>> {
  static String convert(const std::shared_ptr<T>& value)
  {
    return value ? String("non-null shared_ptr") : String("null shared_ptr");
  }
};

template <typename T, typename Deleter>
struct StringMaker<std::unique_ptr<T, Deleter>> {
  static String convert(const std::unique_ptr<T, Deleter>& value)
  {
    return value ? String("non-null unique_ptr") : String("null unique_ptr");
  }
};

} // namespace doctest

#endif // A2_DOCTEST_H
