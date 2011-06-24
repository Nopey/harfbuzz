/*
 * Copyright © 2011  Google, Inc.
 *
 *  This is part of HarfBuzz, a text shaping library.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and its documentation for any purpose, provided that the
 * above copyright notice and the following two paragraphs appear in
 * all copies of this software.
 *
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN
 * IF THE COPYRIGHT HOLDER HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE COPYRIGHT HOLDER HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 * Google Author(s): Behdad Esfahbod
 */

#include "hb-ot-shape-complex-private.hh"

HB_BEGIN_DECLS


/* buffer var allocations */
#define indic_category() var2.u8[0] /* indic_category_t */
#define indic_position() var2.u8[1] /* indic_matra_category_t */

#define INDIC_TABLE_ELEMENT_TYPE uint8_t

/* Cateories used in the OpenType spec:
 * https://www.microsoft.com/typography/otfntdev/devanot/shaping.aspx
 */
/* Note: This enum is duplicated in the -machine.rl source file.
 * Not sure how to avoid duplication. */
enum indic_category_t {
  OT_X = 0,
  OT_C,
  OT_Ra, /* Not explicitly listed in the OT spec, but used in the grammar. */
  OT_V,
  OT_N,
  OT_H,
  OT_ZWNJ,
  OT_ZWJ,
  OT_M,
  OT_SM,
  OT_VD,
  OT_A,
  OT_NBSP
};

/* Categories used in IndicSyllabicCategory.txt from UCD */
/* The assignments are guesswork */
enum indic_syllabic_category_t {
  INDIC_SYLLABIC_CATEGORY_OTHER			= OT_X,

  INDIC_SYLLABIC_CATEGORY_AVAGRAHA		= OT_X,
  INDIC_SYLLABIC_CATEGORY_BINDU			= OT_SM,
  INDIC_SYLLABIC_CATEGORY_CONSONANT		= OT_C,
  INDIC_SYLLABIC_CATEGORY_CONSONANT_DEAD	= OT_C,
  INDIC_SYLLABIC_CATEGORY_CONSONANT_FINAL	= OT_C,
  INDIC_SYLLABIC_CATEGORY_CONSONANT_HEAD_LETTER	= OT_C,
  INDIC_SYLLABIC_CATEGORY_CONSONANT_MEDIAL	= OT_C,
  INDIC_SYLLABIC_CATEGORY_CONSONANT_PLACEHOLDER	= OT_NBSP,
  INDIC_SYLLABIC_CATEGORY_CONSONANT_SUBJOINED	= OT_C,
  INDIC_SYLLABIC_CATEGORY_CONSONANT_REPHA	= OT_C,
  INDIC_SYLLABIC_CATEGORY_MODIFYING_LETTER	= OT_X,
  INDIC_SYLLABIC_CATEGORY_NUKTA			= OT_N,
  INDIC_SYLLABIC_CATEGORY_REGISTER_SHIFTER	= OT_X,
  INDIC_SYLLABIC_CATEGORY_TONE_LETTER		= OT_X,
  INDIC_SYLLABIC_CATEGORY_TONE_MARK		= OT_X,
  INDIC_SYLLABIC_CATEGORY_VIRAMA		= OT_H,
  INDIC_SYLLABIC_CATEGORY_VISARGA		= OT_SM,
  INDIC_SYLLABIC_CATEGORY_VOWEL			= OT_V,
  INDIC_SYLLABIC_CATEGORY_VOWEL_DEPENDENT	= OT_M,
  INDIC_SYLLABIC_CATEGORY_VOWEL_INDEPENDENT	= OT_V
};

/* Categories used in IndicSMatraCategory.txt from UCD */
enum indic_matra_category_t {
  INDIC_MATRA_CATEGORY_NOT_APPLICABLE		= 0,

  INDIC_MATRA_CATEGORY_LEFT			= 0x01,
  INDIC_MATRA_CATEGORY_TOP			= 0x02,
  INDIC_MATRA_CATEGORY_BOTTOM			= 0x04,
  INDIC_MATRA_CATEGORY_RIGHT			= 0x08,

