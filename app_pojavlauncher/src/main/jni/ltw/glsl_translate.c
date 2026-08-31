#include "glsl_translate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    char *text;   /* original token text */
    int is_id;    /* 1 if identifier, else verbatim (punct/space/number) */
} Token;

static Token *tok_alloc(int *cap) {
    int c = 256;
    Token *t = malloc(sizeof(Token) * c);
    *cap = c;
    return t;
}

static void tok_push(Token **t, int *n, int *cap, const char *s, int len, int is_id) {
    if (*n >= *cap) {
        *cap *= 2;
        *t = realloc(*t, sizeof(Token) * (*cap));
    }
    Token *tk = &(*t)[*n];
    tk->text = malloc(len + 1);
    memcpy(tk->text, s, len);
    tk->text[len] = '\0';
    tk->is_id = is_id;
    (*n)++;
}

static int is_ident_char(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

static Token *tokenize(const char *src, int *ntok) {
    int n = 0, cap = 0;
    Token *t = tok_alloc(&cap);
    const char *p = src;
    while (*p) {
        if (is_ident_char(*p)) {
            const char *start = p;
            while (*p && is_ident_char(*p)) p++;
            tok_push(&t, &n, &cap, start, (int)(p - start), 1);
        } else {
            const char *start = p;
            while (*p && !is_ident_char(*p)) p++;
            tok_push(&t, &n, &cap, start, (int)(p - start), 0);
        }
    }
    *ntok = n;
    return t;
}

static int eq(const char *a, const char *s) { return strcmp(a, s) == 0; }

static int is_emulated_or_core_ext(const char *name) {
    static const char *drop[] = {
        "GL_EXT_clip_cull_distance",
        "GL_ARB_clip_distance",
        "GL_ARB_shader_texture_lod",
        "GL_EXT_shader_texture_lod",
        "GL_ARB_draw_buffers",
        "GL_EXT_draw_buffers",
        "GL_ARB_texture_rectangle",
        "GL_EXT_texture_rectangle",
        "GL_ARB_texture_non_power_of_two",
        "GL_OES_standard_derivatives",
        "GL_ARB_shading_language_100",
        "GL_EXT_gpu_shader4",
        "GL_EXT_texture_filter_anisotropic",
        "GL_ARB_fragment_program",
        "GL_ARB_vertex_program",
        "GL_ARB_shader_bit_encoding",
        "GL_ARB_shader_objects",
        NULL
    };
    for (int k = 0; drop[k]; k++)
        if (eq(name, drop[k])) return 1;
    return 0;
}

/* Drop #version and #extension lines whose feature is core/emulated, BEFORE
 * tokenizing. Keeping them would make the GLES driver reject the shader
 * ("Extension not supported" -- the fatal Create/Iris error, Pojav #4310). */
static char *preprocess(const char *src) {
    size_t len = strlen(src);
    char *out = malloc(len * 2 + 1);
    size_t o = 0;
    const char *p = src;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t llen = nl ? (size_t)(nl - p) : strlen(p);
        char *line = malloc(llen + 1);
        memcpy(line, p, llen); line[llen] = '\0';

        const char *ls = line;
        while (*ls == ' ' || *ls == '\t') ls++;
        int drop = 0;
        if (strncmp(ls, "#version", 8) == 0) {
            drop = 1;
        } else if (strncmp(ls, "#extension", 10) == 0) {
            const char *sp = ls + 10;
            while (*sp == ' ' || *sp == '\t') sp++;
            char ext[160]; int ei = 0;
            while (*sp && *sp != ' ' && *sp != '\t' && *sp != ':' && ei < 159)
                ext[ei++] = *sp++;
            ext[ei] = '\0';
            if (is_emulated_or_core_ext(ext)) drop = 1;
        }
        if (!drop) {
            memcpy(out + o, p, llen); o += llen;
            out[o++] = '\n';
        }
        free(line);
        if (nl) p = nl + 1; else break;
    }
    out[o] = '\0';
    return out;
}

