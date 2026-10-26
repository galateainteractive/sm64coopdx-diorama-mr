#include <android/log.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "quest_rom.h"
#include "quest_game_runtime.h"
#include "quest_openxr_input.h"
/* Keep this bootstrap translation unit independent of the game's ultra64
 * include order. The display function's u32 parameter is unsigned int on
 * every supported target. */
void djui_fps_display_update(unsigned int fps);

extern void quest_vr_bridge_update_views(
    const float positions[2][3], const float rotations[2][4],
    const float fovs[2][4], const uint32_t widths[2],
    const uint32_t heights[2]);
extern bool quest_vr_bridge_recentered_to_local(
    const float recentered[3], float local[3]);

#define LOG_TAG "SM64CoopDXVR"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define QUEST_VIEW_COUNT 2U
#define QUEST_DEFAULT_RENDER_SCALE_PERCENT 80U
#define QUEST_PASSTHROUGH_LUT_RESOLUTION 32U
#define QUEST_DAMAGE_PULSE_DURATION_NS 700000000LL
#define QUEST_HEAL_PULSE_DURATION_NS 1100000000LL
#define QUEST_STAR_PULSE_DURATION_NS 2200000000LL
#define QUEST_CAP_STYLE_WEIGHT 0.78f
#define QUEST_ROOM_PULSE_DURATION_NS 5000000000LL
#define QUEST_ROOM_MESH_MAX 4U
#define QUEST_ROOM_PULSE_ENABLED 0
#define QUEST_ROOM_QUERY_MAX_RESULTS 16U

typedef struct QuestRoomMesh {
    XrSpace space;
    GLuint vertex_buffer;
    GLuint index_buffer;
    uint32_t index_count;
} QuestRoomMesh;

typedef struct QuestPassthroughThemeStyle {
    float tint[3];
    float strength;
    float brightness;
    float contrast;
    float saturation;
    XrColor4f edge_color;
} QuestPassthroughThemeStyle;

static const QuestPassthroughThemeStyle sPassthroughThemeStyles[] = {
    [QUEST_PASSTHROUGH_THEME_NEUTRAL] = {
        {1.00f, 1.00f, 1.00f}, 0.00f, 0.00f, 0.02f, 1.00f,
        {0.80f, 0.85f, 1.00f, 0.05f},
    },
    [QUEST_PASSTHROUGH_THEME_GRASS] = {
        {0.48f, 1.48f, 0.42f}, 0.66f, 0.01f, 0.08f, 1.24f,
        {0.18f, 1.00f, 0.22f, 0.32f},
    },
    [QUEST_PASSTHROUGH_THEME_WATER] = {
        {0.34f, 0.82f, 1.62f}, 0.74f, -0.02f, 0.11f, 1.20f,
        {0.08f, 0.58f, 1.00f, 0.40f},
    },
    [QUEST_PASSTHROUGH_THEME_SNOW] = {
        {0.64f, 1.02f, 1.48f}, 0.56f, 0.08f, 0.06f, 0.92f,
        {0.58f, 0.86f, 1.00f, 0.30f},
    },
    [QUEST_PASSTHROUGH_THEME_SKY] = {
        {0.38f, 0.76f, 1.62f}, 0.70f, 0.02f, 0.09f, 1.18f,
        {0.16f, 0.54f, 1.00f, 0.38f},
    },
    [QUEST_PASSTHROUGH_THEME_FIRE] = {
        {1.78f, 0.30f, 0.16f}, 0.84f, -0.02f, 0.17f, 1.32f,
        {1.00f, 0.08f, 0.01f, 0.52f},
    },
    [QUEST_PASSTHROUGH_THEME_CAVE] = {
        {0.54f, 0.36f, 1.30f}, 0.66f, -0.10f, 0.18f, 0.88f,
        {0.44f, 0.18f, 1.00f, 0.38f},
    },
    [QUEST_PASSTHROUGH_THEME_DESERT] = {
        {1.58f, 0.92f, 0.28f}, 0.72f, 0.04f, 0.12f, 1.20f,
        {1.00f, 0.60f, 0.06f, 0.38f},
    },
    [QUEST_PASSTHROUGH_THEME_SPOOKY] = {
        {1.18f, 0.24f, 1.58f}, 0.78f, -0.07f, 0.20f, 0.92f,
        {0.78f, 0.06f, 1.00f, 0.48f},
    },
};

static const QuestPassthroughThemeStyle sPassthroughEffectStyles[] = {
    [QUEST_PASSTHROUGH_EFFECT_NONE] = {
        {1.00f, 1.00f, 1.00f}, 0.00f, 0.00f, 0.02f, 1.00f,
        {0.80f, 0.85f, 1.00f, 0.05f},
    },
    [QUEST_PASSTHROUGH_EFFECT_DAMAGE] = {
        {1.95f, 0.10f, 0.06f}, 0.98f, 0.08f, 0.28f, 1.44f,
        {1.00f, 0.02f, 0.01f, 0.96f},
    },
    [QUEST_PASSTHROUGH_EFFECT_HEAL] = {
        {0.16f, 1.86f, 0.28f}, 0.92f, 0.14f, 0.18f, 1.34f,
        {0.06f, 1.00f, 0.18f, 0.92f},
    },
    [QUEST_PASSTHROUGH_EFFECT_STAR] = {
        {1.82f, 1.28f, 0.04f}, 0.96f, 0.18f, 0.24f, 1.38f,
        {1.00f, 0.78f, 0.08f, 0.95f},
    },
    [QUEST_PASSTHROUGH_EFFECT_WING_CAP] = {
        {1.62f, 0.46f, 0.18f}, 0.76f, 0.06f, 0.14f, 1.30f,
        {1.00f, 0.24f, 0.06f, 0.58f},
    },
    [QUEST_PASSTHROUGH_EFFECT_METAL_CAP] = {
        {0.82f, 1.02f, 1.20f}, 0.46f, 0.04f, 0.24f, 0.22f,
        {0.72f, 0.90f, 1.00f, 0.54f},
    },
    [QUEST_PASSTHROUGH_EFFECT_VANISH_CAP] = {
        {0.64f, 0.24f, 1.74f}, 0.82f, -0.05f, 0.20f, 1.02f,
        {0.62f, 0.08f, 1.00f, 0.62f},
    },
};

typedef struct QuestSwapchain {
    XrSwapchain handle;
    int32_t width;
    int32_t height;
    uint32_t image_count;
    XrSwapchainImageOpenGLESKHR *images;
    GLuint *framebuffers;
} QuestSwapchain;

typedef struct QuestApp {
    struct android_app *android_app;

    EGLDisplay egl_display;
    EGLConfig egl_config;
    EGLContext egl_context;
    EGLSurface egl_surface;

    XrInstance instance;
    XrSystemId system_id;
    XrSession session;
    XrSpace local_space;
    XrSessionState session_state;
    XrEnvironmentBlendMode blend_mode;
    bool session_running;
    bool exit_requested;
    bool activity_resumed;
    bool window_ready;
    QuestRomResult rom;
    GLuint mario_program;
    GLuint mario_texture;
    GLint mario_eye_offset;

    XrViewConfigurationView view_configs[QUEST_VIEW_COUNT];
    XrView views[QUEST_VIEW_COUNT];
    QuestSwapchain swapchains[QUEST_VIEW_COUNT];
    unsigned int active_render_scale;
    unsigned int pending_render_scale;
    unsigned int render_scale_stable_frames;
    bool foveation_supported;
    bool performance_settings_supported;
    bool display_refresh_rate_supported;
    bool passthrough_supported;
    bool passthrough_active;
    bool passthrough_style_supported;
    bool passthrough_color_lut_supported;
    bool render_model_supported;
    bool room_mesh_supported;
    bool triangle_mesh_supported;
    bool room_query_uses_triangle_mesh;
    char system_name[XR_MAX_SYSTEM_NAME_SIZE];
    XrFoveationProfileFB foveation_profiles[3];
    unsigned int active_foveation_profile;
    unsigned int active_refresh_rate_index;
    PFN_xrCreateFoveationProfileFB create_foveation_profile;
    PFN_xrDestroyFoveationProfileFB destroy_foveation_profile;
    PFN_xrUpdateSwapchainFB update_swapchain;
    PFN_xrPerfSettingsSetPerformanceLevelEXT set_performance_level;
    PFN_xrEnumerateDisplayRefreshRatesFB enumerate_refresh_rates;
    PFN_xrGetDisplayRefreshRateFB get_refresh_rate;
    PFN_xrRequestDisplayRefreshRateFB request_refresh_rate;
    XrPassthroughFB passthrough;
    XrPassthroughLayerFB passthrough_layer;
    PFN_xrCreatePassthroughFB create_passthrough;
    PFN_xrDestroyPassthroughFB destroy_passthrough;
    PFN_xrPassthroughStartFB start_passthrough;
    PFN_xrPassthroughPauseFB pause_passthrough;
    PFN_xrCreatePassthroughLayerFB create_passthrough_layer;
    PFN_xrDestroyPassthroughLayerFB destroy_passthrough_layer;
    PFN_xrPassthroughLayerPauseFB pause_passthrough_layer;
    PFN_xrPassthroughLayerResumeFB resume_passthrough_layer;
    PFN_xrPassthroughLayerSetStyleFB set_passthrough_style;
    PFN_xrCreatePassthroughColorLutMETA create_passthrough_color_lut;
    PFN_xrDestroyPassthroughColorLutMETA destroy_passthrough_color_lut;
    PFN_xrUpdatePassthroughColorLutMETA update_passthrough_color_lut;
    XrPassthroughColorLutMETA theme_color_lut;
    XrPassthroughColorLutMETA effect_color_lut;
    enum QuestPassthroughTheme active_passthrough_theme;
    enum QuestPassthroughEffect active_passthrough_effect;
    enum QuestPassthroughEffect active_cap_effect;
    uint32_t observed_passthrough_effect_serial;
    XrTime passthrough_effect_start_time;
    float applied_passthrough_effect_weight;
    PFN_xrEnumerateRenderModelPathsFB enumerate_render_model_paths;
    PFN_xrGetRenderModelPropertiesFB get_render_model_properties;
    PFN_xrLoadRenderModelFB load_render_model;
    PFN_xrQuerySpacesFB query_spaces;
    PFN_xrRetrieveSpaceQueryResultsFB retrieve_space_query_results;
    PFN_xrEnumerateSpaceSupportedComponentsFB enumerate_space_components;
    PFN_xrGetSpaceComponentStatusFB get_space_component_status;
    PFN_xrSetSpaceComponentStatusFB set_space_component_status;
    PFN_xrGetSpaceRoomMeshMETA get_space_room_mesh;
    PFN_xrGetSpaceRoomMeshFaceIndicesMETA get_room_mesh_face_indices;
    PFN_xrGetSpaceTriangleMeshMETA get_space_triangle_mesh;
    QuestRoomMesh room_meshes[QUEST_ROOM_MESH_MAX];
    uint32_t room_mesh_count;
    bool room_query_pending;
    bool room_query_complete;
    XrTime room_query_retry_time;
    GLuint room_pulse_program;
    GLint room_pulse_projection;
    GLint room_pulse_eye_position;
    GLint room_pulse_eye_rotation;
    GLint room_pulse_anchor_position;
    GLint room_pulse_anchor_rotation;
    GLint room_pulse_center;
    GLint room_pulse_radius;
    GLint room_pulse_opacity;
    uint32_t room_observed_star_serial;
    XrTime room_pulse_start_time;
    float room_pulse_center_position[3];
    GLuint depth_buffer;
    int32_t depth_width;
    int32_t depth_height;
} QuestApp;

static bool openxr_extension_available(const char *name) {
    uint32_t count = 0;
    if (XR_FAILED(xrEnumerateInstanceExtensionProperties(
            NULL, 0, &count, NULL)) || count == 0) return false;
    XrExtensionProperties *properties = calloc(count, sizeof(*properties));
    if (properties == NULL) return false;
    for (uint32_t i = 0; i < count; ++i) {
        properties[i].type = XR_TYPE_EXTENSION_PROPERTIES;
    }
    bool found = false;
    if (XR_SUCCEEDED(xrEnumerateInstanceExtensionProperties(
            NULL, count, &count, properties))) {
        for (uint32_t i = 0; i < count; ++i) {
            if (strcmp(properties[i].extensionName, name) == 0) {
                found = true;
                break;
            }
        }
    }
    free(properties);
    return found;
}

static QuestApp *sActiveQuestApp;
static bool poll_openxr_events(QuestApp *app);
static bool initialize_room_mesh(QuestApp *app);

/* Long first-launch mod and DynOS scans run on android_main because they need
 * the game renderer's thread.  Android still requires this same thread to
 * acknowledge NativeActivity focus/input events within five seconds.  Let
 * those scanners service the native queue without advancing a game frame. */
void quest_android_pump_startup_events(void) {
    QuestApp *app = sActiveQuestApp;
    if (app == NULL || app->android_app == NULL) return;

    int events = 0;
    struct android_poll_source *source = NULL;
    while (ALooper_pollOnce(0, NULL, &events, (void **)&source) >= 0) {
        if (source != NULL) source->process(app->android_app, source);
        if (app->android_app->destroyRequested) break;
    }
    if (app->instance != XR_NULL_HANDLE) {
        (void)poll_openxr_events(app);
    }
}

ANativeActivity *quest_android_get_activity(void) {
    if (sActiveQuestApp == NULL || sActiveQuestApp->android_app == NULL) return NULL;
    return sActiveQuestApp->android_app->activity;
}

void quest_android_request_exit(void) {
    if (sActiveQuestApp == NULL) return;
    sActiveQuestApp->exit_requested = true;
    ANativeActivity_finish(sActiveQuestApp->android_app->activity);
}

