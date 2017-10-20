// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COBALT_GTEST_H_
#define COBALT_GTEST_H_

#ifdef HAVE_GOOGLETEST
#include "third_party/googletest/googletest/include/gtest/gtest.h"

#elif defined(__Fuchsia__)
#include "third_party/gtest/include/gtest/gtest.h"

#else
#error "Either HAVE_GOOGLETEST or __Fuchsia__ must be defined"
#endif

#endif  // COBALT_GTEST_H_