static void emit_builtin_decls(const Token *t, int n, glsl_stage stage,
                               char *buf, size_t *pos, size_t size,
                               int *clip_n) {
    int need_vertex = 0, need_normal = 0, need_color = 0, need_sec = 0;
    int need_mtc[8] = {0};
    int need_texcoord = 0, need_fog = 0, need_clipvertex = 0;
    int uses_fragcolor = 0, uses_fragdata = 0, max_fd = 0;
    int uses_clipdist = 0, max_cd = 0;

    for (int i = 0; i < n; i++) {
        if (!t[i].is_id) continue;
        const char *s = t[i].text;
        if (eq(s, "gl_Vertex")) need_vertex = 1;
        else if (eq(s, "gl_Normal")) need_normal = 1;
        else if (eq(s, "gl_Color")) need_color = 1;
        else if (eq(s, "gl_SecondaryColor")) need_sec = 1;
        else if (eq(s, "gl_FogCoord")) need_fog = 1;
        else if (eq(s, "gl_TexCoord")) need_texcoord = 1;
        else if (eq(s, "gl_ClipVertex")) need_clipvertex = 1;
        else if (eq(s, "gl_FragColor")) uses_fragcolor = 1;
        else if (eq(s, "gl_FragData")) {
            uses_fragdata = 1;
            if (i + 2 < n && eq(t[i+1].text, "[")) {
                int v = atoi(t[i+2].text);
                if (v > max_fd) max_fd = v;
            }
        }
        else if (eq(s, "gl_ClipDistance")) {
            uses_clipdist = 1;
            if (i + 2 < n && eq(t[i+1].text, "[")) {
                int v = atoi(t[i+2].text);
                if (v > max_cd) max_cd = v;
            }
        }
        for (int k = 0; k < 8; k++) {
            char name[32];
            snprintf(name, sizeof(name), "gl_MultiTexCoord%d", k);
            if (eq(s, name)) need_mtc[k] = 1;
        }
    }

    if (clip_n) *clip_n = uses_clipdist ? (max_cd + 1) : 0;

    if (stage == STAGE_VERTEX) {
        if (need_vertex)   { snprintf(buf+*pos, size-*pos, "in vec4 gl_Vertex;\n"); *pos += strlen(buf+*pos); }
        if (need_normal)   { snprintf(buf+*pos, size-*pos, "in vec3 gl_Normal;\n"); *pos += strlen(buf+*pos); }
        if (need_color)    { snprintf(buf+*pos, size-*pos, "in vec4 gl_Color;\n"); *pos += strlen(buf+*pos); }
        if (need_sec)      { snprintf(buf+*pos, size-*pos, "in vec4 gl_SecondaryColor;\n"); *pos += strlen(buf+*pos); }
        if (need_fog)      { snprintf(buf+*pos, size-*pos, "in float gl_FogCoord;\n"); *pos += strlen(buf+*pos); }
        if (need_texcoord) { snprintf(buf+*pos, size-*pos, "out vec4 gl_TexCoord[8];\n"); *pos += strlen(buf+*pos); }
        if (need_clipvertex){ snprintf(buf+*pos, size-*pos, "out vec4 gl_ClipVertex;\n"); *pos += strlen(buf+*pos); }
        for (int k = 0; k < 8; k++) if (need_mtc[k]) {
            snprintf(buf+*pos, size-*pos, "in vec4 gl_MultiTexCoord%d;\n", k); *pos += strlen(buf+*pos);
        }
        if (uses_clipdist) {
            snprintf(buf+*pos, size-*pos, "out float _clipDist[%d];\n", max_cd + 1); *pos += strlen(buf+*pos);
        }
    } else {
        if (need_texcoord) { snprintf(buf+*pos, size-*pos, "in vec4 gl_TexCoord[8];\n"); *pos += strlen(buf+*pos); }
        if (uses_fragcolor){ snprintf(buf+*pos, size-*pos, "out vec4 _fragColor;\n"); *pos += strlen(buf+*pos); }
        if (uses_fragdata) { snprintf(buf+*pos, size-*pos, "out vec4 gl_FragData[%d];\n", max_fd + 1); *pos += strlen(buf+*pos); }
        if (uses_clipdist) {
            snprintf(buf+*pos, size-*pos, "in float _clipDist[%d];\n", max_cd + 1); *pos += strlen(buf+*pos);
        }
    }
}