static GLuint compile_shader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
        char message[512] = {0};
        glGetShaderInfoLog(shader, sizeof(message), NULL, message);
        LOGE("Mario preview shader failed: %s", message);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool create_mario_preview(QuestApp *app) {
    if (app->rom.status != QUEST_ROM_VALID) return true;
    unsigned char pixels[64U * 32U * 4U];
    if (!quest_rom_load_rgba16_texture(&app->rom,
            0x002a65b0U, 22255U, 0x00001018U, 64U, 32U, pixels)) {
        LOGE("Could not extract the Mario portrait from the ROM.");
        return false;
    }
    const char *vertex_source =
        "#version 300 es\n"
        "uniform float eyeOffset; out vec2 uv;\n"
        "void main(){\n"
        "const vec2 p[6]=vec2[6](vec2(-.56,-.28),vec2(.56,-.28),"
        "vec2(-.56,.28),vec2(-.56,.28),vec2(.56,-.28),vec2(.56,.28));\n"
        "const vec2 t[6]=vec2[6](vec2(0.,1.),vec2(1.,1.),vec2(0.,0.),"
        "vec2(0.,0.),vec2(1.,1.),vec2(1.,0.));\n"
        "uv=t[gl_VertexID]; gl_Position=vec4(p[gl_VertexID]+vec2(eyeOffset,0.),0.,1.);}\n";
    const char *fragment_source =
        "#version 300 es\nprecision mediump float; in vec2 uv;"
        "uniform sampler2D portrait; out vec4 color;"
        "void main(){color=texture(portrait,uv);}\n";
    GLuint vertex = compile_shader(GL_VERTEX_SHADER, vertex_source);
    GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    if (vertex == 0 || fragment == 0) {
        if (vertex != 0) glDeleteShader(vertex);
        if (fragment != 0) glDeleteShader(fragment);
        return false;
    }
    app->mario_program = glCreateProgram();
    glAttachShader(app->mario_program, vertex);
    glAttachShader(app->mario_program, fragment);
    glLinkProgram(app->mario_program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    glGetProgramiv(app->mario_program, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
        LOGE("Could not link the Mario preview shader.");
        return false;
    }
    app->mario_eye_offset = glGetUniformLocation(app->mario_program, "eyeOffset");
    glUseProgram(app->mario_program);
    glUniform1i(glGetUniformLocation(app->mario_program, "portrait"), 0);
    glGenTextures(1, &app->mario_texture);
    glBindTexture(GL_TEXTURE_2D, app->mario_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 32, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    LOGI("Mario portrait extracted from the ROM and uploaded to OpenGL ES.");
    return true;
}

static void handle_app_command(struct android_app *android_app, int32_t command) {
    QuestApp *app = (QuestApp *)android_app->userData;
    if (app == NULL) {
        return;
    }

    switch (command) {
        case APP_CMD_RESUME:
            app->activity_resumed = true;
            LOGI("Android activity resumed.");
            break;
        case APP_CMD_PAUSE:
            quest_game_flush_persistent_state();
            app->activity_resumed = false;
            quest_input_suspend();
            LOGI("Android activity paused.");
            break;
        case APP_CMD_STOP:
            quest_game_flush_persistent_state();
            app->activity_resumed = false;
            quest_input_suspend();
            // Quest can briefly stop the NativeActivity while handing focus
            // between the system shell, the ROM picker, and the immersive
            // OpenXR session.  Treating every STOP as a permanent exit kills
            // the native host before that handoff can complete.  Keep the
            // process alive here; android_app->destroyRequested remains the
            // authoritative signal for final teardown.
            LOGI("Android activity stopped; persistent state flushed.");
            break;
        case APP_CMD_INIT_WINDOW:
            app->window_ready = android_app->window != NULL;
            LOGI("Android native window is ready.");
            break;
        case APP_CMD_TERM_WINDOW:
            app->window_ready = false;
            quest_input_suspend();
            LOGI("Android native window was released.");
            break;
        default:
            break;
    }
}

static bool wait_for_android_ready(QuestApp *app) {
    LOGI("Waiting for the Android activity and native window.");
    while (!app->android_app->destroyRequested && !app->exit_requested
           && !(app->activity_resumed && app->window_ready)) {
        int events = 0;
        struct android_poll_source *source = NULL;
        const int result =
            ALooper_pollOnce(-1, NULL, &events, (void **)&source);
        if (result >= 0 && source != NULL) {
            source->process(app->android_app, source);
        }
    }
    return !app->android_app->destroyRequested && !app->exit_requested;
}

static bool xr_check(const char *operation, XrResult result) {
    if (XR_SUCCEEDED(result)) {
        return true;
    }

    LOGE("%s failed with OpenXR result %d", operation, (int)result);
    return false;
}

static bool egl_check(const char *operation, EGLBoolean result) {
    if (result == EGL_TRUE) {
        return true;
    }

    LOGE("%s failed with EGL error 0x%04x", operation, eglGetError());
    return false;
}

static bool create_egl_context(QuestApp *app) {
    app->egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (app->egl_display == EGL_NO_DISPLAY) {
        LOGE("eglGetDisplay failed with EGL error 0x%04x", eglGetError());
        return false;
    }

    EGLint major = 0;
    EGLint minor = 0;
    if (!egl_check("eglInitialize", eglInitialize(app->egl_display, &major, &minor))) {
        return false;
    }

    const EGLint config_attributes[] = {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 0,
        EGL_SAMPLES, 0,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_NONE,
    };

    EGLint config_count = 0;
    if (!egl_check(
            "eglChooseConfig",
            eglChooseConfig(
                app->egl_display, config_attributes, &app->egl_config, 1, &config_count))
        || config_count < 1) {
        LOGE("No compatible OpenGL ES 3 EGL configuration was found.");
        return false;
    }

    if (!egl_check("eglBindAPI", eglBindAPI(EGL_OPENGL_ES_API))) {
        return false;
    }

    const EGLint context_attributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE,
    };
    app->egl_context = eglCreateContext(
        app->egl_display, app->egl_config, EGL_NO_CONTEXT, context_attributes);
    if (app->egl_context == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext failed with EGL error 0x%04x", eglGetError());
        return false;
    }

    const EGLint surface_attributes[] = {
        EGL_WIDTH, 16,
        EGL_HEIGHT, 16,
        EGL_NONE,
    };
    app->egl_surface =
        eglCreatePbufferSurface(app->egl_display, app->egl_config, surface_attributes);
    if (app->egl_surface == EGL_NO_SURFACE) {
        LOGE("eglCreatePbufferSurface failed with EGL error 0x%04x", eglGetError());
        return false;
    }

    if (!egl_check(
            "eglMakeCurrent",
            eglMakeCurrent(
                app->egl_display,
                app->egl_surface,
                app->egl_surface,
                app->egl_context))) {
        return false;
    }

    LOGI("EGL %d.%d OpenGL ES context created.", major, minor);
    return true;
}

static bool create_openxr_instance(QuestApp *app) {
    PFN_xrInitializeLoaderKHR initialize_loader = NULL;
    XrResult result = xrGetInstanceProcAddr(
        XR_NULL_HANDLE,
        "xrInitializeLoaderKHR",
        (PFN_xrVoidFunction *)&initialize_loader);
    if (!xr_check("xrGetInstanceProcAddr(xrInitializeLoaderKHR)", result)
        || initialize_loader == NULL) {
        return false;
    }

    XrLoaderInitInfoAndroidKHR loader_info = {
        .type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR,
        .next = NULL,
        .applicationVM = app->android_app->activity->vm,
        .applicationContext = app->android_app->activity->clazz,
    };
    if (!xr_check(
            "xrInitializeLoaderKHR",
            initialize_loader((const XrLoaderInitInfoBaseHeaderKHR *)&loader_info))) {
        return false;
    }

    const char *extensions[20] = {
        XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
        XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
    };
    uint32_t extension_count = 2;
    app->performance_settings_supported =
        openxr_extension_available(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME);
    if (app->performance_settings_supported) {
        extensions[extension_count++] =
            XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME;
    }
    app->foveation_supported =
        openxr_extension_available(XR_FB_FOVEATION_EXTENSION_NAME)
        && openxr_extension_available(
            XR_FB_FOVEATION_CONFIGURATION_EXTENSION_NAME)
        && openxr_extension_available(
            XR_FB_SWAPCHAIN_UPDATE_STATE_EXTENSION_NAME);
    if (app->foveation_supported) {
        extensions[extension_count++] = XR_FB_FOVEATION_EXTENSION_NAME;
        extensions[extension_count++] =
            XR_FB_FOVEATION_CONFIGURATION_EXTENSION_NAME;
        extensions[extension_count++] =
            XR_FB_SWAPCHAIN_UPDATE_STATE_EXTENSION_NAME;
    }
    app->display_refresh_rate_supported =
        openxr_extension_available(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
    if (app->display_refresh_rate_supported) {
        extensions[extension_count++] =
            XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME;
    }
    app->passthrough_supported =
        openxr_extension_available(XR_FB_PASSTHROUGH_EXTENSION_NAME);
    if (app->passthrough_supported) {
        extensions[extension_count++] = XR_FB_PASSTHROUGH_EXTENSION_NAME;
    }
    app->passthrough_color_lut_supported =
        app->passthrough_supported &&
        openxr_extension_available(
            XR_META_PASSTHROUGH_COLOR_LUT_EXTENSION_NAME);
    if (app->passthrough_color_lut_supported) {
        extensions[extension_count++] =
            XR_META_PASSTHROUGH_COLOR_LUT_EXTENSION_NAME;
    }
    app->render_model_supported =
        openxr_extension_available(XR_FB_RENDER_MODEL_EXTENSION_NAME);
    if (app->render_model_supported) {
        extensions[extension_count++] = XR_FB_RENDER_MODEL_EXTENSION_NAME;
    }
    const bool scene_query_supported =
        openxr_extension_available(XR_FB_SPATIAL_ENTITY_EXTENSION_NAME) &&
        openxr_extension_available(XR_FB_SPATIAL_ENTITY_QUERY_EXTENSION_NAME) &&
        openxr_extension_available(XR_FB_SPATIAL_ENTITY_STORAGE_EXTENSION_NAME) &&
        openxr_extension_available(XR_FB_SCENE_EXTENSION_NAME);
    app->room_mesh_supported = scene_query_supported &&
        openxr_extension_available(XR_META_SPATIAL_ENTITY_ROOM_MESH_EXTENSION_NAME);
    app->triangle_mesh_supported = scene_query_supported &&
        openxr_extension_available(XR_META_SPATIAL_ENTITY_MESH_EXTENSION_NAME);
    if (app->room_mesh_supported || app->triangle_mesh_supported) {
        extensions[extension_count++] = XR_FB_SPATIAL_ENTITY_EXTENSION_NAME;
        extensions[extension_count++] = XR_FB_SPATIAL_ENTITY_QUERY_EXTENSION_NAME;
        extensions[extension_count++] = XR_FB_SPATIAL_ENTITY_STORAGE_EXTENSION_NAME;
        extensions[extension_count++] = XR_FB_SCENE_EXTENSION_NAME;
    }
    if (app->room_mesh_supported) {
        extensions[extension_count++] =
            XR_META_SPATIAL_ENTITY_ROOM_MESH_EXTENSION_NAME;
    }
    if (app->triangle_mesh_supported) {
        extensions[extension_count++] =
            XR_META_SPATIAL_ENTITY_MESH_EXTENSION_NAME;
    }

    XrInstanceCreateInfoAndroidKHR android_info = {
        .type = XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR,
        .next = NULL,
        .applicationVM = app->android_app->activity->vm,
        .applicationActivity = app->android_app->activity->clazz,
    };
    XrInstanceCreateInfo create_info = {
        .type = XR_TYPE_INSTANCE_CREATE_INFO,
        .next = &android_info,
        .applicationInfo = {
            .applicationName = "SM64 Diorama MR",
            .applicationVersion = 1,
            .engineName = "SM64 Co-Op DX",
            .engineVersion = 1,
            .apiVersion = XR_CURRENT_API_VERSION,
        },
        .enabledExtensionCount = extension_count,
        .enabledExtensionNames = extensions,
    };

    if (!xr_check("xrCreateInstance", xrCreateInstance(&create_info, &app->instance))) {
        return false;
    }

    XrSystemGetInfo system_info = {
        .type = XR_TYPE_SYSTEM_GET_INFO,
        .formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY,
    };
    if (!xr_check(
            "xrGetSystem", xrGetSystem(app->instance, &system_info, &app->system_id))) {
        return false;
    }

    XrSystemPassthroughProperties2FB passthrough_properties = {
        .type = XR_TYPE_SYSTEM_PASSTHROUGH_PROPERTIES2_FB,
    };
    XrSystemProperties system_properties = {
        .type = XR_TYPE_SYSTEM_PROPERTIES,
        .next = app->passthrough_supported ? &passthrough_properties : NULL,
    };
    const bool got_system_properties = xr_check(
            "xrGetSystemProperties",
            xrGetSystemProperties(
                app->instance, app->system_id, &system_properties));
    if (got_system_properties) {
        snprintf(app->system_name, sizeof(app->system_name), "%s",
                 system_properties.systemName);
    }
    app->passthrough_supported = got_system_properties
        && app->passthrough_supported
        && (passthrough_properties.capabilities
            & XR_PASSTHROUGH_CAPABILITY_BIT_FB) != 0;

    LOGI("Quest OpenXR runtime detected: %s; system id=%llu.",
         app->system_name[0] != '\0' ? app->system_name : "unknown headset",
         (unsigned long long)app->system_id);
    return true;
}

static bool select_blend_mode(QuestApp *app) {
    uint32_t count = 0;
    if (!xr_check(
            "xrEnumerateEnvironmentBlendModes(count)",
            xrEnumerateEnvironmentBlendModes(
                app->instance,
                app->system_id,
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                0,
                &count,
                NULL))
        || count == 0) {
        return false;
    }

    XrEnvironmentBlendMode *modes =
        (XrEnvironmentBlendMode *)calloc(count, sizeof(*modes));
    if (modes == NULL) {
        LOGE("Could not allocate environment blend mode list.");
        return false;
    }

    const bool enumerated = xr_check(
        "xrEnumerateEnvironmentBlendModes",
        xrEnumerateEnvironmentBlendModes(
            app->instance,
            app->system_id,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
            count,
            &count,
            modes));
    if (!enumerated) {
        free(modes);
        return false;
    }

    app->blend_mode = modes[0];
    for (uint32_t index = 0; index < count; ++index) {
        if (modes[index] == XR_ENVIRONMENT_BLEND_MODE_OPAQUE) {
            app->blend_mode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
            break;
        }
    }
    free(modes);
    return true;
}

static void configure_display_refresh_rate(QuestApp *app) {
    if (!app->display_refresh_rate_supported) {
        LOGI("OpenXR display refresh-rate control is unavailable.");
        return;
    }

    xrGetInstanceProcAddr(
        app->instance,
        "xrEnumerateDisplayRefreshRatesFB",
        (PFN_xrVoidFunction *)&app->enumerate_refresh_rates
    );
    xrGetInstanceProcAddr(
        app->instance,
        "xrGetDisplayRefreshRateFB",
        (PFN_xrVoidFunction *)&app->get_refresh_rate
    );
    xrGetInstanceProcAddr(
        app->instance,
        "xrRequestDisplayRefreshRateFB",
        (PFN_xrVoidFunction *)&app->request_refresh_rate
    );
    if (app->enumerate_refresh_rates == NULL ||
        app->get_refresh_rate == NULL ||
        app->request_refresh_rate == NULL) {
        app->display_refresh_rate_supported = false;
        LOGI("OpenXR display refresh-rate entry points are unavailable.");
        return;
    }

    uint32_t rate_count = 0;
    if (XR_FAILED(app->enumerate_refresh_rates(
            app->session, 0, &rate_count, NULL)) || rate_count == 0) {
        LOGI("Quest runtime did not enumerate display refresh rates.");
        return;
    }
    float *rates = calloc(rate_count, sizeof(*rates));
    if (rates == NULL) return;
    if (XR_FAILED(app->enumerate_refresh_rates(
            app->session, rate_count, &rate_count, rates))) {
        free(rates);
        return;
    }

    static const float refresh_targets[] = { 72.0f, 90.0f, 120.0f };
    const unsigned int requested_index =
        quest_game_refresh_rate_index() <= 2
            ? quest_game_refresh_rate_index()
            : 2;
    const float target = refresh_targets[requested_index];
    float selected = 0.0f;
    for (uint32_t index = 0; index < rate_count; ++index) {
        if (rates[index] <= target + 0.5f && rates[index] > selected) {
            selected = rates[index];
        }
    }
    if (selected <= 0.0f) {
        selected = rates[0];
        for (uint32_t index = 1; index < rate_count; ++index) {
            if (rates[index] < selected) selected = rates[index];
        }
    }
    free(rates);

    const XrResult request_result =
        app->request_refresh_rate(app->session, selected);
    float current = 0.0f;
    const XrResult get_result =
        app->get_refresh_rate(app->session, &current);
    if (XR_SUCCEEDED(request_result)) {
        LOGI("Quest display refresh requested: %.1f Hz.", selected);
    } else {
        LOGI("Quest display refresh request %.1f Hz failed (%d).",
             selected, (int)request_result);
    }
    if (XR_SUCCEEDED(get_result)) {
        LOGI("Quest display refresh currently %.1f Hz.", current);
    }
    app->active_refresh_rate_index = requested_index;
}

static XrFoveationProfileFB create_foveation_level_profile(
    QuestApp *app,
    XrFoveationLevelFB foveation_level
) {
    XrFoveationLevelProfileCreateInfoFB level = {
        .type = XR_TYPE_FOVEATION_LEVEL_PROFILE_CREATE_INFO_FB,
        .level = foveation_level,
        .verticalOffset = 0.0f,
        .dynamic = XR_FOVEATION_DYNAMIC_LEVEL_ENABLED_FB,
    };
    XrFoveationProfileCreateInfoFB profile_info = {
        .type = XR_TYPE_FOVEATION_PROFILE_CREATE_INFO_FB,
        .next = &level,
    };
    XrFoveationProfileFB profile = XR_NULL_HANDLE;
    if (XR_FAILED(app->create_foveation_profile(
            app->session, &profile_info, &profile))) {
        return XR_NULL_HANDLE;
    }
    return profile;
}

static float passthrough_clamp01(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static uint8_t passthrough_color_byte(float value) {
    return (uint8_t)(passthrough_clamp01(value) * 255.0f + 0.5f);
}

static const QuestPassthroughThemeStyle *passthrough_theme_style(
    enum QuestPassthroughTheme theme
) {
    if (theme < QUEST_PASSTHROUGH_THEME_NEUTRAL ||
        theme > QUEST_PASSTHROUGH_THEME_SPOOKY) {
        theme = QUEST_PASSTHROUGH_THEME_NEUTRAL;
    }
    return &sPassthroughThemeStyles[theme];
}

static const char *passthrough_theme_name(
    enum QuestPassthroughTheme theme
) {
    static const char *names[] = {
        "neutral", "grass", "water", "snow", "sky",
        "fire", "cave", "desert", "spooky",
    };
    if (theme < QUEST_PASSTHROUGH_THEME_NEUTRAL ||
        theme > QUEST_PASSTHROUGH_THEME_SPOOKY) {
        return "unknown";
    }
    return names[theme];
}

static const QuestPassthroughThemeStyle *passthrough_effect_style(
    enum QuestPassthroughEffect effect
) {
    if (effect < QUEST_PASSTHROUGH_EFFECT_NONE ||
        effect > QUEST_PASSTHROUGH_EFFECT_VANISH_CAP) {
        effect = QUEST_PASSTHROUGH_EFFECT_NONE;
    }
    return &sPassthroughEffectStyles[effect];
}

static const char *passthrough_effect_name(
    enum QuestPassthroughEffect effect
) {
    static const char *names[] = {
        "none", "damage", "heal", "star",
        "wing cap", "metal cap", "vanish cap",
    };
    if (effect < QUEST_PASSTHROUGH_EFFECT_NONE ||
        effect > QUEST_PASSTHROUGH_EFFECT_VANISH_CAP) {
        return "unknown";
    }
    return names[effect];
}

static QuestPassthroughThemeStyle blend_passthrough_styles(
    const QuestPassthroughThemeStyle *source,
    const QuestPassthroughThemeStyle *target,
    float weight
) {
    QuestPassthroughThemeStyle blended;
    for (uint32_t channel = 0; channel < 3; ++channel) {
        blended.tint[channel] = source->tint[channel] +
            (target->tint[channel] - source->tint[channel]) * weight;
    }
    blended.strength = source->strength +
        (target->strength - source->strength) * weight;
    blended.brightness = source->brightness +
        (target->brightness - source->brightness) * weight;
    blended.contrast = source->contrast +
        (target->contrast - source->contrast) * weight;
    blended.saturation = source->saturation +
        (target->saturation - source->saturation) * weight;
    blended.edge_color.r = source->edge_color.r +
        (target->edge_color.r - source->edge_color.r) * weight;
    blended.edge_color.g = source->edge_color.g +
        (target->edge_color.g - source->edge_color.g) * weight;
    blended.edge_color.b = source->edge_color.b +
        (target->edge_color.b - source->edge_color.b) * weight;
    blended.edge_color.a = source->edge_color.a +
        (target->edge_color.a - source->edge_color.a) * weight;
    return blended;
}

static XrDuration passthrough_effect_duration(
    enum QuestPassthroughEffect effect
) {
    switch (effect) {
        case QUEST_PASSTHROUGH_EFFECT_DAMAGE:
            return QUEST_DAMAGE_PULSE_DURATION_NS;
        case QUEST_PASSTHROUGH_EFFECT_HEAL:
            return QUEST_HEAL_PULSE_DURATION_NS;
        case QUEST_PASSTHROUGH_EFFECT_STAR:
            return QUEST_STAR_PULSE_DURATION_NS;
        default:
            return 0;
    }
}

static void build_passthrough_color_lut(
    const QuestPassthroughThemeStyle *style,
    uint8_t *buffer
) {
    const uint32_t resolution = QUEST_PASSTHROUGH_LUT_RESOLUTION;
    uint32_t offset = 0;
    for (uint32_t blue = 0; blue < resolution; ++blue) {
        for (uint32_t green = 0; green < resolution; ++green) {
            for (uint32_t red = 0; red < resolution; ++red) {
                const float input[3] = {
                    (float)red / (float)(resolution - 1U),
                    (float)green / (float)(resolution - 1U),
                    (float)blue / (float)(resolution - 1U),
                };
                const float luminance =
                    input[0] * 0.2126f +
                    input[1] * 0.7152f +
                    input[2] * 0.0722f;
                for (uint32_t channel = 0; channel < 3; ++channel) {
                    float value = luminance +
                        (input[channel] - luminance) * style->saturation;
                    const float tinted = value * style->tint[channel];
                    value += (tinted - value) * style->strength;
                    value = (value - 0.5f) *
                        (1.0f + style->contrast) +
                        0.5f + style->brightness;
                    buffer[offset++] = passthrough_color_byte(value);
                }
            }
        }
    }
}

static bool create_passthrough_color_lut(
    QuestApp *app,
    const QuestPassthroughThemeStyle *style,
    XrPassthroughColorLutMETA *color_lut
) {
    const uint32_t buffer_size =
        QUEST_PASSTHROUGH_LUT_RESOLUTION *
        QUEST_PASSTHROUGH_LUT_RESOLUTION *
        QUEST_PASSTHROUGH_LUT_RESOLUTION * 3U;
    uint8_t *buffer = malloc(buffer_size);
    if (buffer == NULL) return false;
    build_passthrough_color_lut(style, buffer);
    XrPassthroughColorLutCreateInfoMETA create_info = {
        .type = XR_TYPE_PASSTHROUGH_COLOR_LUT_CREATE_INFO_META,
        .channels = XR_PASSTHROUGH_COLOR_LUT_CHANNELS_RGB_META,
        .resolution = QUEST_PASSTHROUGH_LUT_RESOLUTION,
        .data = {
            .bufferSize = buffer_size,
            .buffer = buffer,
        },
    };
    const XrResult result = app->create_passthrough_color_lut(
        app->passthrough, &create_info, color_lut);
    free(buffer);
    return XR_SUCCEEDED(result);
}

static bool update_passthrough_color_lut(
    QuestApp *app,
    const QuestPassthroughThemeStyle *style,
    XrPassthroughColorLutMETA color_lut
) {
    const uint32_t buffer_size =
        QUEST_PASSTHROUGH_LUT_RESOLUTION *
        QUEST_PASSTHROUGH_LUT_RESOLUTION *
        QUEST_PASSTHROUGH_LUT_RESOLUTION * 3U;
    uint8_t *buffer = malloc(buffer_size);
    if (buffer == NULL) return false;
    build_passthrough_color_lut(style, buffer);
    XrPassthroughColorLutUpdateInfoMETA update_info = {
        .type = XR_TYPE_PASSTHROUGH_COLOR_LUT_UPDATE_INFO_META,
        .data = {
            .bufferSize = buffer_size,
            .buffer = buffer,
        },
    };
    const XrResult result = app->update_passthrough_color_lut(
        color_lut, &update_info);
    free(buffer);
    return XR_SUCCEEDED(result);
}

static void initialize_passthrough_styling(QuestApp *app) {
    xrGetInstanceProcAddr(app->instance, "xrPassthroughLayerSetStyleFB",
        (PFN_xrVoidFunction *)&app->set_passthrough_style);
    app->passthrough_style_supported =
        app->set_passthrough_style != NULL;
    app->active_passthrough_theme =
        (enum QuestPassthroughTheme)-1;
    app->active_passthrough_effect = QUEST_PASSTHROUGH_EFFECT_NONE;
    app->active_cap_effect = (enum QuestPassthroughEffect)-1;
    app->applied_passthrough_effect_weight = -1.0f;
    app->observed_passthrough_effect_serial =
        quest_game_passthrough_effect_serial();

    if (!app->passthrough_style_supported ||
        !app->passthrough_color_lut_supported) {
        LOGI("Passthrough styling path: %s.",
             app->passthrough_style_supported
                 ? "basic brightness/contrast/saturation (META color LUT unavailable)"
                 : "natural passthrough (style API unavailable)");
        app->passthrough_color_lut_supported = false;
        return;
    }
    xrGetInstanceProcAddr(app->instance, "xrCreatePassthroughColorLutMETA",
        (PFN_xrVoidFunction *)&app->create_passthrough_color_lut);
    xrGetInstanceProcAddr(app->instance, "xrDestroyPassthroughColorLutMETA",
        (PFN_xrVoidFunction *)&app->destroy_passthrough_color_lut);
    xrGetInstanceProcAddr(app->instance, "xrUpdatePassthroughColorLutMETA",
        (PFN_xrVoidFunction *)&app->update_passthrough_color_lut);
    app->passthrough_color_lut_supported =
        app->create_passthrough_color_lut != NULL &&
        app->destroy_passthrough_color_lut != NULL &&
        app->update_passthrough_color_lut != NULL;
    if (!app->passthrough_color_lut_supported) return;

    if (!create_passthrough_color_lut(
            app,
            passthrough_theme_style(QUEST_PASSTHROUGH_THEME_NEUTRAL),
            &app->theme_color_lut) ||
        !create_passthrough_color_lut(
            app,
            passthrough_effect_style(QUEST_PASSTHROUGH_EFFECT_STAR),
            &app->effect_color_lut)) {
        if (app->theme_color_lut != XR_NULL_HANDLE) {
            app->destroy_passthrough_color_lut(app->theme_color_lut);
            app->theme_color_lut = XR_NULL_HANDLE;
        }
        if (app->effect_color_lut != XR_NULL_HANDLE) {
            app->destroy_passthrough_color_lut(app->effect_color_lut);
            app->effect_color_lut = XR_NULL_HANDLE;
        }
        app->passthrough_color_lut_supported = false;
        LOGI("Passthrough color LUT creation failed; using basic styling.");
        return;
    }
    LOGI("Passthrough styling path: advanced META color LUT.");
}

static float passthrough_effect_weight(
    QuestApp *app,
    XrTime display_time
) {
    const XrDuration duration = passthrough_effect_duration(
        app->active_passthrough_effect);
    if (duration <= 0 || app->passthrough_effect_start_time == 0 ||
        display_time <= app->passthrough_effect_start_time) {
        return app->passthrough_effect_start_time == 0 ? 0.0f : 0.01f;
    }
    const XrDuration elapsed =
        display_time - app->passthrough_effect_start_time;
    if (elapsed >= duration) return 0.0f;
    const float progress = (float)elapsed / (float)duration;
    float weight;
    if (progress < 0.14f) {
        weight = progress / 0.14f;
    } else {
        weight = 1.0f - (progress - 0.14f) / 0.86f;
    }
    return weight * weight * (3.0f - 2.0f * weight);
}

static void update_passthrough_style(
    QuestApp *app,
    XrTime display_time
) {
    if (!app->passthrough_active ||
        !app->passthrough_style_supported) return;

    enum QuestPassthroughTheme theme = quest_game_passthrough_theme();
    const QuestPassthroughThemeStyle *theme_style =
        passthrough_theme_style(theme);
    const enum QuestPassthroughEffect cap_effect =
        quest_game_passthrough_cap_effect();
    const bool theme_changed =
        theme != app->active_passthrough_theme;
    const bool cap_changed = cap_effect != app->active_cap_effect;
    QuestPassthroughThemeStyle base_style = *theme_style;
    if (cap_effect != QUEST_PASSTHROUGH_EFFECT_NONE) {
        base_style = blend_passthrough_styles(
            theme_style,
            passthrough_effect_style(cap_effect),
            QUEST_CAP_STYLE_WEIGHT);
    }

    if (theme_changed || cap_changed) {
        LOGI("Passthrough theme: %s, cap: %s (%s path).",
             passthrough_theme_name(theme),
             passthrough_effect_name(cap_effect),
             app->passthrough_color_lut_supported ? "META LUT" : "basic");
    }
    if ((theme_changed || cap_changed) &&
        app->passthrough_color_lut_supported &&
        !update_passthrough_color_lut(
            app, &base_style, app->theme_color_lut)) {
        app->passthrough_color_lut_supported = false;
        LOGI("Passthrough LUT update failed; using basic styling.");
    }
    app->active_passthrough_theme = theme;
    app->active_cap_effect = cap_effect;

    const uint32_t effect_serial =
        quest_game_passthrough_effect_serial();
    if (effect_serial != app->observed_passthrough_effect_serial) {
        app->observed_passthrough_effect_serial = effect_serial;
        app->active_passthrough_effect =
            quest_game_passthrough_effect();
        app->passthrough_effect_start_time = display_time;
        LOGI("Passthrough effect: %s.",
             passthrough_effect_name(app->active_passthrough_effect));
        if (app->passthrough_color_lut_supported &&
            !update_passthrough_color_lut(
                app,
                passthrough_effect_style(app->active_passthrough_effect),
                app->effect_color_lut)) {
            app->passthrough_color_lut_supported = false;
            LOGI("Passthrough effect LUT update failed; using basic styling.");
        }
    }
    const float effect_weight =
        passthrough_effect_weight(app, display_time);
    if (!theme_changed && !cap_changed && effect_weight == 0.0f &&
        app->applied_passthrough_effect_weight == 0.0f) return;

    const QuestPassthroughThemeStyle *effect_style =
        passthrough_effect_style(app->active_passthrough_effect);

    XrPassthroughStyleFB style = {
        .type = XR_TYPE_PASSTHROUGH_STYLE_FB,
        .textureOpacityFactor = 1.0f,
        .edgeColor = {
            base_style.edge_color.r +
                (effect_style->edge_color.r - base_style.edge_color.r) * effect_weight,
            base_style.edge_color.g +
                (effect_style->edge_color.g - base_style.edge_color.g) * effect_weight,
            base_style.edge_color.b +
                (effect_style->edge_color.b - base_style.edge_color.b) * effect_weight,
            base_style.edge_color.a +
                (effect_style->edge_color.a - base_style.edge_color.a) * effect_weight,
        },
    };
    XrPassthroughColorMapInterpolatedLutMETA lut_map = {
        .type = XR_TYPE_PASSTHROUGH_COLOR_MAP_INTERPOLATED_LUT_META,
        .sourceColorLut = app->theme_color_lut,
        .targetColorLut = app->effect_color_lut,
        .weight = effect_weight,
    };
    XrPassthroughBrightnessContrastSaturationFB adjustment = {
        .type = XR_TYPE_PASSTHROUGH_BRIGHTNESS_CONTRAST_SATURATION_FB,
        .brightness = (base_style.brightness +
            (effect_style->brightness - base_style.brightness) *
                effect_weight) * 100.0f,
        .contrast = 1.0f + base_style.contrast +
            (effect_style->contrast - base_style.contrast) * effect_weight,
        .saturation = base_style.saturation +
            (effect_style->saturation - base_style.saturation) * effect_weight,
    };
    style.next = app->passthrough_color_lut_supported
        ? (const void *)&lut_map
        : (const void *)&adjustment;
    const XrResult result = app->set_passthrough_style(
        app->passthrough_layer, &style);
    if (XR_FAILED(result)) {
        LOGI("Passthrough styling was rejected (%d); keeping natural passthrough.",
             (int)result);
        app->passthrough_style_supported = false;
        return;
    }
    app->applied_passthrough_effect_weight = effect_weight;
}

static void initialize_passthrough(QuestApp *app) {
    if (!app->passthrough_supported) {
        LOGI("XR_FB_passthrough is unavailable; Tabletop will use its VR fallback.");
        return;
    }

    xrGetInstanceProcAddr(app->instance, "xrCreatePassthroughFB",
        (PFN_xrVoidFunction *)&app->create_passthrough);
    xrGetInstanceProcAddr(app->instance, "xrDestroyPassthroughFB",
        (PFN_xrVoidFunction *)&app->destroy_passthrough);
    xrGetInstanceProcAddr(app->instance, "xrPassthroughStartFB",
        (PFN_xrVoidFunction *)&app->start_passthrough);
    xrGetInstanceProcAddr(app->instance, "xrPassthroughPauseFB",
        (PFN_xrVoidFunction *)&app->pause_passthrough);
    xrGetInstanceProcAddr(app->instance, "xrCreatePassthroughLayerFB",
        (PFN_xrVoidFunction *)&app->create_passthrough_layer);
    xrGetInstanceProcAddr(app->instance, "xrDestroyPassthroughLayerFB",
        (PFN_xrVoidFunction *)&app->destroy_passthrough_layer);
    xrGetInstanceProcAddr(app->instance, "xrPassthroughLayerPauseFB",
        (PFN_xrVoidFunction *)&app->pause_passthrough_layer);
    xrGetInstanceProcAddr(app->instance, "xrPassthroughLayerResumeFB",
        (PFN_xrVoidFunction *)&app->resume_passthrough_layer);

    app->passthrough_supported = app->create_passthrough != NULL
        && app->destroy_passthrough != NULL
        && app->start_passthrough != NULL
        && app->pause_passthrough != NULL
        && app->create_passthrough_layer != NULL
        && app->destroy_passthrough_layer != NULL
        && app->pause_passthrough_layer != NULL
        && app->resume_passthrough_layer != NULL;
    if (!app->passthrough_supported) {
        LOGI("XR_FB_passthrough entry points are incomplete; using VR fallback.");
        return;
    }

    XrPassthroughCreateInfoFB passthrough_info = {
        .type = XR_TYPE_PASSTHROUGH_CREATE_INFO_FB,
    };
    XrResult result = app->create_passthrough(
        app->session, &passthrough_info, &app->passthrough);
    if (XR_FAILED(result)) {
        LOGI("xrCreatePassthroughFB declined Diorama MR (%d); using VR fallback.",
             (int)result);
        app->passthrough_supported = false;
        return;
    }

    XrPassthroughLayerCreateInfoFB layer_info = {
        .type = XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB,
        .passthrough = app->passthrough,
        .purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB,
    };
    result = app->create_passthrough_layer(
        app->session, &layer_info, &app->passthrough_layer);
    if (XR_FAILED(result)) {
        LOGI("xrCreatePassthroughLayerFB declined Diorama MR (%d); using VR fallback.",
             (int)result);
        app->destroy_passthrough(app->passthrough);
        app->passthrough = XR_NULL_HANDLE;
        app->passthrough_supported = false;
        return;
    }
    LOGI("Diorama MR passthrough layer is ready.");
    initialize_passthrough_styling(app);
}

static bool update_passthrough_state(QuestApp *app) {
    const bool requested = app->passthrough_supported
        && quest_game_tabletop_mr_requested();
    if (requested == app->passthrough_active) return app->passthrough_active;

    if (requested) {
        XrResult result = app->start_passthrough(app->passthrough);
        if (XR_SUCCEEDED(result)) {
            result = app->resume_passthrough_layer(app->passthrough_layer);
        }
        if (XR_FAILED(result)) {
            app->pause_passthrough(app->passthrough);
            LOGI("Could not activate Diorama MR passthrough (%d).", (int)result);
            return false;
        }
        app->passthrough_active = true;
        app->applied_passthrough_effect_weight = -1.0f;
        LOGI("Diorama MR passthrough activated.");
    } else {
        app->pause_passthrough_layer(app->passthrough_layer);
        app->pause_passthrough(app->passthrough);
        app->passthrough_active = false;
        LOGI("Diorama MR passthrough paused.");
    }
    return app->passthrough_active;
}

static void load_controller_render_model_probe(QuestApp *app) {
    if (!app->render_model_supported || app->session == XR_NULL_HANDLE) {
        LOGI("OpenXR controller render models are unavailable.");
        return;
    }

    xrGetInstanceProcAddr(app->instance, "xrEnumerateRenderModelPathsFB",
        (PFN_xrVoidFunction *)&app->enumerate_render_model_paths);
    xrGetInstanceProcAddr(app->instance, "xrGetRenderModelPropertiesFB",
        (PFN_xrVoidFunction *)&app->get_render_model_properties);
    xrGetInstanceProcAddr(app->instance, "xrLoadRenderModelFB",
        (PFN_xrVoidFunction *)&app->load_render_model);
    if (app->enumerate_render_model_paths == NULL ||
        app->get_render_model_properties == NULL ||
        app->load_render_model == NULL) {
        app->render_model_supported = false;
        LOGI("XR_FB_render_model entry points are unavailable.");
        return;
    }

    uint32_t path_count = 0;
    XrResult result = app->enumerate_render_model_paths(
        app->session, 0, &path_count, NULL);
    if (XR_FAILED(result) || path_count == 0) {
        LOGI("No OpenXR render model paths were enumerated (result %d).",
             (int)result);
        return;
    }

    XrRenderModelPathInfoFB *paths = calloc(path_count, sizeof(*paths));
    if (paths == NULL) return;
    for (uint32_t i = 0; i < path_count; ++i) {
        paths[i].type = XR_TYPE_RENDER_MODEL_PATH_INFO_FB;
    }
    result = app->enumerate_render_model_paths(
        app->session, path_count, &path_count, paths);
    if (XR_FAILED(result)) {
        LOGI("Could not enumerate OpenXR render model paths (result %d).",
             (int)result);
        free(paths);
        return;
    }

    for (uint32_t i = 0; i < path_count; ++i) {
        char path_string[XR_MAX_PATH_LENGTH] = {0};
        uint32_t path_length = 0;
        if (XR_FAILED(xrPathToString(
                app->instance, paths[i].path, sizeof(path_string),
                &path_length, path_string))) {
            continue;
        }
        if (strcmp(path_string, "/model_fb/controller/left") != 0 &&
            strcmp(path_string, "/model_fb/controller/right") != 0) {
            continue;
        }

        XrRenderModelCapabilitiesRequestFB capabilities = {
            .type = XR_TYPE_RENDER_MODEL_CAPABILITIES_REQUEST_FB,
            .flags = XR_RENDER_MODEL_SUPPORTS_GLTF_2_0_SUBSET_2_BIT_FB,
        };
        XrRenderModelPropertiesFB properties = {
            .type = XR_TYPE_RENDER_MODEL_PROPERTIES_FB,
            .next = &capabilities,
        };
        result = app->get_render_model_properties(
            app->session, paths[i].path, &properties);
        LOGI("Render model %s: result=%d name=%s version=%u flags=%llu key=%llu.",
             path_string, (int)result, properties.modelName,
             properties.modelVersion,
             (unsigned long long)properties.flags,
             (unsigned long long)properties.modelKey);
        if (XR_FAILED(result) || properties.modelKey == 0) continue;

        XrRenderModelLoadInfoFB load_info = {
            .type = XR_TYPE_RENDER_MODEL_LOAD_INFO_FB,
            .modelKey = properties.modelKey,
        };
        XrRenderModelBufferFB model_buffer = {
            .type = XR_TYPE_RENDER_MODEL_BUFFER_FB,
        };
        result = app->load_render_model(
            app->session, &load_info, &model_buffer);
        if (XR_FAILED(result) || model_buffer.bufferCountOutput == 0) {
            LOGI("Could not query %s GLB size (result %d).",
                 path_string, (int)result);
            continue;
        }
        model_buffer.buffer = malloc(model_buffer.bufferCountOutput);
        if (model_buffer.buffer == NULL) continue;
        model_buffer.bufferCapacityInput = model_buffer.bufferCountOutput;
        result = app->load_render_model(
            app->session, &load_info, &model_buffer);
        if (XR_SUCCEEDED(result)) {
            const char *side = strstr(path_string, "left") != NULL
                ? "left" : "right";
            char output_path[1024];
            snprintf(output_path, sizeof(output_path),
                "%s/controller-%s.glb",
                app->android_app->activity->internalDataPath, side);
            FILE *output = fopen(output_path, "wb");
            if (output != NULL) {
                const size_t written = fwrite(model_buffer.buffer, 1,
                    model_buffer.bufferCountOutput, output);
                fclose(output);
                LOGI("Saved %s controller GLB probe: %zu/%u bytes.",
                     side, written, model_buffer.bufferCountOutput);
            }
        } else {
            LOGI("Could not load %s GLB (result %d).",
                 path_string, (int)result);
        }
        free(model_buffer.buffer);
    }
    free(paths);
}

static bool create_openxr_session(QuestApp *app) {
    PFN_xrGetOpenGLESGraphicsRequirementsKHR get_requirements = NULL;
    if (!xr_check(
            "xrGetInstanceProcAddr(xrGetOpenGLESGraphicsRequirementsKHR)",
            xrGetInstanceProcAddr(
                app->instance,
                "xrGetOpenGLESGraphicsRequirementsKHR",
                (PFN_xrVoidFunction *)&get_requirements))
        || get_requirements == NULL) {
        return false;
    }

    XrGraphicsRequirementsOpenGLESKHR requirements = {
        .type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR,
    };
    if (!xr_check(
            "xrGetOpenGLESGraphicsRequirementsKHR",
            get_requirements(app->instance, app->system_id, &requirements))) {
        return false;
    }

    XrGraphicsBindingOpenGLESAndroidKHR graphics_binding = {
        .type = XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR,
        .display = app->egl_display,
        .config = app->egl_config,
        .context = app->egl_context,
    };
    XrSessionCreateInfo session_info = {
        .type = XR_TYPE_SESSION_CREATE_INFO,
        .next = &graphics_binding,
        .systemId = app->system_id,
    };
    if (!xr_check("xrCreateSession", xrCreateSession(
            app->instance, &session_info, &app->session))) {
        return false;
    }

    load_controller_render_model_probe(app);

    if (app->performance_settings_supported) {
        xrGetInstanceProcAddr(
            app->instance,
            "xrPerfSettingsSetPerformanceLevelEXT",
            (PFN_xrVoidFunction *)&app->set_performance_level
        );
        app->performance_settings_supported =
            app->set_performance_level != NULL;
    }
    if (app->performance_settings_supported) {
        const XrResult cpu_result = app->set_performance_level(
            app->session,
            XR_PERF_SETTINGS_DOMAIN_CPU_EXT,
            XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT
        );
        const XrResult gpu_result = app->set_performance_level(
            app->session,
            XR_PERF_SETTINGS_DOMAIN_GPU_EXT,
            XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT
        );
        if (XR_SUCCEEDED(cpu_result) && XR_SUCCEEDED(gpu_result)) {
            LOGI("Quest sustained-high CPU/GPU performance levels enabled.");
        } else {
            LOGI("Quest runtime declined sustained performance levels (CPU %d, GPU %d).",
                 (int)cpu_result, (int)gpu_result);
        }
    } else {
        LOGI("OpenXR performance-level control is unavailable; using runtime defaults.");
    }

    configure_display_refresh_rate(app);

    if (app->foveation_supported) {
        xrGetInstanceProcAddr(app->instance, "xrCreateFoveationProfileFB",
            (PFN_xrVoidFunction *)&app->create_foveation_profile);
        xrGetInstanceProcAddr(app->instance, "xrDestroyFoveationProfileFB",
            (PFN_xrVoidFunction *)&app->destroy_foveation_profile);
        xrGetInstanceProcAddr(app->instance, "xrUpdateSwapchainFB",
            (PFN_xrVoidFunction *)&app->update_swapchain);
        app->foveation_supported = app->create_foveation_profile != NULL
            && app->destroy_foveation_profile != NULL
            && app->update_swapchain != NULL;
    }
    if (app->foveation_supported) {
        app->foveation_profiles[0] = create_foveation_level_profile(
            app, XR_FOVEATION_LEVEL_NONE_FB);
        app->foveation_profiles[1] = create_foveation_level_profile(
            app, XR_FOVEATION_LEVEL_MEDIUM_FB);
        app->foveation_profiles[2] = create_foveation_level_profile(
            app, XR_FOVEATION_LEVEL_HIGH_FB);
        if (app->foveation_profiles[1] == XR_NULL_HANDLE) {
            app->foveation_supported = false;
            LOGI("Quest fixed foveated rendering is unavailable.");
        } else {
            LOGI("Quest fixed foveated rendering profiles are ready%s%s.",
                 app->foveation_profiles[0] != XR_NULL_HANDLE
                     ? " (off/medium" : " (medium",
                 app->foveation_profiles[2] != XR_NULL_HANDLE
                     ? "/high)" : ")");
        }
    }

    XrReferenceSpaceCreateInfo space_info = {
        .type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
        .referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL,
        .poseInReferenceSpace = {
            .orientation = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f},
            .position = {.x = 0.0f, .y = 0.0f, .z = 0.0f},
        },
    };
    if (!xr_check(
            "xrCreateReferenceSpace",
            xrCreateReferenceSpace(app->session, &space_info, &app->local_space))) {
        return false;
    }

    if (!select_blend_mode(app)) {
        return false;
    }

    initialize_passthrough(app);
    initialize_room_mesh(app);

    LOGI("OpenXR OpenGL ES session created.");
    return true;
}

static int64_t select_swapchain_format(QuestApp *app) {
    uint32_t format_count = 0;
    if (!xr_check(
            "xrEnumerateSwapchainFormats(count)",
            xrEnumerateSwapchainFormats(
                app->session, 0, &format_count, NULL))
        || format_count == 0) {
        return 0;
    }

    int64_t *formats = (int64_t *)calloc(format_count, sizeof(*formats));
    if (formats == NULL) {
        LOGE("Could not allocate swapchain format list.");
        return 0;
    }

    if (!xr_check(
            "xrEnumerateSwapchainFormats",
            xrEnumerateSwapchainFormats(
                app->session, format_count, &format_count, formats))) {
        free(formats);
        return 0;
    }

    int64_t selected = formats[0];
    for (uint32_t index = 0; index < format_count; ++index) {
        if (formats[index] == GL_SRGB8_ALPHA8) {
            selected = GL_SRGB8_ALPHA8;
            break;
        }
        if (formats[index] == GL_RGBA8) {
            selected = GL_RGBA8;
        }
    }
    free(formats);
    return selected;
}

static void destroy_swapchain_render_targets(QuestApp *app) {
    // This is only called between submitted frames, when no image is acquired.
    glFinish();
    for (uint32_t eye = 0; eye < QUEST_VIEW_COUNT; ++eye) {
        QuestSwapchain *swapchain = &app->swapchains[eye];
        if (swapchain->framebuffers != NULL) {
            glDeleteFramebuffers(
                (GLsizei)swapchain->image_count,
                swapchain->framebuffers
            );
            free(swapchain->framebuffers);
            swapchain->framebuffers = NULL;
        }
        free(swapchain->images);
        swapchain->images = NULL;
        swapchain->image_count = 0;
        if (swapchain->handle != XR_NULL_HANDLE) {
            xrDestroySwapchain(swapchain->handle);
            swapchain->handle = XR_NULL_HANDLE;
        }
    }
    if (app->depth_buffer != 0) {
        glDeleteRenderbuffers(1, &app->depth_buffer);
        app->depth_buffer = 0;
    }
}

static bool create_swapchains(QuestApp *app) {
    uint32_t view_count = 0;
    if (!xr_check(
            "xrEnumerateViewConfigurationViews(count)",
            xrEnumerateViewConfigurationViews(
                app->instance,
                app->system_id,
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                0,
                &view_count,
                NULL))
        || view_count != QUEST_VIEW_COUNT) {
        LOGE("Primary stereo reported %u views; expected %u.",
             view_count,
             QUEST_VIEW_COUNT);
        return false;
    }

    unsigned int render_scale = quest_game_render_scale_percent();
    LOGI("Quest eye render scale: %u%% (applies after app restart).", render_scale);

    for (uint32_t eye = 0; eye < QUEST_VIEW_COUNT; ++eye) {
        app->view_configs[eye].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
        app->views[eye].type = XR_TYPE_VIEW;
    }
    if (!xr_check(
            "xrEnumerateViewConfigurationViews",
            xrEnumerateViewConfigurationViews(
                app->instance,
                app->system_id,
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                QUEST_VIEW_COUNT,
                &view_count,
                app->view_configs))) {
        return false;
    }

    const int64_t format = select_swapchain_format(app);
    if (format == 0) {
        return false;
    }

    const bool ui_full_quality = quest_game_ui_requires_full_quality();
    const bool ultra_foveation = quest_game_ultra_performance_enabled();
    const bool low_scale_foveation = !ui_full_quality && render_scale <= 80U;
    const unsigned int foveation_index =
        ui_full_quality && app->foveation_profiles[0] != XR_NULL_HANDLE
            ? 0U
            : (ultra_foveation || low_scale_foveation) &&
              app->foveation_profiles[2] != XR_NULL_HANDLE
                ? 2U : 1U;
    const XrFoveationProfileFB active_foveation =
        app->foveation_profiles[foveation_index];
    app->active_foveation_profile = foveation_index;
    LOGI("Quest initial foveation profile: %s.",
         foveation_index == 0U ? "off (full-quality UI)"
             : foveation_index == 2U ? "high" : "medium");

    for (uint32_t eye = 0; eye < QUEST_VIEW_COUNT; ++eye) {
        QuestSwapchain *swapchain = &app->swapchains[eye];
        swapchain->width = (int32_t)(
            (app->view_configs[eye].recommendedImageRectWidth *
             render_scale + 50U) / 100U);
        swapchain->height = (int32_t)(
            (app->view_configs[eye].recommendedImageRectHeight *
             render_scale + 50U) / 100U);

        XrSwapchainCreateInfoFoveationFB foveation_create = {
            .type = XR_TYPE_SWAPCHAIN_CREATE_INFO_FOVEATION_FB,
            .flags = 0,
        };
        XrSwapchainCreateInfo create_info = {
            .type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
            .next = app->foveation_supported ? &foveation_create : NULL,
            .usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
            .format = format,
            .sampleCount = 1,
            .width = (uint32_t)swapchain->width,
            .height = (uint32_t)swapchain->height,
            .faceCount = 1,
            .arraySize = 1,
            .mipCount = 1,
        };
        if (!xr_check(
                "xrCreateSwapchain",
                xrCreateSwapchain(
                    app->session, &create_info, &swapchain->handle))) {
            return false;
        }
        if (app->foveation_supported) {
            XrSwapchainStateFoveationFB state = {
                .type = XR_TYPE_SWAPCHAIN_STATE_FOVEATION_FB,
                .profile = active_foveation,
            };
            if (XR_FAILED(app->update_swapchain(
                    swapchain->handle,
                    (const XrSwapchainStateBaseHeaderFB *)&state))) {
                LOGI("Could not apply foveation to the %s eye swapchain.",
                     eye == 0 ? "left" : "right");
            }
        }

        if (!xr_check(
                "xrEnumerateSwapchainImages(count)",
                xrEnumerateSwapchainImages(
                    swapchain->handle, 0, &swapchain->image_count, NULL))
            || swapchain->image_count == 0) {
            return false;
        }

        swapchain->images = (XrSwapchainImageOpenGLESKHR *)calloc(
            swapchain->image_count, sizeof(*swapchain->images));
        if (swapchain->images == NULL) {
            LOGE("Could not allocate swapchain image list.");
            return false;
        }
        for (uint32_t image = 0; image < swapchain->image_count; ++image) {
            swapchain->images[image].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
        }
        if (!xr_check(
                "xrEnumerateSwapchainImages",
                xrEnumerateSwapchainImages(
                    swapchain->handle,
                    swapchain->image_count,
                    &swapchain->image_count,
                    (XrSwapchainImageBaseHeader *)swapchain->images))) {
            return false;
        }

        LOGI("%s eye swapchain: %dx%d, %u images.",
             eye == 0 ? "Left" : "Right",
             swapchain->width,
             swapchain->height,
             swapchain->image_count);
    }

    glGenRenderbuffers(1, &app->depth_buffer);
    app->depth_width = app->swapchains[0].width;
    app->depth_height = app->swapchains[0].height;
    for (uint32_t eye = 1; eye < QUEST_VIEW_COUNT; ++eye) {
        if (app->swapchains[eye].width > app->depth_width)
            app->depth_width = app->swapchains[eye].width;
        if (app->swapchains[eye].height > app->depth_height)
            app->depth_height = app->swapchains[eye].height;
    }
    glBindRenderbuffer(GL_RENDERBUFFER, app->depth_buffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                          app->depth_width, app->depth_height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    // Framebuffer attachment state is invariant for each OpenXR image. Build
    // it once here instead of reattaching color/depth and validating the FBO
    // for both eyes on every 72/90 Hz headset frame.
    for (uint32_t eye = 0; eye < QUEST_VIEW_COUNT; ++eye) {
        QuestSwapchain *swapchain = &app->swapchains[eye];
        swapchain->framebuffers = calloc(
            swapchain->image_count, sizeof(*swapchain->framebuffers));
        if (swapchain->framebuffers == NULL) return false;
        glGenFramebuffers(
            (GLsizei)swapchain->image_count,
            swapchain->framebuffers
        );
        for (uint32_t image = 0; image < swapchain->image_count; ++image) {
            glBindFramebuffer(
                GL_FRAMEBUFFER, swapchain->framebuffers[image]);
            glFramebufferTexture2D(
                GL_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D,
                swapchain->images[image].image,
                0
            );
            glFramebufferRenderbuffer(
                GL_FRAMEBUFFER,
                GL_DEPTH_ATTACHMENT,
                GL_RENDERBUFFER,
                app->depth_buffer
            );
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
                    GL_FRAMEBUFFER_COMPLETE) {
                LOGE("%s eye framebuffer %u is incomplete.",
                     eye == 0 ? "Left" : "Right", image);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                return false;
            }
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    LOGI("OpenGL ES depth buffer: %dx%d, 24-bit.",
         app->depth_width, app->depth_height);
    app->active_render_scale = render_scale;
    app->pending_render_scale = render_scale;
    app->render_scale_stable_frames = 0;
    return true;
}

static bool update_render_scale_if_needed(QuestApp *app) {
    const unsigned int requested = quest_game_render_scale_percent();
    if (requested == app->active_render_scale) {
        app->pending_render_scale = requested;
        app->render_scale_stable_frames = 0;
        return true;
    }
    if (requested != app->pending_render_scale) {
        app->pending_render_scale = requested;
        app->render_scale_stable_frames = 0;
        return true;
    }

    // Wait until the slider has stopped moving so holding the stick does not
    // repeatedly allocate and destroy large OpenXR textures.
    if (++app->render_scale_stable_frames < 30U) return true;

    LOGI("Applying live Quest render scale change: %u%% -> %u%%.",
         app->active_render_scale, requested);
    destroy_swapchain_render_targets(app);
    return create_swapchains(app);
}

static void update_foveation_if_needed(QuestApp *app) {
    if (!app->foveation_supported) return;
    const bool ui_full_quality = quest_game_ui_requires_full_quality();
    const bool ultra = quest_game_ultra_performance_enabled();
    // A smaller eye target removes pixels, but Adreno can still spend most of
    // the frame shading the center/periphery uniformly. Couple the lower half
    // of the render-scale slider to high fixed foveation so lowering the scale
    // produces a real GPU-time reduction, while menus retain their dedicated
    // full-quality/off profile.
    const bool low_scale = !ui_full_quality &&
        app->active_render_scale <= 80U;
    const unsigned int desired =
        ui_full_quality && app->foveation_profiles[0] != XR_NULL_HANDLE
            ? 0U
            : (ultra || low_scale) &&
              app->foveation_profiles[2] != XR_NULL_HANDLE
                ? 2U : 1U;
    if (desired == app->active_foveation_profile) return;

    const XrFoveationProfileFB profile = app->foveation_profiles[desired];
    if (profile == XR_NULL_HANDLE) return;

    for (uint32_t eye = 0; eye < QUEST_VIEW_COUNT; ++eye) {
        if (app->swapchains[eye].handle == XR_NULL_HANDLE) continue;
        XrSwapchainStateFoveationFB state = {
            .type = XR_TYPE_SWAPCHAIN_STATE_FOVEATION_FB,
            .profile = profile,
        };
        if (XR_FAILED(app->update_swapchain(
                app->swapchains[eye].handle,
                (const XrSwapchainStateBaseHeaderFB *)&state))) {
            LOGI("Could not update %s eye foveation profile.",
                 eye == 0 ? "left" : "right");
            return;
        }
    }
    app->active_foveation_profile = desired;
    LOGI("Quest foveation changed to %s profile.",
         desired == 0U ? "off (full-quality UI)"
             : desired == 2U ? "high" : "medium");
}

static void update_display_refresh_rate_if_needed(QuestApp *app) {
    if (!app->display_refresh_rate_supported) return;
    const unsigned int requested = quest_game_refresh_rate_index();
    if (requested == app->active_refresh_rate_index) return;
    configure_display_refresh_rate(app);
}

static bool room_space_has_component(
    QuestApp *app, XrSpace space, XrSpaceComponentTypeFB wanted) {
    uint32_t count = 0;
    if (XR_FAILED(app->enumerate_space_components(
            space, 0, &count, NULL)) || count == 0) return false;
    XrSpaceComponentTypeFB *components =
        calloc(count, sizeof(*components));
    if (components == NULL) return false;
    const XrResult result = app->enumerate_space_components(
        space, count, &count, components);
    bool found = false;
    if (XR_SUCCEEDED(result)) {
        for (uint32_t i = 0; i < count; ++i) {
            if (components[i] == wanted) {
                found = true;
                break;
            }
        }
    }
    free(components);
    return found;
}

static bool room_space_component_enabled(
    QuestApp *app, XrSpace space, XrSpaceComponentTypeFB type) {
    XrSpaceComponentStatusFB status = {
        .type = XR_TYPE_SPACE_COMPONENT_STATUS_FB,
    };
    return XR_SUCCEEDED(app->get_space_component_status(
        space, type, &status)) && status.enabled && !status.changePending;
}

static void upload_scene_mesh_buffers(
    QuestRoomMesh *mesh, const XrVector3f *vertices,
    uint32_t vertex_count, const uint32_t *indices,
    uint32_t index_count) {
    GLint previous_vertex_array = 0;
    GLint previous_array_buffer = 0;
    GLint previous_element_buffer = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous_vertex_array);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous_array_buffer);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,
                  &previous_element_buffer);

    /* GL_ELEMENT_ARRAY_BUFFER is VAO state. Scene query events arrive between
     * game frames, while the SM64 renderer's VAO may still be bound. Upload
     * through VAO 0 so creating the room mesh cannot replace the game's EBO. */
    glBindVertexArray(0);
    glGenBuffers(1, &mesh->vertex_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, mesh->vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, vertex_count * sizeof(*vertices),
                 vertices, GL_STATIC_DRAW);
    glGenBuffers(1, &mesh->index_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->index_buffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, index_count * sizeof(*indices),
                 indices, GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)previous_array_buffer);
    glBindVertexArray((GLuint)previous_vertex_array);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,
                 (GLuint)previous_element_buffer);
}

