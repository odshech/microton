/* The engine's build settings on the FM-1: shared by the app (dinghy.c) and the engine's own unit (micro_unit.c). */
#pragma once
#define SEQ_PATTERNS 4u                         /* 4 patterns in RAM on the FM-1 for now (12 KB each, in the pool) */
#define SEQ_POOL __attribute__((section(".pool")))
