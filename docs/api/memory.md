# Memory

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **Memory** · [quick reference](../QUICK.md) · [full API](../API.md)


Linear (bump) allocator. Nothing is freed individually; reset or pop to a mark.

```c
typedef struct { uint8_t *base; size_t used, cap; size_t peak; } arc_arena;
ARC_API void arc_arena_init(arc_arena *a, void *mem, size_t cap);
ARC_API void *arc_arena_alloc(arc_arena *a, size_t size, size_t align);    // zeroed; returns NULL when full
#define arc_arena_new(a, T, n) ((T *)arc_arena_alloc((a), sizeof(T) * (size_t)(n), alignof(T)))
size_t arc_arena_mark(arc_arena *a);
void arc_arena_pop(arc_arena *a, size_t mark);
void arc_arena_reset(arc_arena *a);
```

Engine arenas. perm lives for the program; frame is reset at the start of every frame.

```c
ARC_API arc_arena *arc_perm(void);
ARC_API arc_arena *arc_frame(void);
ARC_API char *arc_fmt(const char *fmt, ...);    // printf into the frame arena
```

A named, zeroed block that lives until the program ends and survives code reloads (hot-reload builds):
   the same name gives the same memory. G = arc_persist("game", sizeof *G); in init() and reloaded().

```c
ARC_API void *arc_persist(const char *name, size_t size);
```

Fixed-capacity pool of same-sized items with generation-checked handles.
Iterate live items with: for (int i = 0; i < p.cap; i++) if (arc_pool_alive(&p, i)) {...}

```c
typedef struct { uint32_t v; } arc_handle;    // 0 = invalid. low 20 bits index+1, high 12 bits generation
typedef struct { uint8_t *items; uint16_t *gen; uint8_t *alive; uint32_t *free_list; int item_size, cap, count, free_top; } arc_pool;
ARC_API void arc_pool_init(arc_pool *p, arc_arena *a, int item_size, int cap);
#define arc_pool_make(p, a, T, cap) arc_pool_init((p), (a), (int)sizeof(T), (cap))
ARC_API arc_handle arc_pool_add(arc_pool *p);    // zeroed item; invalid handle when full
ARC_API void arc_pool_remove(arc_pool *p, arc_handle h);
ARC_API void *arc_pool_get(arc_pool *p, arc_handle h);    // NULL if stale
bool arc_pool_alive(const arc_pool *p, int i);
void *arc_pool_at(const arc_pool *p, int i);
ARC_API arc_handle arc_pool_handle_at(const arc_pool *p, int i);
```

Snapshots for arc_app.save_state / load_state. The writer counts bytes even past cap (so return w.len and
ARC calls again with room enough); the reader fails safely on short data.
    size_t save(void *buf, size_t cap) { arc_writer w = { buf, cap }; arc_write(&w, &G.game, sizeof G.game); arc_write_pool(&w, &G.enemies); return w.len; }
    bool load(const void *buf, size_t n) { arc_reader r = { buf, n }; arc_read(&r, &G.game, sizeof G.game); arc_read_pool(&r, &G.enemies); return !r.fail; }

```c
typedef struct { uint8_t *buf; size_t cap, len; } arc_writer;
typedef struct { const uint8_t *data; size_t size, pos; bool fail; } arc_reader;
ARC_API void arc_write(arc_writer *w, const void *p, size_t n);
ARC_API bool arc_read(arc_reader *r, void *p, size_t n);
ARC_API void arc_write_pool(arc_writer *w, const arc_pool *p);    // the items and the free list (the pool must be the same size when read)
ARC_API bool arc_read_pool(arc_reader *r, arc_pool *p);
```
