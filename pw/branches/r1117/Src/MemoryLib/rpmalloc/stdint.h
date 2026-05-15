// stdint.h shim for MSVC 2008 (VS9) which lacks C99 stdint.h
#pragma once

typedef signed __int8    int8_t;
typedef unsigned __int8  uint8_t;
typedef signed __int16   int16_t;
typedef unsigned __int16 uint16_t;
typedef signed __int32   int32_t;
typedef unsigned __int32 uint32_t;
typedef signed __int64   int64_t;
typedef unsigned __int64 uint64_t;
typedef uint32_t         uintptr_t;
typedef int32_t          intptr_t;

#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

#ifndef INT32_MAX
#define INT32_MAX 0x7fffffff
#endif
