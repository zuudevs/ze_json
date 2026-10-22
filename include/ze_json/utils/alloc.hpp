#pragma once

#include <ze_json/types/integral.hpp>

// Placement allocation operator HARUS selalu noexcept sesuai standar C++,
// jangan pakai makro NOEXCEPT (kempretan konflik dengan <vcruntime_new.h>
// yang mendeklarasikannya secara noexcept eksplisit).
void*
operator new(zuu::types::size_t, void* p) noexcept;
void
operator delete(void*, void*) noexcept;