// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "encoder/system_data.h"

#include <cstring>
#include <utility>

#include "./logging.h"

namespace cobalt {
namespace encoder {

namespace {

#if defined(__x86_64__)

// Invokes the cpuid instruction on X86. |info_type| specifies which query
// we are performing. This is written into register EAX prior to invoking
// cpuid. (The sub-type specifier in register ECX is alwyas set to zero.)  The
// results from registers EAX, EBX, ECX, EDX respectively are writtent into the
// four entries of |cpu_info|. See for example the wikipedia article on
// cpuid for more info.
void Cpuid(int info_type, int cpu_info[4]) {
  __asm__ volatile("cpuid\n"
                   : "=a"(cpu_info[0]), "=b"(cpu_info[1]), "=c"(cpu_info[2]),
                     "=d"(cpu_info[3])
                   : "a"(info_type), "c"(0));
}

// Invokes Cpuid() and uses the results to populate |cpu|.
void PopuluateCpuInfo(SystemProfile::CPU* cpu) {
  // First we invoke Cpuid with info_type = 0 in order to obtain num_ids
  // and vendor_name.
  int cpu_info[4] = {-1};
  Cpuid(0, cpu_info);
  int num_ids = cpu_info[0];

  // The human-readable vendor name is the concatenation of three substrings
  // in fields 1, 3, 2 respectively.
  std::swap(cpu_info[2], cpu_info[3]);
  cpu->mutable_vendor_name()->resize(3 * sizeof(cpu_info[1]));
  std::memcpy(&(*cpu->mutable_vendor_name())[0], &cpu_info[1],
              3 * sizeof(cpu_info[1]));

  if (num_ids > 0) {
    // Then invoke Cpuid again with info_type = 1 in order to obtain
    // |signature|.
    Cpuid(1, cpu_info);
    cpu->set_signature(cpu_info[0]);
  }
}

#endif

}  // namespace

SystemData::SystemData() { PopulateSystemProfile(); }

void SystemData::PopulateSystemProfile() {
#if defined(__linux__)

  system_profile_.set_os(SystemProfile::LINUX);

#elif defined(__Fuchsia__)

  system_profile_.set_os(SystemProfile::FUCHSIA);

#else

  system_profile_.set_os(SystemProfile::UNKNOWN_OS);

#endif

#if defined(__x86_64__)

  system_profile_.set_arch(SystemProfile::X86_64);
  PopuluateCpuInfo(system_profile_.mutable_cpu());

#elif defined(__aarch64__)

  system_profile_.set_arch(SystemProfile::ARM_64);
  // TODO(rudominer) Implement CpuInfo on ARM.

#else

  system_profile_.set_arch(SystemProfile::UNKNOWN_ARCH);

#endif
}

}  // namespace encoder
}  // namespace cobalt
