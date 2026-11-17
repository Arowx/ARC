# JSON writer (for dump() and tools)

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **JSON writer (for dump() and tools)** · [quick reference](../QUICK.md) · [full API](../API.md)


Inside dump(), you are already inside an object: write members with the *_k functions.
  arc_json_num_k(j, "score", score);
  arc_json_arr_k(j, "enemies"); for (...) { arc_json_obj(j); arc_json_num_k(j,"x",e->x); arc_json_end(j); } arc_json_end(j);

```c
ARC_API void arc_json_obj(arc_json *j);    // begin unnamed object (inside array)
ARC_API void arc_json_arr(arc_json *j);
ARC_API void arc_json_obj_k(arc_json *j, const char *key);
ARC_API void arc_json_arr_k(arc_json *j, const char *key);
ARC_API void arc_json_end(arc_json *j);
ARC_API void arc_json_num(arc_json *j, double v);
ARC_API void arc_json_int(arc_json *j, long long v);
ARC_API void arc_json_str(arc_json *j, const char *s);
ARC_API void arc_json_bool(arc_json *j, bool b);
ARC_API void arc_json_num_k(arc_json *j, const char *key, double v);
ARC_API void arc_json_int_k(arc_json *j, const char *key, long long v);
ARC_API void arc_json_str_k(arc_json *j, const char *key, const char *s);
ARC_API void arc_json_bool_k(arc_json *j, const char *key, bool b);
ARC_API void arc_json_v3_k(arc_json *j, const char *key, arc_v3 v);    // [x,y,z]
```
