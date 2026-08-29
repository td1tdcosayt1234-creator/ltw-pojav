#include "gl_adapt.h"

/* GLES 3.0 legal texture targets (subset we pass through unchanged) */
static int is_valid_target(GLenum t) {
    switch (t) {
        case 0x0DE1: /* GL_TEXTURE_2D */
        case 0x8513: /* GL_TEXTURE_CUBE_MAP */
        case 0x806F: /* GL_TEXTURE_2D_ARRAY? (ES3) */
        case 0x8C2A: /* GL_TEXTURE_3D (ES3) */
            return 1;
        default:
            return 0;
    }
}

int adapt_texture_target(GLenum *target) {
    /* GL_TEXTURE_1D = 0x0DE0 ; GL_TEXTURE_RECTANGLE = 0x84F5 */
    if (*target == 0x0DE0 || *target == 0x84F5) {
        *target = 0x0DE1; /* GL_TEXTURE_2D */
        return 1;
    }
    /* leave valid targets untouched; anything else we cannot fix here */
    if (!is_valid_target(*target)) { *target = 0x0DE1; return 1; }
    return 0;
}

int adapt_wrap(GLenum *wrap) {
    /* GL_CLAMP = 0x2900 ; GL_CLAMP_TO_EDGE = 0x812F ;
       GL_CLAMP_TO_BORDER = 0x812D */
    if (*wrap == 0x2900 || *wrap == 0x812D) {
        *wrap = 0x812F; /* GL_CLAMP_TO_EDGE */
        return 1;
    }
    return 0;
}

int adapt_pixel_format(GLenum *internalformat, GLenum *format) {
    int changed = 0;
    /* GL_ALPHA=0x1906, GL_LUMINANCE=0x1909, GL_LUMINANCE_ALPHA=0x190A,
       GL_INTENSITY=0x8049 -> RGBA.  GL_R=0x1903/GL_RED ok in ES3;
       keep simple: collapse legacy single-channel to RGBA. */
    switch (*format) {
        case 0x1906: /* ALPHA */
        case 0x1909: /* LUMINANCE */
        case 0x190A: /* LUMINANCE_ALPHA */
        case 0x8049: /* INTENSITY */
            *format = 0x1908;        /* GL_RGBA */
            *internalformat = 0x1908;/* GL_RGBA */
            changed = 1;
            break;
        default:
            break;
    }
    /* GL_INTENSITY as internalformat alone */
    if (*internalformat == 0x8049) {
        *internalformat = 0x1908;
        changed = 1;
    }
    return changed;
}

int adapt_primitive(GLenum *mode) {
    /* GL_QUADS = 0x0007 ; GL_POLYGON = 0x0009 */
    if (*mode == 0x0007) { *mode = 0x0004; return 1; } /* TRIANGLES */
    if (*mode == 0x0009) { *mode = 0x000B; return 1; } /* TRIANGLE_FAN */
    return 0;
}

int expand_quads_to_triangles(int quad_count, const GLuint *q, GLuint *out) {
    if (quad_count <= 0 || !q || !out) return 0;
    for (int i = 0; i < quad_count; i++) {
        GLuint a = q[4 * i + 0];
        GLuint b = q[4 * i + 1];
        GLuint c = q[4 * i + 2];
        GLuint d = q[4 * i + 3];
        /* two triangles: a,b,c and a,c,d */
        out[6 * i + 0] = a;
        out[6 * i + 1] = b;
        out[6 * i + 2] = c;
        out[6 * i + 3] = a;
        out[6 * i + 4] = c;
        out[6 * i + 5] = d;
    }
    return 6 * quad_count;
}

/* ---- stencil FBO merge ---- */

