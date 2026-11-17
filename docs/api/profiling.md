# Profiling

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **Profiling** · [quick reference](../QUICK.md) · [full API](../API.md)


Named CPU timers, summed per frame. Shown by `stats`, included in dumps.
  ARC_PROF("physics") { ...code... }

```c
ARC_API int arc_prof_begin(const char *name);
ARC_API void arc_prof_end(int id);
#define ARC_PROF(name)
```
