#ifndef LTW_GLUE_H
#define LTW_GLUE_H

#include "glsl_translate.h"

/* Map a desktop-GL shader type to our translate stage and run the LTW-style
 * translation. This is THE call LTW's glShaderSource() must make (see
 * INTEGRATION.md) -- it runs right where LTW currently calls optimize_shader().
 *
 * GL_VERTEX_SHADER   = 0x8B31
 * GL_FRAGMENT_SHADER = 0x8B30
 */
char *ltw_translate_shader(const char *src, unsigned int shader_type);

#endif /* LTW_GLUE_H */
