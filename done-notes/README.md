# From Babel (D:\Projects\TowerOfBabel), for ARC

| File | Babel path | What it is |
|---|---|---|
| `shell.html` | `web/shell.html` | The game's web page: Babel title, umber/parchment colours, a CSS spinner instead of "loading" text, and Space/Tab/arrow keys kept from scrolling the itch page. `build.sh`/`build.bat web` pick up `web/shell.html` when it exists. |
| `dye_textures.py` | `art_src/dye_textures.py` | Recolours the red parts of a Ludo GLB's base texture to each tongue's colour, keeping light and shade. `dye_textures.py MODEL.glb OUTDIR NAME [strict]`; `strict` also requires g/r < 0.4, so orange-brown wood isn't caught. Writes `NAME_0..5.png`. |
| `tongue_textures.py` | `art_src/tongue_textures.py` | Needed by `dye_textures.py`: it imports `glb_base_texture` (reads the first material's base-colour image out of a GLB) and `TONGUES` (the six colours) from here. |
| `sfx_prep.sh` | `art_src/sfx_prep.sh` | ffmpeg: trims silence, levels to about −20 dB mean with a 0.7 limiter, adds short fades; `wind` is a loop, so it only gets loudnorm (no trim or fades). Reads `art_src/sfx_raw/`, writes `assets/sfx/*.mp3`. |
| `voice_prep.sh` | `art_src/voice_prep.sh` | ffmpeg: loudnorm and silence trim for the narrator lines. `art_src/voice_raw/` to `assets/voice/*.mp3`. |
| `compose.py` | `art_src/itch/compose.py` | The itch cover (630×500 and 2×) and banner (1920×800, 960×400): crops Ludo art and adds a nine-sliced parchment plaque with the title. **Paths are hard-coded** (`/root/Projects/TowerOfBabel/assets/ui/panel.png`, `/tmp/fonts/fellsc.ttf`, `/tmp/itch/ludo/*`, `/tmp/itch/out/`), so they need to be parameters in a reusable tool. |
| `tent.glb` | `art_src/glb/tent.glb` | A Ludo model made tonight (`create3DModel`, model "prop", 6000 faces, 1024 texture). Its material has `baseColorFactor` **1.0**. |
| `houseB.glb` | `art_src/glb/houseB.glb` | An older Ludo model (made about 10 hours earlier). Its material has `baseColorFactor` **0.4**, the darkening that `--ignore-color-factor` exists for. |

**On recognising Ludo files automatically:** Babel has 20 Ludo GLBs. The 18 older ones all have `baseColorFactor` [0.4, 0.4, 0.4, 1]; the two from tonight (`tent`, `cabin`) have [1, 1, 1, 1]. All 20 say `"generator": "https://github.com/mikedh/trimesh"`, which is a generic Python library and doesn't identify Ludo. So Ludo seems to have stopped writing the 0.4, and the generator field can't tell you a file is from Ludo. A safer rule might be: a textured material whose factor is a flat grey (r = g = b < 1, alpha 1) is ignored, with a note in the converter's output.

The tools were run from the project root (paths like `art_src/...` and `assets/...` are relative to it). They need Python 3 with numpy and Pillow, and ffmpeg on PATH.