static bool load_room_mesh(QuestApp *app, XrSpace space) {
    for (uint32_t i = 0; i < app->room_mesh_count; ++i) {
        if (app->room_meshes[i].space == space) return true;
    }
    if (app->room_mesh_count >= QUEST_ROOM_MESH_MAX) {
        xrDestroySpace(space);
        return false;
    }

    static const XrSemanticLabelMETA labels[] = {
        XR_SEMANTIC_LABEL_FLOOR_META,
        XR_SEMANTIC_LABEL_CEILING_META,
        XR_SEMANTIC_LABEL_WALL_FACE_META,
        XR_SEMANTIC_LABEL_INNER_WALL_FACE_META,
        XR_SEMANTIC_LABEL_INVISIBLE_WALL_FACE_META,
        XR_SEMANTIC_LABEL_DOOR_FRAME_META,
        XR_SEMANTIC_LABEL_WINDOW_FRAME_META,
    };
    XrSpaceRoomMeshGetInfoMETA get_info = {
        .type = XR_TYPE_SPACE_ROOM_MESH_GET_INFO_META,
        .recognizedSemanticLabelCount =
            (uint32_t)(sizeof(labels) / sizeof(labels[0])),
        .recognizedSemanticLabels = labels,
    };
    XrRoomMeshMETA room = {.type = XR_TYPE_ROOM_MESH_META};
    XrResult result = app->get_space_room_mesh(space, &get_info, &room);
    if (XR_FAILED(result) || room.vertexCountOutput == 0 ||
        room.faceCountOutput == 0 || room.vertexCountOutput > 2000000U ||
        room.faceCountOutput > 1000000U) {
        LOGI("Room Mesh geometry was unavailable (%d, %u vertices, %u faces).",
             (int)result, room.vertexCountOutput, room.faceCountOutput);
        xrDestroySpace(space);
        return false;
    }

    XrVector3f *vertices = calloc(room.vertexCountOutput, sizeof(*vertices));
    XrRoomMeshFaceMETA *faces =
        calloc(room.faceCountOutput, sizeof(*faces));
    if (vertices == NULL || faces == NULL) {
        free(vertices);
        free(faces);
        xrDestroySpace(space);
        return false;
    }
    room.vertexCapacityInput = room.vertexCountOutput;
    room.vertices = vertices;
    room.faceCapacityInput = room.faceCountOutput;
    room.faces = faces;
    result = app->get_space_room_mesh(space, &get_info, &room);
    if (XR_FAILED(result)) {
        LOGI("Could not read Room Mesh vertices (%d).", (int)result);
        free(vertices);
        free(faces);
        xrDestroySpace(space);
        return false;
    }

    uint32_t *indices = NULL;
    uint32_t index_count = 0;
    for (uint32_t face = 0; face < room.faceCountOutput; ++face) {
        XrRoomMeshFaceIndicesMETA face_indices = {
            .type = XR_TYPE_ROOM_MESH_FACE_INDICES_META,
        };
        result = app->get_room_mesh_face_indices(
            space, &faces[face].uuid, &face_indices);
        if (XR_FAILED(result) || face_indices.indexCountOutput == 0) continue;
        if (face_indices.indexCountOutput > 6000000U - index_count) {
            LOGI("Room Mesh index data exceeded the safety limit.");
            free(indices);
            free(vertices);
            free(faces);
            xrDestroySpace(space);
            return false;
        }
        const uint32_t new_count =
            index_count + face_indices.indexCountOutput;
        uint32_t *grown = realloc(indices, new_count * sizeof(*grown));
        if (grown == NULL) {
            free(indices);
            free(vertices);
            free(faces);
            xrDestroySpace(space);
            return false;
        }
        indices = grown;
        face_indices.indexCapacityInput = face_indices.indexCountOutput;
        face_indices.indices = indices + index_count;
        result = app->get_room_mesh_face_indices(
            space, &faces[face].uuid, &face_indices);
        if (XR_SUCCEEDED(result)) {
            index_count += face_indices.indexCountOutput;
        }
    }
    free(faces);
    if (index_count == 0) {
        free(indices);
        free(vertices);
        xrDestroySpace(space);
        return false;
    }

    QuestRoomMesh *mesh = &app->room_meshes[app->room_mesh_count];
    mesh->space = space;
    mesh->index_count = index_count;
    upload_scene_mesh_buffers(
        mesh, vertices, room.vertexCountOutput, indices, index_count);
    free(indices);
    free(vertices);
    ++app->room_mesh_count;
    LOGI("Loaded Room Mesh %u: %u vertices, %u triangle indices.",
         app->room_mesh_count, room.vertexCountOutput, index_count);
    return true;
}

