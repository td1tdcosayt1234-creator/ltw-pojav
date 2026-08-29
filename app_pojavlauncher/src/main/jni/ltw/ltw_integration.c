/**
 * ltw-lite integration: GL -> GLES3 primitive & texture target adaptation.
 * Copyright (c) 2025 ltw-lite contributors. Under LGPL-3.0 (matches LTW).
 */

#include "proc.h"
#include "gl_adapt.h"
#include <stdlib.h>
#include <string.h>

#ifndef GL_QUADS
#define GL_QUADS 0x0007
#endif
#ifndef GL_TRIANGLES
#define GL_TRIANGLES 0x0004
#endif
#ifndef GL_UNSIGNED_BYTE
#define GL_UNSIGNED_BYTE 0x1401
#endif
#ifndef GL_UNSIGNED_SHORT
#define GL_UNSIGNED_SHORT 0x1403
#endif
#ifndef GL_UNSIGNED_INT
#define GL_UNSIGNED_INT 0x1405
#endif

static GLuint read_index(GLenum type, const void *indices, size_t i) {
    const unsigned char *p = (const unsigned char *)indices;
    if (type == GL_UNSIGNED_BYTE) return (GLuint)p[i];
    if (type == GL_UNSIGNED_SHORT) return (GLuint)((const unsigned short *)indices)[i];
    return ((const unsigned int *)indices)[i];
}

/* GL_TEXTURE_1D / GL_TEXTURE_RECTANGLE -> GL_TEXTURE_2D */
void glBindTexture(GLenum target, GLuint texture) {
    adapt_texture_target(&target);
    es3_functions.glBindTexture(target, texture);
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    if (mode == GL_QUADS) {
        size_t n = (size_t)count;
        size_t quads = n / 4;
        GLuint *base = (GLuint *)malloc(quads * 4 * sizeof(GLuint));
        GLuint *exp = (GLuint *)malloc(quads * 6 * sizeof(GLuint));
        for (size_t i = 0; i < quads * 4; i++) base[i] = (GLuint)(first + (GLint)i);
        expand_quads_to_triangles((int)quads, base, exp);
        es3_functions.glDrawElements(GL_TRIANGLES, (GLsizei)(quads * 6), GL_UNSIGNED_INT, exp);
        free(exp);
        free(base);
        return;
    }
    adapt_primitive(&mode);
    es3_functions.glDrawArrays(mode, first, count);
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices) {
    if (mode == GL_QUADS) {
        size_t n = (size_t)count;
        size_t quads = n / 4;
        GLuint *exp = (GLuint *)malloc(quads * 6 * sizeof(GLuint));
        for (size_t i = 0; i < quads; i++) {
            GLuint a = read_index(type, indices, 4 * i + 0);
            GLuint b = read_index(type, indices, 4 * i + 1);
            GLuint c = read_index(type, indices, 4 * i + 2);
            GLuint d = read_index(type, indices, 4 * i + 3);
            exp[6 * i + 0] = a;
            exp[6 * i + 1] = b;
            exp[6 * i + 2] = c;
            exp[6 * i + 3] = a;
            exp[6 * i + 4] = c;
            exp[6 * i + 5] = d;
        }
        es3_functions.glDrawElements(GL_TRIANGLES, (GLsizei)(quads * 6), GL_UNSIGNED_INT, exp);
        free(exp);
        return;
    }
    adapt_primitive(&mode);
    es3_functions.glDrawElements(mode, count, type, indices);
}
