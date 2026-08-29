#ifndef GLSL_TRANSLATE_H
#define GLSL_TRANSLATE_H

/* Translation stages */
typedef enum { STAGE_VERTEX, STAGE_FRAGMENT } glsl_stage;

/*
 * Translate a desktop OpenGL GLSL shader (versions 110/120/150/330)
 * into GLSL ES 3.00 source suitable for an OpenGL ES 3.0 backend
 * (the kind LTW wraps). Returns a malloc'd string, or NULL on error.
 *
 * The goal is exactly the failure mode that crashes mods like Create
 * on the LTW renderer: built-in vertex attributes, `varying`,
 * `texture2D`, and `gl_FragColor`/`gl_FragData` are all invalid in
 * GLSL ES 3.00 and must be rewritten. Without this, vertex shaders
 * fail to compile -> "vertex error" -> the game crashes.
 */
char *glsl_translate(const char *src, glsl_stage stage);

#endif /* GLSL_TRANSLATE_H */