/* enum constants (desktop/GLES share these numeric values) */
#define ATT_DEPTH          0x8D00  /* GL_DEPTH_ATTACHMENT */
#define ATT_STENCIL        0x8D01  /* GL_STENCIL_ATTACHMENT */
#define ATT_DEPTH_STENCIL 0x821A  /* GL_DEPTH_STENCIL_ATTACHMENT */
#define FMT_STENCIL8      0x8D48  /* GL_STENCIL_INDEX8 */
#define FMT_DEPTH24_STENCIL8 0x88F0 /* GL_DEPTH24_STENCIL8 */
#define FMT_DEPTH_COMPONENT  0x81A5
#define FMT_DEPTH_COMPONENT16 0x81A5
#define FMT_DEPTH_COMPONENT24 0x81A6
#define FMT_DEPTH_COMPONENT32 0x81A7
#define FMT_DEPTH_COMPONENT32F 0x8CAC

int adapt_renderbuffer_storage(GLenum *internalformat) {
    GLenum f = *internalformat;
    if (f == FMT_STENCIL8 ||
        f == 0x8D47 /* STENCIL_INDEX1 */ || f == 0x8D46 /* STENCIL_INDEX4 */) {
        *internalformat = FMT_DEPTH24_STENCIL8;
        return 1;
    }
    /* a depth buffer that will share the FBO with stencil -> make it combined */
    if (f == FMT_DEPTH_COMPONENT || f == FMT_DEPTH_COMPONENT16 ||
        f == FMT_DEPTH_COMPONENT24 || f == FMT_DEPTH_COMPONENT32 ||
        f == FMT_DEPTH_COMPONENT32F) {
        *internalformat = FMT_DEPTH24_STENCIL8;
        return 1;
    }
    return 0;
}

void fbo_builder_init(fbo_builder_t *b) { b->n = 0; }

void fbo_builder_add_rb(fbo_builder_t *b, GLenum attachment, GLenum internalformat, GLuint rb) {
    if (b->n < 16) {
        b->atts[b->n].attachment = attachment;
        b->atts[b->n].internalformat = internalformat;
        b->atts[b->n].obj = rb;
        b->atts[b->n].is_texture = 0;
        b->n++;
    }
}

void fbo_builder_add_tex(fbo_builder_t *b, GLenum attachment, GLuint tex) {
    if (b->n < 16) {
        b->atts[b->n].attachment = attachment;
        b->atts[b->n].internalformat = FMT_DEPTH24_STENCIL8; /* best-effort */
        b->atts[b->n].obj = tex;
        b->atts[b->n].is_texture = 1;
        b->n++;
    }
}

int fbo_builder_resolve(const fbo_builder_t *b, GLenum *out_att,
                        GLenum *out_int, GLuint *out_obj, int *out_tex) {
    int has_depth = 0, has_stencil = 0;
    for (int i = 0; i < b->n; i++) {
        if (b->atts[i].attachment == ATT_DEPTH)    has_depth = 1;
        if (b->atts[i].attachment == ATT_STENCIL)  has_stencil = 1;
    }
    int k = 0, merged = 0;
    for (int i = 0; i < b->n; i++) {
        GLenum a = b->atts[i].attachment;
        GLenum f = b->atts[i].internalformat;
        GLuint o = b->atts[i].obj;
        int tx = b->atts[i].is_texture;
        if (a == ATT_DEPTH) {
            if (has_stencil && !merged) { out_att[k]=ATT_DEPTH_STENCIL; out_int[k]=FMT_DEPTH24_STENCIL8; out_obj[k]=o; out_tex[k]=tx; k++; merged=1; }
            else if (!has_stencil)      { out_att[k]=a; out_int[k]=f; out_obj[k]=o; out_tex[k]=tx; k++; }
        } else if (a == ATT_STENCIL) {
            if (has_depth && !merged)   { out_att[k]=ATT_DEPTH_STENCIL; out_int[k]=FMT_DEPTH24_STENCIL8; out_obj[k]=o; out_tex[k]=tx; k++; merged=1; }
            else if (!has_depth)        { out_att[k]=ATT_DEPTH_STENCIL; out_int[k]=FMT_DEPTH24_STENCIL8; out_obj[k]=o; out_tex[k]=tx; k++; merged=1; }
        } else {
            out_att[k]=a; out_int[k]=f; out_obj[k]=o; out_tex[k]=tx; k++;
        }
    }
    return k;
}
