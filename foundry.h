#ifndef __FOUNDRY_H__
#define __FOUNDRY_H__

#include <stdbool.h>
#include <stdint.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cairo-ft.h>

struct _teddy_fontset_t;
typedef struct _teddy_fontset_t teddy_fontset_t;


void foundry_init(void);
teddy_fontset_t *foundry_lookup(const char *name, bool lock);
void foundry_release(teddy_fontset_t *font);

int fontset_fontidx(teddy_fontset_t *fontset, uint32_t code);
FT_UInt fontset_glyph_index(teddy_fontset_t *fontset, int fontidx, uint32_t code);
double fontset_get_kerning(teddy_fontset_t *fontset, int fontidx, FT_UInt previous_glyph, FT_UInt glyph);
double fontset_x_advance(teddy_fontset_t *fontset, int fontidx, FT_UInt glyph);

cairo_scaled_font_t *fontset_get_cairofont(teddy_fontset_t *fontset, int fontidx);
cairo_scaled_font_t *fontset_get_cairofont_by_name(const char *name, int fontidx);

void foundry_free(void);

#endif