  /* We don't really care much about these since we decompose them
   * in the generic pre-shaping layer. */
  INDIC_MATRA_CATEGORY_BOTTOM_AND_RIGHT		= INDIC_MATRA_CATEGORY_BOTTOM +
						  INDIC_MATRA_CATEGORY_RIGHT,
  INDIC_MATRA_CATEGORY_LEFT_AND_RIGHT		= INDIC_MATRA_CATEGORY_LEFT +
						  INDIC_MATRA_CATEGORY_RIGHT,
  INDIC_MATRA_CATEGORY_TOP_AND_BOTTOM		= INDIC_MATRA_CATEGORY_TOP +
						  INDIC_MATRA_CATEGORY_BOTTOM,
  INDIC_MATRA_CATEGORY_TOP_AND_BOTTOM_AND_RIGHT	= INDIC_MATRA_CATEGORY_TOP +
						  INDIC_MATRA_CATEGORY_BOTTOM +
						  INDIC_MATRA_CATEGORY_RIGHT,
  INDIC_MATRA_CATEGORY_TOP_AND_LEFT		= INDIC_MATRA_CATEGORY_TOP +
						  INDIC_MATRA_CATEGORY_LEFT,
  INDIC_MATRA_CATEGORY_TOP_AND_LEFT_AND_RIGHT	= INDIC_MATRA_CATEGORY_TOP +
						  INDIC_MATRA_CATEGORY_LEFT +
						  INDIC_MATRA_CATEGORY_RIGHT,
  INDIC_MATRA_CATEGORY_TOP_AND_RIGHT		= INDIC_MATRA_CATEGORY_TOP +
						  INDIC_MATRA_CATEGORY_RIGHT,

  INDIC_MATRA_CATEGORY_INVISIBLE		= INDIC_MATRA_CATEGORY_NOT_APPLICABLE,
  INDIC_MATRA_CATEGORY_OVERSTRUCK		= INDIC_MATRA_CATEGORY_NOT_APPLICABLE,
  INDIC_MATRA_CATEGORY_VISUAL_ORDER_LEFT	= INDIC_MATRA_CATEGORY_NOT_APPLICABLE
};

#define INDIC_COMBINE_CATEGORIES(S,M) \
  (ASSERT_STATIC_EXPR (M == INDIC_MATRA_CATEGORY_NOT_APPLICABLE || (S == INDIC_SYLLABIC_CATEGORY_VIRAMA || S == INDIC_SYLLABIC_CATEGORY_VOWEL_DEPENDENT)), \
   ASSERT_STATIC_EXPR (S < 16 && M < 16), \
   (M << 4) | S)

#include "hb-ot-shape-complex-indic-table.hh"


static const struct {
  hb_tag_t tag;
  hb_bool_t is_global;
} indic_basic_features[] =
{
  {HB_TAG('n','u','k','t'), true},
  {HB_TAG('a','k','h','n'), false},
  {HB_TAG('r','p','h','f'), false},
  {HB_TAG('r','k','r','f'), false},
  {HB_TAG('p','r','e','f'), false},
  {HB_TAG('b','l','w','f'), false},
  {HB_TAG('h','a','l','f'), false},
  {HB_TAG('v','a','t','u'), true},
  {HB_TAG('p','s','t','f'), false},
  {HB_TAG('c','j','c','t'), true},
};

/* Same order as the indic_basic_features array */
enum {
  _NUKT,
  AKHN,
  RPHF,
  RKRF,
  PREF,
  BLWF,
  HALF,
  _VATU,
  PSTF,
  _CJCT,
};

static const hb_tag_t indic_other_features[] =
{
  HB_TAG('p','r','e','s'),
  HB_TAG('a','b','v','s'),
  HB_TAG('b','l','w','s'),
  HB_TAG('p','s','t','s'),
  HB_TAG('h','a','l','n'),

  HB_TAG('d','i','s','t'),
  HB_TAG('a','b','v','m'),
  HB_TAG('b','l','w','m'),
};


void
_hb_ot_shape_complex_collect_features_indic (hb_ot_shape_planner_t *planner, const hb_segment_properties_t *props HB_UNUSED)
{
  for (unsigned int i = 0; i < ARRAY_LENGTH (indic_basic_features); i++)
    planner->map.add_bool_feature (indic_basic_features[i].tag, indic_basic_features[i].is_global);

  for (unsigned int i = 0; i < ARRAY_LENGTH (indic_other_features); i++)
    planner->map.add_bool_feature (indic_other_features[i], true);
}



#include "hb-ot-shape-complex-indic-machine.hh"


void
_hb_ot_shape_complex_setup_masks_indic	(hb_ot_shape_context_t *c)
{
  unsigned int count = c->buffer->len;

  for (unsigned int i = 0; i < count; i++)
  {
    unsigned int type = get_indic_categories (c->buffer->info[i].codepoint);

    c->buffer->info[i].indic_category() = type & 0x0F;
    c->buffer->info[i].indic_position() = type >> 4;
  }

  find_syllables (c);

  hb_mask_t mask_array[ARRAY_LENGTH (indic_basic_features)] = {0};
  unsigned int num_masks = ARRAY_LENGTH (indic_basic_features);
  for (unsigned int i = 0; i < num_masks; i++)
    mask_array[i] = c->plan->map.get_1_mask (indic_basic_features[i].tag);
}


HB_END_DECLS
