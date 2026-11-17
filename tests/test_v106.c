/* ARC 0.10.6 self-test: which paint-pattern part a joint name counts as (shoulders and clavicles with the arms, the
   game's own rules from arc_paint_parts_rule). Included by test_core.c; v106_init() runs in init. Names start with v106_. */
static void v106_init(void) {
    CHECK(arc__joint_part("mixamorig:LeftShoulder") == ARC_PART_ARMS && arc__joint_part("RightShoulder") == ARC_PART_ARMS);
    CHECK(arc__joint_part("clavicle_l") == ARC_PART_ARMS && arc__joint_part("Bip01 R Clavicle") == ARC_PART_ARMS);
    CHECK(arc__joint_part("mixamorig:LeftArm") == ARC_PART_ARMS && arc__joint_part("mixamorig:Head") == ARC_PART_HEAD);
    CHECK(arc__joint_part("Spine2") == ARC_PART_TORSO && arc__joint_part("Neck") == ARC_PART_TORSO && arc__joint_part("LeftUpLeg") == ARC_PART_LEGS);
    CHECK(arc__joint_part("Pauldron_L") == ARC_PART_TORSO);
    arc_paint_parts_rule("*Pauldron*", ARC_PART_ARMS);
    arc_paint_parts_rule("*shoulder*", ARC_PART_TORSO);            /* the game's rule wins over the built-in names */
    arc_paint_parts_rule("tail??", ARC_PART_LEGS);
    CHECK(arc__joint_part("Pauldron_L") == ARC_PART_ARMS && arc__joint_part("mixamorig:LeftShoulder") == ARC_PART_TORSO);
    CHECK(arc__joint_part("Tail01") == ARC_PART_LEGS && arc__joint_part("Tail1") == ARC_PART_TORSO && arc__joint_part("Tail012") == ARC_PART_TORSO);
    arc_paint_parts_rule("*SHOULDER*", ARC_PART_HEAD);            /* case ignored: replaces the rule */
    CHECK(arc__joint_part("RightShoulder") == ARC_PART_HEAD);
    arc_paint_parts_rule("*shoulder*", 0);                         /* removed: the built-in names again */
    CHECK(arc__joint_part("RightShoulder") == ARC_PART_ARMS && arc__joint_part("Pauldron_R") == ARC_PART_ARMS);
    arc_paint_parts_rule("*", ARC_PART_LEGS); arc_paint_parts_rule("*head*", ARC_PART_HEAD);   /* the latest rule first */
    CHECK(arc__joint_part("Head") == ARC_PART_HEAD && arc__joint_part("Spine") == ARC_PART_LEGS);
    arc_paint_parts_rule(NULL, 0);                                 /* all gone */
    CHECK(arc__joint_part("Spine") == ARC_PART_TORSO && arc__joint_part("Pauldron_L") == ARC_PART_TORSO);
}
