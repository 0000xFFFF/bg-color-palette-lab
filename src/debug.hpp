#pragma once

#ifdef DEBUG
#define D(x) x
#else
#define D(x)
#endif
#define UNUSED(x) (void)(x)
#define DP(x) D(printf("[DEBUG] %s", x))
#define DPL(x) D(printf("[DEBUG] %s\n", x))
