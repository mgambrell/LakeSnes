#pragma once

#ifdef _MSC_VER
#define LAKESNES_UNREACHABLE __assume(false)
#define LAKENES_NOINLINE __declspec(noinline)
#else
#define LAKESNES_UNREACHABLE __builtin_unreachable()
#define LAKENES_NOINLINE __attribute__((noinline)) 
#endif
#define LAKESNES_UNREACHABLE_DEFAULT default: LAKESNES_UNREACHABLE; break;
