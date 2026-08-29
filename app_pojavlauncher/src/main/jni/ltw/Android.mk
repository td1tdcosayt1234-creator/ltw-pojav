LOCAL_PATH := $(call my-dir)


include $(CLEAR_VARS)
LOCAL_MODULE := ltw
LOCAL_SRC_FILES := \
    egl.c \
    proc.c \
    main.c \
    glformats.c \
    basevertex.c \
    shader_wrapper.c \
    string_utils.c \
    framebuffer.c \
    of_buffer_copier.c \
    stubs.c \
    multidraw.c \
    vertexattrib.c \
    swizzle.c \
    license_notice.c \
    ltw_integration.c \
    glsl_translate.c \
    gl_adapt.c \
    ltw_glue.c \
    vgpu_shaderconv/shaderconv.c \
    unordered_map/unordered_map.c \
    unordered_map/int_hash.c
LOCAL_LDLIBS := -llog -lEGL
include $(BUILD_SHARED_LIBRARY)