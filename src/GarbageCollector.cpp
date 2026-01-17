#include "GarbageCollector.h"
#include "runtime/gc.h"
#include "Global.h"
#include "Utils.h"
#include <iostream>
#include <algorithm>

GarbageCollector* global_gc = nullptr;

GarbageCollector::~GarbageCollector() {
    // Очищаем всю кучу при уничтожении
    for (auto obj : heap) {
        delete obj;
    }
    heap.clear();
}

GCObject* GarbageCollector::allocate(size_t size) {
    // Проверяем, нужен ли сбор мусора
    if (total_allocated > gc_threshold) {
        collect();
    }
    
    auto obj = new GCObject();
    obj->data = malloc(size);
    obj->size = size;
    obj->marked = false;
    obj->ref_count = 0;
    
    heap.push_back(obj);
    total_allocated += size;
    
    return obj;
}

void GarbageCollector::add_root(GCObject* obj) {
    if (obj) {
        roots.insert(obj);
    }
}

void GarbageCollector::remove_root(GCObject* obj) {
    roots.erase(obj);
}

void GarbageCollector::mark_object(GCObject* obj) {
    if (!obj || obj->marked) return;
    obj->marked = true;
    
    // Здесь можно добавить рекурсивную разметку ссылок внутри объекта
    // Но для простого GC этого достаточно
}

void GarbageCollector::mark() {
    // Размечаем все корневые объекты
    for (auto root : roots) {
        mark_object(root);
    }
}

void GarbageCollector::sweep() {
    auto it = heap.begin();
    while (it != heap.end()) {
        if (!(*it)->marked) {
            // Объект не помечен, удаляем его
            total_allocated -= (*it)->size;
            delete *it;
            it = heap.erase(it);
        } else {
            // Снимаем метку для следующего цикла GC
            (*it)->marked = false;
            ++it;
        }
    }
}

void GarbageCollector::collect() {
    if (DEBUG) {
        std::cout << "[GC] Starting garbage collection..." << std::endl;
        std::cout << "[GC] Heap size before: " << total_allocated << " bytes (" 
                  << heap.size() << " objects)" << std::endl;
    }
    
    mark();
    sweep();
    
    if (DEBUG) {
        std::cout << "[GC] Heap size after: " << total_allocated << " bytes (" 
                  << heap.size() << " objects)" << std::endl;
    }
}

void GarbageCollector::auto_collect() {
    if (total_allocated > gc_threshold) {
        collect();
    }
}

// Runtime функции для использования в сгенерированном коде
void gc_init() {
    if (!global_gc) {
        global_gc = new GarbageCollector();
    }
}

void gc_cleanup() {
    if (global_gc) {
        delete global_gc;
        global_gc = nullptr;
    }
}

void* gc_malloc(size_t size) {
    if (!global_gc) gc_init();
    auto obj = global_gc->allocate(size);
    return obj->data;
}

void gc_collect() {
    if (global_gc) {
        global_gc->collect();
    }
}

void gc_add_root(void* ptr) {
    // Упрощенная версия - не используем в текущей реализации
}

void gc_remove_root(void* ptr) {
    // Упрощенная версия - не используем в текущей реализации
}