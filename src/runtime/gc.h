#ifndef PGT_RUNTIME_GC_H
#define PGT_RUNTIME_GC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Инициализация GC
void gc_init();

// Очистка GC
void gc_cleanup();

// Выделение памяти через GC
void* gc_malloc(size_t size);

// Принудительный запуск сборки мусора
void gc_collect();

// Добавление/удаление корневых объектов
void gc_add_root(void* ptr);
void gc_remove_root(void* ptr);

#ifdef __cplusplus
}
#endif

#endif // PGT_RUNTIME_GC_H

