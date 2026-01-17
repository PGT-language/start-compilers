#pragma once

#include <vector>
#include <set>
#include <memory>
#include <cstddef>
#include <cstdlib>

// Объект в куче, управляемый GC
struct GCObject {
    bool marked = false;  // Метка для mark-and-sweep
    size_t ref_count = 0;  // Счетчик ссылок (опционально)
    void* data = nullptr;  // Указатель на данные
    size_t size = 0;  // Размер данных
    
    virtual ~GCObject() {
        if (data) {
            free(data);
            data = nullptr;
        }
    }
};

// Garbage Collector с mark-and-sweep алгоритмом
class GarbageCollector {
    std::vector<GCObject*> heap;  // Все объекты в куче
    std::set<GCObject*> roots;  // Корневые объекты (стек, глобальные переменные)
    size_t total_allocated = 0;  // Всего выделено памяти
    size_t gc_threshold = 1024 * 1024;  // Порог для запуска GC (1 MB)
    
    // Mark-and-sweep
    void mark();
    void sweep();
    void mark_object(GCObject* obj);
    
public:
    GarbageCollector() = default;
    ~GarbageCollector();
    
    // Выделение памяти
    GCObject* allocate(size_t size);
    
    // Регистрация корневых объектов
    void add_root(GCObject* obj);
    void remove_root(GCObject* obj);
    
    // Запуск сборки мусора
    void collect();
    
    // Автоматический сбор мусора при превышении порога
    void auto_collect();
    
    // Статистика
    size_t get_heap_size() const { return total_allocated; }
    size_t get_object_count() const { return heap.size(); }
};

// Глобальный GC (для runtime)
extern GarbageCollector* global_gc;