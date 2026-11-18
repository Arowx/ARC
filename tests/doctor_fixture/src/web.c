/* arc_doctor test fixture (0.10.9): names that are not assets to look for, so no ERROR for any of them:
   a name in a comment ("file PACK BYTES assets/x/comment_only.arcm" lines), the web manifest, this game's own WEB_PACKS
   pack, a screenshot it writes, a file it writes, and a line marked as not an asset. */
#include "arc.h"
#include <stdio.h>
void web_bits(void) {
    if (arc_file_exists("arc_packs.txt")) arc_pack_fetch("lvl.arcpak");
    arc_screenshot("view.png");
    FILE *f = fopen("notes_out.txt", "w"); if (f) fclose(f);
    const char *later = "made_at_run_time.png";   /* doctor: not an asset */
    (void)later;
}