static bool load_triangle_mesh(QuestApp *app, XrSpace space) {
    for (uint32_t i = 0; i < app->room_mesh_count; ++i) {
        if (app->room_meshes[i].space == space) return true;
    }
    if (app->room_mesh_count >= QUEST_ROOM_MESH_MAX) {
        xrDestroySpace(space);
        return false;
    }
    XrSpaceTriangleMeshGetInfoMETA get_info = {
        .type = XR_TYPE_SPACE_TRIANGLE_MESH_GET_INFO_META,
    };
    XrSpaceTriangleMeshMETA triangle = {
        .type = XR_TYPE_SPACE_TRIANGLE_MESH_META,
    };
    XrResult result =
        app->get_space_triangle_mesh(space, &get_info, &triangle);
    if (XR_FAILED(result) || triangle.vertexCountOutput == 0 ||
        triangle.indexCountOutput == 0 ||
        triangle.vertexCountOutput > 2000000U ||
        triangle.indexCountOutput > 6000000U) {
        LOGI("Triangle Scene Mesh geometry was unavailable (%d, %u vertices, %u indices).",
             (int)result, triangle.vertexCountOutput,
             triangle.indexCountOutput);
        xrDestroySpace(space);
        return false;
    }
    XrVector3f *vertices =
        calloc(triangle.vertexCountOutput, sizeof(*vertices));
    uint32_t *indices =
        calloc(triangle.indexCountOutput, sizeof(*indices));
    if (vertices == NULL || indices == NULL) {
        free(vertices);
        free(indices);
        xrDestroySpace(space);
        return false;
    }
    triangle.vertexCapacityInput = triangle.vertexCountOutput;
    triangle.vertices = vertices;
    triangle.indexCapacityInput = triangle.indexCountOutput;
    triangle.indices = indices;
    result = app->get_space_triangle_mesh(space, &get_info, &triangle);
    if (XR_FAILED(result)) {
        LOGI("Could not read Triangle Scene Mesh (%d).", (int)result);
        free(vertices);
        free(indices);
        xrDestroySpace(space);
        return false;
    }

    QuestRoomMesh *mesh = &app->room_meshes[app->room_mesh_count];
    mesh->space = space;
    mesh->index_count = triangle.indexCountOutput;
    upload_scene_mesh_buffers(
        mesh, vertices, triangle.vertexCountOutput,
        indices, triangle.indexCountOutput);
    free(vertices);
    free(indices);
    ++app->room_mesh_count;
    LOGI("Loaded Triangle Scene Mesh %u: %u vertices, %u indices.",
         app->room_mesh_count, triangle.vertexCountOutput,
         triangle.indexCountOutput);
    return true;
}

