# Math (header-only, inlined)

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **Math (header-only, inlined)** · [quick reference](../QUICK.md) · [full API](../API.md)

```c
#define ARC_PI 3.14159265358979323846f
#define ARC_TAU 6.28318530717958647692f
typedef struct { float x, y; } arc_v2;
typedef struct { float x, y, z; } arc_v3;
typedef struct { float x, y, z, w; } arc_v4;
typedef struct { float m[16]; } arc_m4;    // column-major: m[col*4+row]
typedef struct { float x, y, z, w; } arc_quat;
float arc_minf(float a, float b);
float arc_maxf(float a, float b);
float arc_clampf(float v, float lo, float hi);
float arc_lerpf(float a, float b, float t);
int arc_mini(int a, int b);
int arc_maxi(int a, int b);
int arc_clampi(int v, int lo, int hi);
```

wrap an angle into [-PI, PI)

```c
float arc_wrap_angle(float a);
arc_v2 arc_v2_make(float x, float y);
arc_v2 arc_v2_add(arc_v2 a, arc_v2 b);
arc_v2 arc_v2_sub(arc_v2 a, arc_v2 b);
arc_v2 arc_v2_scale(arc_v2 a, float s);
float arc_v2_dot(arc_v2 a, arc_v2 b);
float arc_v2_len(arc_v2 a);
arc_v2 arc_v2_norm(arc_v2 a);
arc_v3 arc_v3_make(float x, float y, float z);
arc_v3 arc_v3_add(arc_v3 a, arc_v3 b);
arc_v3 arc_v3_sub(arc_v3 a, arc_v3 b);
arc_v3 arc_v3_mul(arc_v3 a, arc_v3 b);
arc_v3 arc_v3_scale(arc_v3 a, float s);
arc_v3 arc_v3_madd(arc_v3 a, arc_v3 b, float s);
float arc_v3_dot(arc_v3 a, arc_v3 b);
arc_v3 arc_v3_cross(arc_v3 a, arc_v3 b);
float arc_v3_len(arc_v3 a);
float arc_v3_dist(arc_v3 a, arc_v3 b);
arc_v3 arc_v3_norm(arc_v3 a);
arc_v3 arc_v3_lerp(arc_v3 a, arc_v3 b, float t);
arc_m4 arc_m4_identity(void);
arc_m4 arc_m4_mul(arc_m4 a, arc_m4 b);
arc_m4 arc_m4_translate(arc_v3 t);
arc_m4 arc_m4_scale(arc_v3 s);
arc_m4 arc_m4_rotate_y(float a);
arc_m4 arc_m4_rotate_x(float a);
arc_m4 arc_m4_rotate_z(float a);
```

translate * rotY(yaw) * scale — the common case for game objects, without three matrix multiplies

```c
arc_m4 arc_m4_trs_y(arc_v3 t, float yaw, arc_v3 s);
```

0.10.14: the yaw that turns a model (front +z, as ARC's models face) toward the direction (dx, dz): for
   arc_m4_trs_y and cam rigs. Looks only: rules code that must match on every machine uses arc_atan2f(dx, dz)

```c
float arc_yaw_to(float dx, float dz);
arc_m4 arc_m4_perspective(float fovy, float aspect, float zn, float zf);
arc_m4 arc_m4_ortho(float l, float r_, float b, float t, float zn, float zf);
arc_m4 arc_m4_look_at(arc_v3 eye, arc_v3 at, arc_v3 up);
arc_v3 arc_m4_mul_point(arc_m4 a, arc_v3 p);
arc_v4 arc_m4_mul_v4(arc_m4 a, arc_v4 p);
ARC_API arc_m4 arc_m4_inverse(arc_m4 m);
```

model matrix from an origin and three (possibly scaled, possibly mirrored) axis vectors:
   world = origin + x*bx + y*by + z*bz

```c
arc_m4 arc_m4_basis(arc_v3 origin, arc_v3 bx, arc_v3 by, arc_v3 bz);
```

shortest-path blend between two angles

```c
float arc_angle_lerp(float a, float b, float t);
arc_quat arc_quat_axis_angle(arc_v3 axis, float a);
ARC_API arc_m4 arc_m4_from_quat(arc_quat q);
ARC_API arc_quat arc_quat_mul(arc_quat a, arc_quat b);
ARC_API arc_quat arc_quat_slerp(arc_quat a, arc_quat b, float t);
```

rotate a vector by a quaternion; the shortest rotation taking direction `from` to `to`

```c
arc_v3 arc_quat_rotate(arc_quat q, arc_v3 v);
ARC_API arc_quat arc_quat_from_to(arc_v3 from, arc_v3 to);
```

any axis (need not be unit length), radians

```c
arc_m4 arc_m4_rotate(arc_v3 axis, float a);
```

translate * rotate(q) * scale: a full transform, as glTF and most editors store them

```c
arc_m4 arc_m4_trs(arc_v3 t, arc_quat q, arc_v3 s);
```

shear: x += xy*y + xz*z, y += yx*x + yz*z, z += zx*x + zy*y (a leaning tower: arc_m4_shear(0.2f, 0, 0, 0, 0, 0))

```c
arc_m4 arc_m4_shear(float xy, float xz, float yx, float yz, float zx, float zy);
```

a direction through a matrix (no translation)

```c
arc_v3 arc_m4_mul_dir(arc_m4 a, arc_v3 d);
```

chain several transforms: arc_m4_chain(3, parent, local, offset) = parent * local * offset

```c
ARC_API arc_m4 arc_m4_chain(int count, ...);
```
