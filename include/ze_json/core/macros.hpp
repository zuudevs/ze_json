#pragma once

#ifndef NDEBUG
#define NOEXCEPT
#else
#define NOEXCEPT noexcept
#endif // NDEBUG

#if defined(__wasm__)
#define EXPORT_NAME(name) __attribute__((export_name(#name)))
#elif defined(_WIN32) || defined(_WIN64)
#define EXPORT_NAME(name)
#endif

#if defined(__GNUC__) || defined(__clang__)
#define RESTRICT __restrict__
#elif defined(_MSC_VER)
#define RESTRICT __restrict
#else
#define RESTRICT
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__wasm64)
#define __IS_64_BIT__ 1
#elif defined(__i386__) || defined(_M_IX86) || defined(__wasm32)
#define __IS_64_BIT__ 0
#else
#error "Unsupported target architecture"
#endif

#ifdef __cplusplus
#define EXTERN_C_BEGIN extern "C" {
#define EXTERN_C_END   }
#else
#define EXTERN_C_BEGIN
#define EXTERN_C_END
#endif