static void handle_room_query_results(
    QuestApp *app, XrAsyncRequestIdFB request_id) {
    XrSpaceQueryResultsFB query = {
        .type = XR_TYPE_SPACE_QUERY_RESULTS_FB,
    };
    XrResult result = app->retrieve_space_query_results(
        app->session, request_id, &query);
    if (XR_FAILED(result) || query.resultCountOutput == 0) {
        LOGI("%s query returned no scene meshes (%d).",
             app->room_query_uses_triangle_mesh
                 ? "Triangle Scene Mesh" : "Room Mesh",
             (int)result);
        return;
    }
    XrSpaceQueryResultFB *results =
        calloc(query.resultCountOutput, sizeof(*results));
    if (results == NULL) return;
    query.resultCapacityInput = query.resultCountOutput;
    query.results = results;
    result = app->retrieve_space_query_results(
        app->session, request_id, &query);
    if (XR_FAILED(result)) {
        free(results);
        return;
    }

    for (uint32_t i = 0; i < query.resultCountOutput; ++i) {
        XrSpace space = results[i].space;
        if (!room_space_has_component(
                app, space, XR_SPACE_COMPONENT_TYPE_LOCATABLE_FB)) {
            xrDestroySpace(space);
            continue;
        }
        XrSpaceComponentStatusSetInfoFB set_info = {
            .type = XR_TYPE_SPACE_COMPONENT_STATUS_SET_INFO_FB,
            .componentType = XR_SPACE_COMPONENT_TYPE_LOCATABLE_FB,
            .enabled = XR_TRUE,
            .timeout = 0,
        };
        XrAsyncRequestIdFB set_request = 0;
        result = app->set_space_component_status(
            space, &set_info, &set_request);
        if (result == XR_ERROR_SPACE_COMPONENT_STATUS_ALREADY_SET_FB ||
            room_space_component_enabled(
                app, space, XR_SPACE_COMPONENT_TYPE_LOCATABLE_FB)) {
            if (app->room_query_uses_triangle_mesh) {
                (void)load_triangle_mesh(app, space);
            } else {
                (void)load_room_mesh(app, space);
            }
        } else if (XR_FAILED(result)) {
            LOGI("Could not make Room Mesh locatable (%d).", (int)result);
            xrDestroySpace(space);
        }
    }
    free(results);
}

