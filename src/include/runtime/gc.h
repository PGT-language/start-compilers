#ifndef PGT_RUNTIME_GC_H
#define PGT_RUNTIME_GC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
void gc_init();
void gc_cleanup();
void *gc_malloc(size_t size);
void gc_collect();
void gc_add_root(void *ptr);
void gc_remove_root(void *ptr);
#ifdef __cplusplus
}
#endif
#endif