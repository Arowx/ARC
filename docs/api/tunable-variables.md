# Tunable variables (agents can list/set them: `vars`, `set name value`)

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **Tunable variables (agents can list/set them: `vars`, `set name value`)** · [quick reference](../QUICK.md) · [full API](../API.md)


Register a float the game reads every tick. Returns the pointer you pass in, so:
    static float speed; arc_var("player.speed", &speed, 6.0f);   (call in init)

```c
ARC_API float *arc_var(const char *name, float *ptr, float initial);
ARC_API bool arc_var_set(const char *name, float value);
ARC_API float arc_var_get(const char *name);
```
