// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// A dummy utility library. Just a place-holder to test the Cobalt
// build and test framework.

#ifndef UTIL_DUMMY_UTIL_H_
#define UTIL_DUMMY_UTIL_H_

// Returns n! (the factorial of n).  For negative n, n! is defined to be 1.
int Factorial(int n);

// Returns true iff n is a prime number.
bool IsPrime(int n);

#endif  // UTIL_DUMMY_UTIL_H_
