#include "LaoText.h"

extern const lv_font_t notosanslao_regular_25;
extern const lv_font_t notosanslao_regular_20;

namespace {

constexpr uint32_t kVariantStart = 0xE000;
constexpr uint8_t kVariantStride = 64;
constexpr uint8_t kLoweredFlag = 32;
constexpr uint32_t kMarks[] = {
    0x0EB1, 0x0EB4, 0x0EB5, 0x0EB6, 0x0EB7, 0x0EB8, 0x0EB9,
    0x0EBB, 0x0EBC, 0x0EC8, 0x0EC9, 0x0ECA, 0x0ECB, 0x0ECC, 0x0ECD,
    0x0EB3, // Lao Vowel Sign Am
};

bool markIndex(uint32_t codepoint, uint8_t& index) {
  for (uint8_t i = 0; i < sizeof(kMarks) / sizeof(kMarks[0]); ++i) {
    if (kMarks[i] == codepoint) {
      index = i;
      return true;
    }
  }
  return false;
}

bool isUpperMark(uint32_t codepoint) {
  return codepoint == 0x0EB1 || (codepoint >= 0x0EB4 && codepoint <= 0x0EB7) ||
         codepoint == 0x0EBB || codepoint == 0x0ECD || codepoint == 0x0EB3;
}

bool isToneMark(uint32_t codepoint) {
  return codepoint >= 0x0EC8 && codepoint <= 0x0ECB;
}

bool isLaoConsonant(uint32_t codepoint) {
  return codepoint >= 0x0E81 && codepoint <= 0x0EAE;
}

uint32_t decodeUtf8(const char* text, size_t length, size_t& offset) {
  const uint8_t first = static_cast<uint8_t>(text[offset++]);
  if (first < 0x80) return first;
  if ((first & 0xE0) == 0xC0 && offset < length) {
    return ((first & 0x1F) << 6) | (static_cast<uint8_t>(text[offset++]) & 0x3F);
  }
  if ((first & 0xF0) == 0xE0 && offset + 1 < length) {
    uint32_t value = (first & 0x0F) << 12;
    value |= (static_cast<uint8_t>(text[offset++]) & 0x3F) << 6;
    value |= static_cast<uint8_t>(text[offset++]) & 0x3F;
    return value;
  }
  if ((first & 0xF8) == 0xF0 && offset + 2 < length) {
    uint32_t value = (first & 0x07) << 18;
    value |= (static_cast<uint8_t>(text[offset++]) & 0x3F) << 12;
    value |= (static_cast<uint8_t>(text[offset++]) & 0x3F) << 6;
    value |= static_cast<uint8_t>(text[offset++]) & 0x3F;
    return value;
  }
  return 0xFFFD;
}

void appendUtf8(String& output, uint32_t codepoint) {
  if (codepoint < 0x80) {
    output += static_cast<char>(codepoint);
  } else if (codepoint < 0x800) {
    output += static_cast<char>(0xC0 | (codepoint >> 6));
    output += static_cast<char>(0x80 | (codepoint & 0x3F));
  } else if (codepoint < 0x10000) {
    output += static_cast<char>(0xE0 | (codepoint >> 12));
    output += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    output += static_cast<char>(0x80 | (codepoint & 0x3F));
  } else {
    output += static_cast<char>(0xF0 | (codepoint >> 18));
    output += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
    output += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    output += static_cast<char>(0x80 | (codepoint & 0x3F));
  }
}

uint32_t decodeVariant(uint32_t codepoint, uint8_t* baseWidth = nullptr,
                       bool* lowered = nullptr) {
  const uint32_t variantCount = (sizeof(kMarks) / sizeof(kMarks[0])) * kVariantStride;
  if (codepoint < kVariantStart || codepoint >= kVariantStart + variantCount) return codepoint;
  const uint32_t encoded = codepoint - kVariantStart;
  const uint8_t variant = encoded % kVariantStride;
  if (baseWidth) *baseWidth = variant & 0x1F;
  if (lowered) *lowered = (variant & kLoweredFlag) != 0;
  return kMarks[encoded / kVariantStride];
}

bool shapedGlyphDescriptor(const lv_font_t*, lv_font_glyph_dsc_t* descriptor,
                           uint32_t codepoint, uint32_t nextCodepoint) {
  uint8_t baseWidth = 0;
  bool lowered = false;
  const uint32_t original = decodeVariant(codepoint, &baseWidth, &lowered);
  const uint32_t originalNext = decodeVariant(nextCodepoint);

  // If U+0EB3 (vowel am) variant, let it render completely natively (keep native ofs_x, ofs_y, and adv_w)
  if (original == 0x0EB3) {
    const bool found = notosanslao_regular_25.get_glyph_dsc(
        &notosanslao_regular_25, descriptor, original, originalNext);
    return found;
  }

  // Look up lowered glyphs from the font if the mark needs to be lowered
  uint32_t lookupOriginal = original;
  if (lowered) {
    if (original == 0x0EC8) lookupOriginal = 0xE001;      // lowered tone ່
    else if (original == 0x0EC9) lookupOriginal = 0xE002; // lowered tone ້
    else if (original == 0x0ECA) lookupOriginal = 0xE003; // lowered tone ໊
    else if (original == 0x0ECB) lookupOriginal = 0xE004; // lowered tone ໋
    else if (original == 0x0ECC) lookupOriginal = 0xE005; // lowered Garan ໌
  }

  const bool found = notosanslao_regular_25.get_glyph_dsc(
      &notosanslao_regular_25, descriptor, lookupOriginal, originalNext);
  if (!found || !baseWidth) return found;

  // Keep the font's native ofs_x (do NOT override it with centeredOffset),
  // as Noto Serif Lao has pre-calibrated offsets for perfect alignment.
  descriptor->adv_w = 0;
  return true;
}

const uint8_t* shapedGlyphBitmap(const lv_font_t*, uint32_t codepoint) {
  bool lowered = false;
  const uint32_t original = decodeVariant(codepoint, nullptr, &lowered);
  uint32_t lookupOriginal = original;
  if (lowered) {
    if (original == 0x0EC8) lookupOriginal = 0xE001;      // lowered tone ່
    else if (original == 0x0EC9) lookupOriginal = 0xE002; // lowered tone ້
    else if (original == 0x0ECA) lookupOriginal = 0xE003; // lowered tone ໊
    else if (original == 0x0ECB) lookupOriginal = 0xE004; // lowered tone ໋
    else if (original == 0x0ECC) lookupOriginal = 0xE005; // lowered Garan ໌
  }
  return notosanslao_regular_25.get_glyph_bitmap(
      &notosanslao_regular_25, lookupOriginal);
}

bool shapedGlyphDescriptor_20(const lv_font_t*, lv_font_glyph_dsc_t* descriptor,
                            uint32_t codepoint, uint32_t nextCodepoint) {
  uint8_t baseWidth = 0;
  bool lowered = false;
  const uint32_t original = decodeVariant(codepoint, &baseWidth, &lowered);
  const uint32_t originalNext = decodeVariant(nextCodepoint);

  // If U+0EB3 (vowel am) variant, let it render completely natively (keep native ofs_x, ofs_y, and adv_w)
  if (original == 0x0EB3) {
    const bool found = notosanslao_regular_20.get_glyph_dsc(
        &notosanslao_regular_20, descriptor, original, originalNext);
    return found;
  }

  // Look up lowered glyphs from the font if the mark needs to be lowered
  uint32_t lookupOriginal = original;
  if (lowered) {
    if (original == 0x0EC8) lookupOriginal = 0xE001;      // lowered tone ່
    else if (original == 0x0EC9) lookupOriginal = 0xE002; // lowered tone ້
    else if (original == 0x0ECA) lookupOriginal = 0xE003; // lowered tone ໊
    else if (original == 0x0ECB) lookupOriginal = 0xE004; // lowered tone ໋
    else if (original == 0x0ECC) lookupOriginal = 0xE005; // lowered Garan ໌
  }

  const bool found = notosanslao_regular_20.get_glyph_dsc(
      &notosanslao_regular_20, descriptor, lookupOriginal, originalNext);
  if (!found || !baseWidth) return found;

  // Keep the font's native ofs_x (do NOT override it with centeredOffset),
  // as Noto Serif Lao has pre-calibrated offsets for perfect alignment.
  descriptor->adv_w = 0;
  return true;
}

const uint8_t* shapedGlyphBitmap_20(const lv_font_t*, uint32_t codepoint) {
  bool lowered = false;
  const uint32_t original = decodeVariant(codepoint, nullptr, &lowered);
  uint32_t lookupOriginal = original;
  if (lowered) {
    if (original == 0x0EC8) lookupOriginal = 0xE001;      // lowered tone ່
    else if (original == 0x0EC9) lookupOriginal = 0xE002; // lowered tone ້
    else if (original == 0x0ECA) lookupOriginal = 0xE003; // lowered tone ໊
    else if (original == 0x0ECB) lookupOriginal = 0xE004; // lowered tone ໋
    else if (original == 0x0ECC) lookupOriginal = 0xE005; // lowered Garan ໌
  }
  return notosanslao_regular_20.get_glyph_bitmap(
      &notosanslao_regular_20, lookupOriginal);
}

}  // namespace

