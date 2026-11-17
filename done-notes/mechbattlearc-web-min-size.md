# Request from MechBattleArc (Molten Breach): web canvas below min size

**What happens.** In `plat_web.c`, a resizable app makes the canvas fill the page. Its backbuffer is
`max(css size x dpr, min_width/min_height)`. When the page (for example an itch.io embed, or a small browser window)
is smaller than the minimum in CSS pixels times dpr, the backbuffer is clamped up to the minimum, but the canvas
element keeps the page's CSS size. The browser then squashes the frame non-uniformly: text is stretched one way and
mouse positions map wrongly if only one axis is clamped.

**Ask.** When either side would fall below its minimum, keep the aspect ratio. Either:
- (a) scale the backbuffer up uniformly until both sides meet the minimum (`k = max(min_w / w, min_h / h)`,
  `w *= k`, `h *= k`), or
- (b) set the canvas's CSS size to the minimum and let the page scroll.

(a) looks better. Mouse mapping already divides by the CSS size, so it should stay correct.

**Game side (done).** Molten Breach now sets `min_width 960 / min_height 540` and its HUD scales by the shorter side,
so this only matters for frames smaller than 960x540 CSS pixels.
