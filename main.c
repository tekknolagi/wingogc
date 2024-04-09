struct gc_obj {
  union {
    uintptr_t tag;
    struct gc_obj *forwarded; // for GC
  };
  uintptr_t payload[0];
};

static const uintptr_t NOT_FORWARDED_BIT = 1;
int is_forwarded(struct gc_obj *obj) {
  return (obj->tag & NOT_FORWARDED_BIT) == 1;
}
void* forwarded(struct gc_obj *obj) { 
  return obj->forwarded;
}
void forward(struct gc_obj *from, struct gc_obj *to) {
  from->forwarded = to;
}

struct gc_heap;

// To implement by the user:
size_t heap_object_size(struct gc_obj *obj);
size_t trace_heap_object(struct gc_obj *obj, struct gc_heap *heap,
                         void (*visit)(struct gc_obj **field,
                                       struct gc_heap *heap));
size_t trace_roots(struct gc_heap *heap,
                   void (*visit)(struct gc_obj **field,
                                 struct gc_heap *heap));

struct gc_heap {
  uintptr_t hp;
  uintptr_t limit;
  uintptr_t from_space;
  uintptr_t to_space;
  size_t size;
};

static uintptr_t align(uintptr_t val, uintptr_t alignment) {
  return (val + alignment - 1) & ~(alignment - 1);
}
static uintptr_t align_size(uintptr_t size) {
  return align(size, sizeof(uintptr_t));
}

struct gc_heap* make_heap(size_t size) {
  size = align(size, getpagesize());
  struct gc_heap *heap = malloc(sizeof(struct gc_heap));
  void *mem = mmap(NULL, size, PROT_READ|PROT_WRITE,
                   MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  heap->to_space = heap->hp = (uintptr_t) mem;
  heap->from_space = heap->limit = space->hp + size / 2;
  heap->size = size;
  return heap;
}

struct gc_obj* copy(struct gc_heap *heap, struct gc_obj *obj) {
  size_t size = heap_object_size(obj);
  struct gc_obj *new_obj = (struct gc_obj*)heap->hp;
  memcpy(new_obj, obj, size);
  forward(obj, new_obj);
  heap->hp += align_size(size);
  return new_obj;
}

void flip(struct gc_heap *heap) {
  heap->hp = heap->from_space;
  heap->from_space = heap->to_space;
  heap->to_space = heap->hp;
  heap->limit = heap->hp + heap->size / 2;
}  

void visit_field(struct gc_obj **field, struct gc_heap *heap) {
  struct gc_obj *from = *field;
  struct gc_obj *to =
    is_forwarded(from) ? forwarded(from) : copy(heap, from);
  *field = to;
}

void collect(struct gc_heap *heap) {
  flip(heap);
  uintptr_t scan = heap->hp;
  trace_roots(heap, visit_field);
  while(scan < heap->hp) {
    struct gc_obj *obj = scan;
    scan += align_size(trace_heap_object(obj, heap, visit_field));
  }
}

inline struct gc_obj* allocate(struct gc_heap *heap, size_t size) {
retry:
  uintptr_t addr = heap->hp;
  uintptr_t new_hp = align_size(addr + size);
  if (heap->limit < new_hp) {
    collect(heap);
    if (heap->limit - heap->hp < size) {
      fprintf(stderr, "out of memory\n");
      abort();
    }
    goto retry;
  }
  heap->hp = new_hp;
  return (struct gc_obj*)addr;
}
