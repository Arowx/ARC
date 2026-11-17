# Random numbers (deterministic, PCG32)

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **Random numbers (deterministic, PCG32)** · [quick reference](../QUICK.md) · [full API](../API.md)

```c
typedef struct { uint64_t state, inc; } arc_rng;
ARC_API void arc_rng_seed(arc_rng *r, uint64_t seed, uint64_t stream);
ARC_API uint32_t arc_rng_u32(arc_rng *r);
ARC_API int arc_rng_range(arc_rng *r, int lo, int hi);    // inclusive lo..hi
ARC_API float arc_rng_float(arc_rng *r);    // [0,1)
ARC_API float arc_rng_rangef(arc_rng *r, float lo, float hi);
```

the engine's own generator, seeded from --seed (default 1); use it in tick() only

```c
ARC_API uint32_t arc_rand_u32(void);
ARC_API int arc_rand_range(int lo, int hi);
ARC_API float arc_rand_float(void);
ARC_API float arc_rand_rangef(float lo, float hi);
ARC_API uint64_t arc_seed(void);
```

Noise (0.8.4): smooth value noise for patches, masks and density maps. Same numbers on every machine (integer hash,
   no tables), so it can drive game state. value2: 0..1, one random value per integer lattice point, smoothstep
   between them (features about 1 unit across). fbm2: octaves of it (each twice the frequency, half the weight),
   still 0..1; octaves 0 = 5. A different seed is a different, unrelated pattern.

```c
ARC_API float arc_noise_value2(float x, float z, uint32_t seed);
ARC_API float arc_noise_fbm2(float x, float z, int octaves, uint32_t seed);
```
