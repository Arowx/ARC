# Files

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **Files** · [quick reference](../QUICK.md) · [full API](../API.md)


Read a whole file into the given arena (NUL-terminated). NULL on failure.

```c
ARC_API uint8_t *arc_file_read(const char *path, arc_arena *a, size_t *size_out);
ARC_API bool arc_file_write(const char *path, const void *data, size_t size);
ARC_API bool arc_file_exists(const char *path);
```

Asset packs: one .arcpak holds many files (zlib-compressed where that helps; tools/arcpak.py makes them).
   Every ARC loader (meshes, textures, sounds, fonts, arc_file_read) looks in mounted packs first, by the same
   relative path ("assets/ui/icons.png"); the newest pack wins. assets.arcpak next to the program is mounted
   at startup.

```c
ARC_API bool arc_pack_mount(const char *path);
ARC_API bool arc_pack_mount_memory(const uint8_t *data, size_t size, const char *name);
```
