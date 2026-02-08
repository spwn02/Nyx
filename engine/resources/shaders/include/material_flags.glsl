// Must match MaterialTypes.h flags.
const uint MAT_FLAG_TANGENT_SPACE_NORMAL = (1u << 7);

uint materialFlagsFromPackedFloat(float f) {
  return floatBitsToUint(f);
}