namespace washerui {

String shapeLaoText(const char* text) {
  if (!text || !*text) return String();
  const size_t length = strlen(text);
  String output;
  output.reserve(length + 8);
  uint8_t baseWidth = 0;
  bool hasUpperMark = false;

  for (size_t offset = 0; offset < length;) {
    const uint32_t codepoint = decodeUtf8(text, length, offset);
    if (codepoint >= kVariantStart && codepoint < kVariantStart + sizeof(kMarks) / sizeof(kMarks[0]) * kVariantStride) {
      appendUtf8(output, codepoint);
      continue;
    }

    uint8_t index = 0;
    if (markIndex(codepoint, index) && baseWidth) {
      const uint8_t width = baseWidth > 31 ? 31 : baseWidth;
      bool nextIsUpper = false;
      if (offset < length) {
        size_t nextOffset = offset;
        uint32_t nextCp = decodeUtf8(text, length, nextOffset);
        if (isUpperMark(nextCp)) {
          nextIsUpper = true;
        }
      }
      const uint8_t flags = isToneMark(codepoint) && !(hasUpperMark || nextIsUpper) ? kLoweredFlag : 0;
      appendUtf8(output, kVariantStart + index * kVariantStride + flags + width);
      if (isUpperMark(codepoint)) hasUpperMark = true;
      continue;
    }

    appendUtf8(output, codepoint);
    hasUpperMark = false;
    baseWidth = 0;
    if (isLaoConsonant(codepoint)) {
      lv_font_glyph_dsc_t descriptor{};
      if (notosanslao_regular_25.get_glyph_dsc(
              &notosanslao_regular_25, &descriptor, codepoint, 0)) {
        baseWidth = descriptor.adv_w;
      }
    }
  }
  return output;
}

bool laoShapingSelfTest() {
  const String shaped = shapeLaoText("ເຄື່ອງ");
  uint8_t variants = 0;
  uint8_t raisedTones = 0;
  const char* text = shaped.c_str();
  const size_t length = shaped.length();
  for (size_t offset = 0; offset < length;) {
    const uint32_t codepoint = decodeUtf8(text, length, offset);
    uint8_t width = 0;
    bool lowered = false;
    const uint32_t original = decodeVariant(codepoint, &width, &lowered);
    if (original != codepoint) {
      ++variants;
      if (isToneMark(original) && !lowered) ++raisedTones;
      lv_font_glyph_dsc_t descriptor{};
      if (!notosanslao_shaped_25.get_glyph_dsc(
              &notosanslao_shaped_25, &descriptor, codepoint, 0)) return false;
    }
  }
  return variants >= 2 && raisedTones >= 1;
}

}  // namespace washerui

const lv_font_t notosanslao_shaped_25 = {
    .get_glyph_dsc = shapedGlyphDescriptor,
    .get_glyph_bitmap = shapedGlyphBitmap,
    .line_height = 29,
    .base_line = 6,
    .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = -2,
    .underline_thickness = 1,
    .dsc = nullptr,
    .fallback = nullptr,
    .user_data = nullptr,
};

const lv_font_t notosanslao_shaped_20 = {
    .get_glyph_dsc = shapedGlyphDescriptor_20,
    .get_glyph_bitmap = shapedGlyphBitmap_20,
    .line_height = 23,
    .base_line = 5,
    .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = -2,
    .underline_thickness = 1,
    .dsc = nullptr,
    .fallback = nullptr,
    .user_data = nullptr,
};