static void request_room_mesh_query(QuestApp *app, XrTime display_time) {
    if ((!app->room_mesh_supported && !app->triangle_mesh_supported) ||
        app->room_query_pending ||
        app->room_query_complete || app->room_mesh_count > 0 ||
        !quest_game_tabletop_mr_requested() ||
        app->session_state != XR_SESSION_STATE_FOCUSED ||
        display_time < app->room_query_retry_time) return;

    XrSpaceStorageLocationFilterInfoFB storage = {
        .type = XR_TYPE_SPACE_STORAGE_LOCATION_FILTER_INFO_FB,
        .location = XR_SPACE_STORAGE_LOCATION_LOCAL_FB,
    };
    XrSpaceComponentFilterInfoFB component = {
        .type = XR_TYPE_SPACE_COMPONENT_FILTER_INFO_FB,
        .next = &storage,
        .componentType = app->room_query_uses_triangle_mesh
            ? XR_SPACE_COMPONENT_TYPE_TRIANGLE_MESH_META
            : XR_SPACE_COMPONENT_TYPE_ROOM_MESH_META,
    };
    XrSpaceQueryInfoFB query = {
        .type = XR_TYPE_SPACE_QUERY_INFO_FB,
        .queryAction = XR_SPACE_QUERY_ACTION_LOAD_FB,
        .maxResultCount = QUEST_ROOM_QUERY_MAX_RESULTS,
        .timeout = 0,
        .filter = (const XrSpaceFilterInfoBaseHeaderFB *)&component,
    };
    XrAsyncRequestIdFB request_id = 0;
    const XrResult result = app->query_spaces(
        app->session, (const XrSpaceQueryInfoBaseHeaderFB *)&query,
        &request_id);
    if (XR_SUCCEEDED(result)) {
        app->room_query_pending = true;
        LOGI("Requested the Meta %s for the tabletop star pulse.",
             app->room_query_uses_triangle_mesh
                 ? "Triangle Scene Mesh fallback" : "Room Mesh");
    } else {
        app->room_query_retry_time = display_time + 2000000000LL;
        LOGI("Room Mesh query is not ready yet (%d); will retry.", (int)result);
    }
}

