#include "ltw_glue.h"

char *ltw_translate_shader(const char *src, unsigned int shader_type) {
    /* GL_VERTEX_SHADER == 0x8B31 ; everything else treated as fragment */
    glsl_stage stage = (shader_type == 0x8B31) ? STAGE_VERTEX : STAGE_FRAGMENT;
    return glsl_translate(src, stage);
}