static const char *tex_rewrite(const char *s) {
    if (eq(s, "texture2D") || eq(s, "texture3D") || eq(s, "textureCube")) return "texture";
    if (eq(s, "texture2DLod") || eq(s, "texture3DLod") || eq(s, "textureCubeLod")) return "textureLod";
    if (eq(s, "texture2DProj") || eq(s, "textureCubeProj")) return "textureProj";
    if (eq(s, "texture2DProjLod") || eq(s, "texture2DProjLodEXT")) return "textureProjLod";
    if (eq(s, "texture2DGrad") || eq(s, "texture2DGradEXT")) return "textureGrad";
    if (eq(s, "shadow2D")) return "texture";
    if (eq(s, "shadow2DProj")) return "textureProj";
    return NULL;
}

char *glsl_translate(const char *src, glsl_stage stage) {
    char *clean = preprocess(src);
    int n = 0;
    Token *t = tokenize(clean, &n);

    size_t outsize = strlen(src) * 3 + 4096;
    char *out = malloc(outsize);
    size_t pos = 0;

    snprintf(out + pos, outsize - pos, "#version 300 es\n");
    pos += strlen(out + pos);
    snprintf(out + pos, outsize - pos, "precision highp float;\n"); pos += strlen(out + pos);
    snprintf(out + pos, outsize - pos, "precision highp int;\n");   pos += strlen(out + pos);
    if (stage == STAGE_FRAGMENT) {
        snprintf(out + pos, outsize - pos, "precision highp sampler2D;\n"); pos += strlen(out + pos);
    }

    int clip_n = 0;
    emit_builtin_decls(t, n, stage, out, &pos, outsize, &clip_n);

    int saw_main = 0, saw_lparen = 0, saw_rparen = 0;

    for (int i = 0; i < n; i++) {
        if (!t[i].is_id) {
            /* inject clip-distance discard right after main()'s opening brace */
            if (stage == STAGE_FRAGMENT && clip_n > 0 &&
                saw_main && saw_lparen && saw_rparen && eq(t[i].text, "{")) {
                snprintf(out + pos, outsize - pos, "{\n"); pos += strlen(out + pos);
                for (int c = 0; c < clip_n; c++)
                    snprintf(out + pos, outsize - pos,
                             "  if (_clipDist[%d] < 0.0) discard;\n", c);
                pos += strlen(out + pos);
                saw_main = saw_lparen = saw_rparen = 0;
                continue;
            }
            snprintf(out + pos, outsize - pos, "%s", t[i].text);
            pos += strlen(out + pos);
            continue;
        }

        const char *s = t[i].text;
        const char *repl = NULL;

        if (eq(s, "attribute")) repl = "in";
        else if (eq(s, "varying")) repl = (stage == STAGE_VERTEX) ? "out" : "in";
        else if (eq(s, "gl_FragColor")) repl = "_fragColor";
        else if (eq(s, "gl_ClipDistance")) repl = "_clipDist";
        else repl = tex_rewrite(s);

        if (repl) {
            snprintf(out + pos, outsize - pos, "%s", repl);
            pos += strlen(out + pos);
        } else {
            snprintf(out + pos, outsize - pos, "%s", s);
            pos += strlen(out + pos);
        }

        if (eq(s, "main")) saw_main = 1;
        else if (saw_main && eq(s, "(")) saw_lparen = 1;
        else if (saw_main && saw_lparen && eq(s, ")")) saw_rparen = 1;
    }

    for (int k = 0; k < n; k++) free(t[k].text);
    free(t);
    free(clean);
    out[pos] = '\0';
    return out;
}