static bool initialize_room_mesh(QuestApp *app) {
    if (!app->room_mesh_supported && !app->triangle_mesh_supported) {
        LOGI("Meta Scene Mesh extensions are unavailable; scanner disabled.");
        return true;
    }
#define LOAD_ROOM_PROC(member, name) \
    xrGetInstanceProcAddr(app->instance, name, \
        (PFN_xrVoidFunction *)&app->member)
    LOAD_ROOM_PROC(query_spaces, "xrQuerySpacesFB");
    LOAD_ROOM_PROC(retrieve_space_query_results,
                   "xrRetrieveSpaceQueryResultsFB");
    LOAD_ROOM_PROC(enumerate_space_components,
                   "xrEnumerateSpaceSupportedComponentsFB");
    LOAD_ROOM_PROC(get_space_component_status,
                   "xrGetSpaceComponentStatusFB");
    LOAD_ROOM_PROC(set_space_component_status,
                   "xrSetSpaceComponentStatusFB");
    if (app->room_mesh_supported) {
        LOAD_ROOM_PROC(get_space_room_mesh, "xrGetSpaceRoomMeshMETA");
        LOAD_ROOM_PROC(get_room_mesh_face_indices,
                       "xrGetSpaceRoomMeshFaceIndicesMETA");
    }
    if (app->triangle_mesh_supported) {
        LOAD_ROOM_PROC(get_space_triangle_mesh,
                       "xrGetSpaceTriangleMeshMETA");
    }
#undef LOAD_ROOM_PROC
    const bool common_functions_ready = app->query_spaces != NULL &&
        app->retrieve_space_query_results != NULL &&
        app->enumerate_space_components != NULL &&
        app->get_space_component_status != NULL &&
        app->set_space_component_status != NULL;
    app->room_mesh_supported = common_functions_ready &&
        app->room_mesh_supported && app->get_space_room_mesh != NULL &&
        app->get_room_mesh_face_indices != NULL;
    app->triangle_mesh_supported = common_functions_ready &&
        app->triangle_mesh_supported && app->get_space_triangle_mesh != NULL;
    app->room_query_uses_triangle_mesh =
        !app->room_mesh_supported && app->triangle_mesh_supported;
    if (!app->room_mesh_supported && !app->triangle_mesh_supported) {
        LOGI("Meta Scene Mesh functions are incomplete; scanner disabled.");
        return true;
    }

    const char *vertex_source =
        "#version 300 es\n"
        "layout(location=0) in vec3 position;\n"
        "uniform mat4 projection; uniform vec3 eyePosition;\n"
        "uniform vec4 eyeRotation; uniform vec3 anchorPosition;\n"
        "uniform vec4 anchorRotation; out vec3 worldPosition;\n"
        "vec3 qr(vec4 q, vec3 v){return v+2.0*cross(q.xyz,cross(q.xyz,v)+q.w*v);}\n"
        "void main(){worldPosition=qr(anchorRotation,position)+anchorPosition;"
        "vec4 iq=vec4(-eyeRotation.xyz,eyeRotation.w);"
        "vec3 view=qr(iq,worldPosition-eyePosition);"
        "gl_Position=projection*vec4(view,1.0);}\n";
    const char *fragment_source =
        "#version 300 es\n"
        "precision highp float; in vec3 worldPosition; out vec4 color;\n"
        "uniform vec3 pulseCenter; uniform float pulseRadius; uniform float pulseOpacity;\n"
        "void main(){float d=abs(distance(worldPosition,pulseCenter)-pulseRadius);"
        "float band=1.0-smoothstep(0.22,0.52,d);"
        "float glow=1.0-smoothstep(0.52,1.05,d);"
        "float a=(band*0.95+glow*0.34)*pulseOpacity;"
        "color=vec4(vec3(1.0,0.48,0.015)*a,a);}\n";
    const GLuint vertex = compile_shader(GL_VERTEX_SHADER, vertex_source);
    const GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    if (vertex == 0 || fragment == 0) {
        if (vertex != 0) glDeleteShader(vertex);
        if (fragment != 0) glDeleteShader(fragment);
        app->room_mesh_supported = false;
        app->triangle_mesh_supported = false;
        return true;
    }
    app->room_pulse_program = glCreateProgram();
    glAttachShader(app->room_pulse_program, vertex);
    glAttachShader(app->room_pulse_program, fragment);
    glLinkProgram(app->room_pulse_program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    glGetProgramiv(app->room_pulse_program, GL_LINK_STATUS, &linked);
    if (!linked) {
        LOGI("Room Mesh scanner shader could not be linked.");
        glDeleteProgram(app->room_pulse_program);
        app->room_pulse_program = 0;
        app->room_mesh_supported = false;
        app->triangle_mesh_supported = false;
        return true;
    }
    app->room_pulse_projection =
        glGetUniformLocation(app->room_pulse_program, "projection");
    app->room_pulse_eye_position =
        glGetUniformLocation(app->room_pulse_program, "eyePosition");
    app->room_pulse_eye_rotation =
        glGetUniformLocation(app->room_pulse_program, "eyeRotation");
    app->room_pulse_anchor_position =
        glGetUniformLocation(app->room_pulse_program, "anchorPosition");
    app->room_pulse_anchor_rotation =
        glGetUniformLocation(app->room_pulse_program, "anchorRotation");
    app->room_pulse_center =
        glGetUniformLocation(app->room_pulse_program, "pulseCenter");
    app->room_pulse_radius =
        glGetUniformLocation(app->room_pulse_program, "pulseRadius");
    app->room_pulse_opacity =
        glGetUniformLocation(app->room_pulse_program, "pulseOpacity");
    app->room_observed_star_serial = quest_game_star_collection_serial();
    LOGI("Meta Scene scanner is ready (room=%d, triangle fallback=%d); awaiting spatial data.",
         app->room_mesh_supported, app->triangle_mesh_supported);
    return true;
}

static void start_room_pulse(
    QuestApp *app, XrTime display_time, bool manual_test) {
    float recentered[3];
    if (quest_game_tabletop_mario_tracking_position(recentered) &&
        quest_vr_bridge_recentered_to_local(
            recentered, app->room_pulse_center_position)) {
        /* Mario is the intended origin. */
    } else {
        app->room_pulse_center_position[0] =
            (app->views[0].pose.position.x +
             app->views[1].pose.position.x) * 0.5f;
        app->room_pulse_center_position[1] =
            (app->views[0].pose.position.y +
             app->views[1].pose.position.y) * 0.5f;
        app->room_pulse_center_position[2] =
            (app->views[0].pose.position.z +
             app->views[1].pose.position.z) * 0.5f;
    }
    app->room_pulse_start_time = display_time;
    if (manual_test) {
        /* The passthrough LUT flash acknowledges the button even if Scene
         * data was unavailable, separating input failure from mesh failure. */
        app->active_passthrough_effect = QUEST_PASSTHROUGH_EFFECT_STAR;
        app->passthrough_effect_start_time = display_time;
    }
    LOGI("%s Room Mesh pulse started at %.2f, %.2f, %.2f; %u room(s) loaded.",
         manual_test ? "Manual" : "Star",
         app->room_pulse_center_position[0],
         app->room_pulse_center_position[1],
         app->room_pulse_center_position[2],
         app->room_mesh_count);
}

static void update_room_pulse(QuestApp *app, XrTime display_time) {
    // The experimental Scene Mesh draw path is parked while its Quest crash is
    // investigated. Do not query or render room geometry from star collection
    // or the old two-stick diagnostic in otherwise stable Tabletop builds.
    if (!QUEST_ROOM_PULSE_ENABLED) return;

    request_room_mesh_query(app, display_time);
    if (quest_game_room_pulse_test_requested()) {
        start_room_pulse(app, display_time, true);
    }
    const uint32_t serial = quest_game_star_collection_serial();
    if (serial == app->room_observed_star_serial) return;
    app->room_observed_star_serial = serial;
    start_room_pulse(app, display_time, false);
}

static void build_projection_matrix(const XrFovf *fov, float out[16]) {
    memset(out, 0, sizeof(float) * 16);
    const float left = tanf(fov->angleLeft);
    const float right = tanf(fov->angleRight);
    const float down = tanf(fov->angleDown);
    const float up = tanf(fov->angleUp);
    const float near_z = 0.03f;
    const float far_z = 30.0f;
    out[0] = 2.0f / (right - left);
    out[5] = 2.0f / (up - down);
    out[8] = (right + left) / (right - left);
    out[9] = (up + down) / (up - down);
    out[10] = -(far_z + near_z) / (far_z - near_z);
    out[11] = -1.0f;
    out[14] = -(2.0f * far_z * near_z) / (far_z - near_z);
}

static void render_room_pulse(
    QuestApp *app, uint32_t eye, XrTime display_time) {
    if (app->room_pulse_program == 0 || app->room_mesh_count == 0 ||
        app->room_pulse_start_time == 0 ||
        display_time <= app->room_pulse_start_time) return;
    const XrDuration elapsed = display_time - app->room_pulse_start_time;
    if (elapsed >= QUEST_ROOM_PULSE_DURATION_NS) return;
    const float progress =
        (float)((double)elapsed / (double)QUEST_ROOM_PULSE_DURATION_NS);
    const float radius = 0.05f + progress * 12.0f;
    const float opacity =
        0.55f + sinf(progress * 3.14159265f) * 0.45f;
    float projection[16];
    build_projection_matrix(&app->views[eye].fov, projection);

    const GLboolean blend_was_enabled = glIsEnabled(GL_BLEND);
    const GLboolean depth_was_enabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean cull_was_enabled = glIsEnabled(GL_CULL_FACE);
    GLint old_program = 0;
    GLint old_vertex_array = 0;
    GLint old_blend_src_rgb = 0;
    GLint old_blend_dst_rgb = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &old_program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &old_vertex_array);
    glGetIntegerv(GL_BLEND_SRC_RGB, &old_blend_src_rgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &old_blend_dst_rgb);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(0);
    glUseProgram(app->room_pulse_program);
    glUniformMatrix4fv(app->room_pulse_projection, 1, GL_FALSE, projection);
    glUniform3f(app->room_pulse_eye_position,
        app->views[eye].pose.position.x, app->views[eye].pose.position.y,
        app->views[eye].pose.position.z);
    glUniform4f(app->room_pulse_eye_rotation,
        app->views[eye].pose.orientation.x,
        app->views[eye].pose.orientation.y,
        app->views[eye].pose.orientation.z,
        app->views[eye].pose.orientation.w);
    glUniform3fv(app->room_pulse_center, 1,
                app->room_pulse_center_position);
    glUniform1f(app->room_pulse_radius, radius);
    glUniform1f(app->room_pulse_opacity, opacity);

    for (uint32_t i = 0; i < app->room_mesh_count; ++i) {
        QuestRoomMesh *mesh = &app->room_meshes[i];
        XrSpaceLocation location = {.type = XR_TYPE_SPACE_LOCATION};
        const XrResult located = xrLocateSpace(
            mesh->space, app->local_space, display_time, &location);
        const XrSpaceLocationFlags needed =
            XR_SPACE_LOCATION_POSITION_VALID_BIT |
            XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        if (XR_FAILED(located) ||
            (location.locationFlags & needed) != needed) continue;
        glUniform3f(app->room_pulse_anchor_position,
            location.pose.position.x, location.pose.position.y,
            location.pose.position.z);
        glUniform4f(app->room_pulse_anchor_rotation,
            location.pose.orientation.x, location.pose.orientation.y,
            location.pose.orientation.z, location.pose.orientation.w);
        glBindBuffer(GL_ARRAY_BUFFER, mesh->vertex_buffer);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                              sizeof(XrVector3f), (const void *)0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->index_buffer);
        glDrawElements(GL_TRIANGLES, (GLsizei)mesh->index_count,
                       GL_UNSIGNED_INT, (const void *)0);
    }
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindVertexArray((GLuint)old_vertex_array);
    glUseProgram((GLuint)old_program);
    glBlendFunc((GLenum)old_blend_src_rgb, (GLenum)old_blend_dst_rgb);
    if (!blend_was_enabled) glDisable(GL_BLEND);
    if (depth_was_enabled) glEnable(GL_DEPTH_TEST);
    if (cull_was_enabled) glEnable(GL_CULL_FACE);
}

static bool render_eye(
    QuestApp *app, uint32_t eye, XrTime display_time) {
    QuestSwapchain *swapchain = &app->swapchains[eye];
    uint32_t image_index = 0;
    XrSwapchainImageAcquireInfo acquire_info = {
        .type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO,
    };
    if (!xr_check(
            "xrAcquireSwapchainImage",
            xrAcquireSwapchainImage(
                swapchain->handle, &acquire_info, &image_index))) {
        return false;
    }

    XrSwapchainImageWaitInfo wait_info = {
        .type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO,
        .timeout = XR_INFINITE_DURATION,
    };
    if (!xr_check(
            "xrWaitSwapchainImage",
            xrWaitSwapchainImage(swapchain->handle, &wait_info))) {
        return false;
    }

    if (image_index >= swapchain->image_count ||
        swapchain->framebuffers == NULL) return false;
    glBindFramebuffer(
        GL_FRAMEBUFFER, swapchain->framebuffers[image_index]);

    glViewport(0, 0, swapchain->width, swapchain->height);
    if (quest_game_is_ready() && quest_game_render_eye(
            eye, (uint32_t)swapchain->width, (uint32_t)swapchain->height)) {
        /* The real game rendered the eye. */
    } else if (app->rom.status == QUEST_ROM_VALID) {
        glClearColor(eye == 0 ? 0.02f : 0.03f,
                     eye == 0 ? 0.30f : 0.18f,
                     eye == 0 ? 0.08f : 0.32f, 1.0f);
    } else if (app->rom.status == QUEST_ROM_INVALID) {
        glClearColor(eye == 0 ? 0.38f : 0.24f, 0.01f, 0.01f, 1.0f);
    } else if (eye == 0) {
        glClearColor(0.02f, 0.10f, 0.32f, 1.0f);
    } else {
        glClearColor(0.22f, 0.03f, 0.32f, 1.0f);
    }
    if (!quest_game_is_ready()) {
        glClear(GL_COLOR_BUFFER_BIT);
    }
    if (!quest_game_is_ready() && app->mario_program != 0 && app->mario_texture != 0) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(app->mario_program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, app->mario_texture);
        glUniform1f(app->mario_eye_offset, eye == 0 ? 0.012f : -0.012f);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisable(GL_BLEND);
    }
    render_room_pulse(app, eye, display_time);
    // Adreno is tile based. Depth is cleared before the next eye and never
    // sampled or submitted to OpenXR, so prevent a needless depth-tile store.
    const GLenum discard_attachments[] = { GL_DEPTH_ATTACHMENT };
    glInvalidateFramebuffer(
        GL_FRAMEBUFFER, 1, discard_attachments);
    glFlush();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    XrSwapchainImageReleaseInfo release_info = {
        .type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO,
    };
    return xr_check(
        "xrReleaseSwapchainImage",
        xrReleaseSwapchainImage(swapchain->handle, &release_info));
}

