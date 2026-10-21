#pragma once

#include <ze_json/types/integral.hpp>

void*
operator new(zuu::types::size_t, void* p) NOEXCEPT;
void
operator delete(void*, void*) NOEXCEPT;