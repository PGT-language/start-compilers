#pragma once

#include <cstddef>
#include <cstdlib>
#include <memory>
#include <set>
#include <vector>

struct GCObject {
  bool marked = false;
  size_t ref_count = 0;
  void *data = nullptr;
  size_t size = 0;

  virtual ~GCObject() {
    if (data) {
      free(data);
      data = nullptr;
    }
  }
};

class GarbageCollector {
  std::vector<GCObject *> heap;
  std::set<GCObject *> roots;
  size_t total_allocated = 0;
  size_t gc_threshold = 1024 * 1024;
  void mark();
  void sweep();
  void mark_object(GCObject *obj);

public:
  GarbageCollector() = default;
  ~GarbageCollector();

  GCObject *allocate(size_t size);

  void add_root(GCObject *obj);
  void remove_root(GCObject *obj);

  void collect();

  void auto_collect();

  size_t get_heap_size() const { return total_allocated; }
  size_t get_object_count() const { return heap.size(); }
};
extern GarbageCollector *global_gc;