static bool render_frame(QuestApp *app) {
    static XrTime fps_window_start = 0;
    static uint32_t fps_window_frames = 0;
    update_foveation_if_needed(app);
    update_display_refresh_rate_if_needed(app);
    if (!update_render_scale_if_needed(app)) {
        LOGE("Could not recreate eye swapchains for the requested render scale.");
        return false;
    }
    XrFrameWaitInfo wait_info = {.type = XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frame_state = {.type = XR_TYPE_FRAME_STATE};
    if (!xr_check(
            "xrWaitFrame", xrWaitFrame(app->session, &wait_info, &frame_state))) {
        return false;
    }

    XrFrameBeginInfo begin_info = {.type = XR_TYPE_FRAME_BEGIN_INFO};
    if (!xr_check("xrBeginFrame", xrBeginFrame(app->session, &begin_info))) {
        return false;
    }

    XrCompositionLayerProjectionView projection_views[QUEST_VIEW_COUNT] = {0};
    XrCompositionLayerProjection projection_layer = {
        .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
        .space = app->local_space,
        .viewCount = QUEST_VIEW_COUNT,
        .views = projection_views,
    };
    XrCompositionLayerPassthroughFB passthrough_layer = {
        .type = XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB,
        .flags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
        .space = XR_NULL_HANDLE,
        .layerHandle = app->passthrough_layer,
    };
    const XrCompositionLayerBaseHeader *layers[2] = {NULL, NULL};
    uint32_t layer_count = 0;

    if (frame_state.shouldRender) {
        if (fps_window_start == 0 ||
            frame_state.predictedDisplayTime <= fps_window_start) {
            fps_window_start = frame_state.predictedDisplayTime;
            fps_window_frames = 0;
        }
        fps_window_frames++;
        const XrDuration fps_elapsed =
            frame_state.predictedDisplayTime - fps_window_start;
        if (fps_elapsed >= 1000000000LL) {
            const uint32_t fps = (uint32_t)(
                ((int64_t)fps_window_frames * 1000000000LL +
                 (int64_t)fps_elapsed / 2) /
                (int64_t)fps_elapsed
            );
            djui_fps_display_update(fps);
            fps_window_start = frame_state.predictedDisplayTime;
            fps_window_frames = 0;
        }
        quest_game_tick();
        const bool passthrough_active = update_passthrough_state(app);
        if (passthrough_active) {
            update_passthrough_style(app, frame_state.predictedDisplayTime);
        }
        projection_layer.layerFlags = passthrough_active
            ? XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT : 0;
        XrViewLocateInfo locate_info = {
            .type = XR_TYPE_VIEW_LOCATE_INFO,
            .viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
            .displayTime = frame_state.predictedDisplayTime,
            .space = app->local_space,
        };
        XrViewState view_state = {.type = XR_TYPE_VIEW_STATE};
        uint32_t view_count = 0;
        if (!xr_check(
                "xrLocateViews",
                xrLocateViews(
                    app->session,
                    &locate_info,
                    &view_state,
                    QUEST_VIEW_COUNT,
                    &view_count,
                    app->views))) {
            return false;
        }

        const XrViewStateFlags required_flags =
            XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
        if (view_count == QUEST_VIEW_COUNT
            && (view_state.viewStateFlags & required_flags) == required_flags) {
            if (app->session_state == XR_SESSION_STATE_FOCUSED &&
                app->activity_resumed && app->window_ready) {
                quest_input_update(app->session, app->local_space,
                                   frame_state.predictedDisplayTime);
            } else {
                quest_input_suspend();
            }
            float positions[2][3];
            float rotations[2][4];
            float fovs[2][4];
            uint32_t widths[2], heights[2];
            for (uint32_t eye = 0; eye < QUEST_VIEW_COUNT; ++eye) {
                positions[eye][0] = app->views[eye].pose.position.x;
                positions[eye][1] = app->views[eye].pose.position.y;
                positions[eye][2] = app->views[eye].pose.position.z;
                rotations[eye][0] = app->views[eye].pose.orientation.x;
                rotations[eye][1] = app->views[eye].pose.orientation.y;
                rotations[eye][2] = app->views[eye].pose.orientation.z;
                rotations[eye][3] = app->views[eye].pose.orientation.w;
                fovs[eye][0] = app->views[eye].fov.angleLeft;
                fovs[eye][1] = app->views[eye].fov.angleRight;
                fovs[eye][2] = app->views[eye].fov.angleDown;
                fovs[eye][3] = app->views[eye].fov.angleUp;
                widths[eye] = (uint32_t)app->swapchains[eye].width;
                heights[eye] = (uint32_t)app->swapchains[eye].height;
            }
            quest_vr_bridge_update_views(positions, rotations, fovs, widths, heights);
            quest_game_prepare_vr_frame();
            update_room_pulse(app, frame_state.predictedDisplayTime);
            for (uint32_t eye = 0; eye < QUEST_VIEW_COUNT; ++eye) {
                if (!render_eye(
                        app, eye, frame_state.predictedDisplayTime)) {
                    return false;
                }
                projection_views[eye].type =
                    XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
                projection_views[eye].pose = app->views[eye].pose;
                projection_views[eye].fov = app->views[eye].fov;
                projection_views[eye].subImage.swapchain =
                    app->swapchains[eye].handle;
                projection_views[eye].subImage.imageRect.extent.width =
                    app->swapchains[eye].width;
                projection_views[eye].subImage.imageRect.extent.height =
                    app->swapchains[eye].height;
                projection_views[eye].subImage.imageArrayIndex = 0;
            }
            if (passthrough_active) {
                layers[0] =
                    (const XrCompositionLayerBaseHeader *)&passthrough_layer;
                layers[1] =
                    (const XrCompositionLayerBaseHeader *)&projection_layer;
                layer_count = 2;
            } else {
                layers[0] =
                    (const XrCompositionLayerBaseHeader *)&projection_layer;
                layer_count = 1;
            }
        }
    } else {
        quest_game_pump_network();
    }

    XrFrameEndInfo end_info = {
        .type = XR_TYPE_FRAME_END_INFO,
        .displayTime = frame_state.predictedDisplayTime,
        .environmentBlendMode = app->blend_mode,
        .layerCount = layer_count,
        .layers = layer_count > 0 ? layers : NULL,
    };
    return xr_check("xrEndFrame", xrEndFrame(app->session, &end_info));
}

static bool poll_openxr_events(QuestApp *app) {
    XrEventDataBuffer event = {.type = XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(app->instance, &event) == XR_SUCCESS) {
        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const XrEventDataSessionStateChanged *changed =
                (const XrEventDataSessionStateChanged *)&event;
            app->session_state = changed->state;
            if (changed->state != XR_SESSION_STATE_FOCUSED) {
                // System overlays, the Quest keyboard, sleep, and headset
                // removal all revoke input focus. Never call xrSyncActions
                // until FOCUSED returns: some Meta runtimes fault inside the
                // driver if action syncing races that transition.
                quest_input_suspend();
            }
            LOGI("OpenXR session state changed to %d.", (int)changed->state);

            if (changed->state == XR_SESSION_STATE_READY
                && !app->session_running) {
                XrSessionBeginInfo begin_info = {
                    .type = XR_TYPE_SESSION_BEGIN_INFO,
                    .primaryViewConfigurationType =
                        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                };
                if (!xr_check(
                        "xrBeginSession",
                        xrBeginSession(app->session, &begin_info))) {
                    return false;
                }
                app->session_running = true;
                LOGI("OpenXR session is running and submitting stereo frames.");
            } else if (changed->state == XR_SESSION_STATE_STOPPING
                       && app->session_running) {
                if (app->passthrough_active) {
                    app->pause_passthrough_layer(app->passthrough_layer);
                    app->pause_passthrough(app->passthrough);
                    app->passthrough_active = false;
                }
                if (!xr_check("xrEndSession", xrEndSession(app->session))) {
                    return false;
                }
                app->session_running = false;
            } else if (changed->state == XR_SESSION_STATE_EXITING
                       || changed->state == XR_SESSION_STATE_LOSS_PENDING) {
                app->exit_requested = true;
            }
        } else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            app->exit_requested = true;
        } else if ((app->room_mesh_supported ||
                    app->triangle_mesh_supported) &&
                   event.type ==
                       XR_TYPE_EVENT_DATA_SPACE_QUERY_RESULTS_AVAILABLE_FB) {
            const XrEventDataSpaceQueryResultsAvailableFB *available =
                (const XrEventDataSpaceQueryResultsAvailableFB *)&event;
            handle_room_query_results(app, available->requestId);
        } else if ((app->room_mesh_supported ||
                    app->triangle_mesh_supported) &&
                   event.type == XR_TYPE_EVENT_DATA_SPACE_QUERY_COMPLETE_FB) {
            const XrEventDataSpaceQueryCompleteFB *complete =
                (const XrEventDataSpaceQueryCompleteFB *)&event;
            app->room_query_pending = false;
            if (app->room_mesh_count == 0 &&
                !app->room_query_uses_triangle_mesh &&
                app->triangle_mesh_supported) {
                app->room_query_uses_triangle_mesh = true;
                app->room_query_complete = false;
                app->room_query_retry_time = 0;
                LOGI("Room Mesh yielded no geometry; switching to Triangle Scene Mesh.");
            } else {
                app->room_query_complete = true;
            }
            LOGI("Scene Mesh query completed with result %d and %u loaded mesh(es).",
                 (int)complete->result, app->room_mesh_count);
        } else if ((app->room_mesh_supported ||
                    app->triangle_mesh_supported) &&
                   event.type ==
                       XR_TYPE_EVENT_DATA_SPACE_SET_STATUS_COMPLETE_FB) {
            const XrEventDataSpaceSetStatusCompleteFB *complete =
                (const XrEventDataSpaceSetStatusCompleteFB *)&event;
            if (XR_SUCCEEDED(complete->result) &&
                complete->componentType ==
                    XR_SPACE_COMPONENT_TYPE_LOCATABLE_FB) {
                if (app->triangle_mesh_supported &&
                    room_space_component_enabled(
                        app, complete->space,
                        XR_SPACE_COMPONENT_TYPE_TRIANGLE_MESH_META)) {
                    (void)load_triangle_mesh(app, complete->space);
                } else if (app->room_mesh_supported &&
                           room_space_component_enabled(
                               app, complete->space,
                               XR_SPACE_COMPONENT_TYPE_ROOM_MESH_META)) {
                    (void)load_room_mesh(app, complete->space);
                }
            } else if (XR_FAILED(complete->result)) {
                xrDestroySpace(complete->space);
            }
        }

        event.type = XR_TYPE_EVENT_DATA_BUFFER;
        event.next = NULL;
    }
    return true;
}

static void destroy_quest_app(QuestApp *app) {
    quest_game_flush_persistent_state();
    quest_input_shutdown();
    for (uint32_t i = 0; i < app->room_mesh_count; ++i) {
        QuestRoomMesh *mesh = &app->room_meshes[i];
        if (mesh->vertex_buffer != 0)
            glDeleteBuffers(1, &mesh->vertex_buffer);
        if (mesh->index_buffer != 0)
            glDeleteBuffers(1, &mesh->index_buffer);
        if (mesh->space != XR_NULL_HANDLE)
            xrDestroySpace(mesh->space);
    }
    if (app->room_pulse_program != 0)
        glDeleteProgram(app->room_pulse_program);
    if (app->mario_texture != 0) glDeleteTextures(1, &app->mario_texture);
    if (app->mario_program != 0) glDeleteProgram(app->mario_program);
    destroy_swapchain_render_targets(app);
    if (app->destroy_foveation_profile != NULL) {
        for (size_t i = 0; i < 3; ++i) {
            if (app->foveation_profiles[i] != XR_NULL_HANDLE) {
                app->destroy_foveation_profile(app->foveation_profiles[i]);
                app->foveation_profiles[i] = XR_NULL_HANDLE;
            }
        }
    }
    if (app->local_space != XR_NULL_HANDLE) {
        xrDestroySpace(app->local_space);
    }
    if (app->passthrough_layer != XR_NULL_HANDLE
        && app->destroy_passthrough_layer != NULL) {
        app->destroy_passthrough_layer(app->passthrough_layer);
        app->passthrough_layer = XR_NULL_HANDLE;
    }
    if (app->destroy_passthrough_color_lut != NULL) {
        if (app->theme_color_lut != XR_NULL_HANDLE) {
            app->destroy_passthrough_color_lut(app->theme_color_lut);
            app->theme_color_lut = XR_NULL_HANDLE;
        }
        if (app->effect_color_lut != XR_NULL_HANDLE) {
            app->destroy_passthrough_color_lut(app->effect_color_lut);
            app->effect_color_lut = XR_NULL_HANDLE;
        }
    }
    if (app->passthrough != XR_NULL_HANDLE
        && app->destroy_passthrough != NULL) {
        app->destroy_passthrough(app->passthrough);
        app->passthrough = XR_NULL_HANDLE;
    }
    if (app->session != XR_NULL_HANDLE) {
        xrDestroySession(app->session);
    }
    if (app->instance != XR_NULL_HANDLE) {
        xrDestroyInstance(app->instance);
    }
    if (app->egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(
            app->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (app->egl_surface != EGL_NO_SURFACE) {
            eglDestroySurface(app->egl_display, app->egl_surface);
        }
        if (app->egl_context != EGL_NO_CONTEXT) {
            eglDestroyContext(app->egl_display, app->egl_context);
        }
        eglTerminate(app->egl_display);
    }
}

void android_main(struct android_app *android_app) {
    QuestApp app = {
        .android_app = android_app,
        .egl_display = EGL_NO_DISPLAY,
        .egl_context = EGL_NO_CONTEXT,
        .egl_surface = EGL_NO_SURFACE,
        .instance = XR_NULL_HANDLE,
        .system_id = XR_NULL_SYSTEM_ID,
        .session = XR_NULL_HANDLE,
        .local_space = XR_NULL_HANDLE,
        .session_state = XR_SESSION_STATE_UNKNOWN,
        .blend_mode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
    };

    android_app->userData = &app;
    android_app->onAppCmd = handle_app_command;
    sActiveQuestApp = &app;

    extern void quest_android_set_user_path(const char *path);
    quest_android_set_user_path(android_app->activity->externalDataPath);

    LOGI("Starting Android/Quest game integration test.");
    const bool android_ready = wait_for_android_ready(&app);
    if (android_ready) {
        app.rom = quest_rom_find_and_validate(android_app);
        if (app.rom.status == QUEST_ROM_VALID) {
            LOGI("Valid US SM64 ROM found at %s (SHA-1 %s).",
                 app.rom.path, app.rom.sha1);
        } else if (app.rom.status == QUEST_ROM_INVALID) {
            LOGE("ROM at %s is not the supported US ROM (SHA-1 %s).",
                 app.rom.path, app.rom.sha1);
        } else {
            LOGI("No ROM found. Copy baserom.us.z64 into %s.",
                 android_app->activity->externalDataPath);
        }
    }
    if (android_ready && app.rom.status == QUEST_ROM_VALID) {
        quest_game_load_early_config();
    }
    bool initialized = android_ready && create_egl_context(&app)
        && create_mario_preview(&app)
        && create_openxr_instance(&app)
        && create_openxr_session(&app)
        && quest_input_initialize(app.instance, app.session)
        && create_swapchains(&app);
    if (initialized && app.rom.status == QUEST_ROM_VALID
        && !quest_game_initialize()) {
        LOGE("Full game initialization failed; retaining ROM preview fallback.");
    }
    if (!initialized) {
        LOGE("Quest OpenXR smoke test initialization failed.");
    }

    while (!android_app->destroyRequested && !app.exit_requested) {
        int events = 0;
        struct android_poll_source *source = NULL;
        const bool render_was_active = initialized && app.session_running &&
            app.activity_resumed && app.window_ready;
        const int timeout = render_was_active ? 0 : 50;
        while (ALooper_pollOnce(timeout, NULL, &events, (void **)&source) >= 0) {
            if (source != NULL) {
                source->process(android_app, source);
            }
            if (android_app->destroyRequested) {
                break;
            }
            if (timeout != 0) {
                break;
            }
        }

        if (!initialized) {
            continue;
        }
        if (!poll_openxr_events(&app)) {
            break;
        }
        const bool can_render = app.session_running && app.activity_resumed &&
            app.window_ready;
        if (can_render && !render_frame(&app)) {
            break;
        } else if (!can_render) {
            quest_input_suspend();
            quest_game_pump_network();
        }
    }

    destroy_quest_app(&app);
    if (sActiveQuestApp == &app) sActiveQuestApp = NULL;
    LOGI("Android/Quest OpenXR smoke test stopped.");

    // NativeActivity normally leaves its Linux process cached after
    // android_main returns. The SM64 runtime and renderer use process-lifetime
    // static state, so reusing that cached process creates a dead black
    // session on the next launch. Both the in-game Quit command and a real
    // Android Activity destroy must end the process after OpenXR/EGL cleanup.
    fflush(NULL);
    _exit(EXIT_SUCCESS);
}
