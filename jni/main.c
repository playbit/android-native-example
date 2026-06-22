#include <jni.h>

#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>

#include <android/log.h>
#include <android/asset_manager.h>
#include <android_native_app_glue.h>

#include <string.h>
#include <assert.h>

#define LOG(...) ((void)__android_log_print(ANDROID_LOG_INFO, "NativeExample", __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "NativeExample", __VA_ARGS__))

struct engine
{
    struct android_app *app;

    int active;

    WGPUInstance           instance;
    WGPUSurface            surface;
    WGPUAdapter            adapter;
    WGPUDevice             device;
    WGPUQueue              queue;
    WGPURenderPipeline     pipeline;
    WGPUSurfaceConfiguration config;
};

// ─── wgpu callbacks ────────────────────────────────────────────────────────

static void on_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                       WGPUStringView msg, void *ud1, void *ud2) {
    (void)ud2;
    struct engine *engine = ud1;
    if (status == WGPURequestAdapterStatus_Success)
        engine->adapter = adapter;
    else
        LOGE("request_adapter failed: %.*s", (int)msg.length, msg.data);
}

static void on_device(WGPURequestDeviceStatus status, WGPUDevice device,
                      WGPUStringView msg, void *ud1, void *ud2) {
    (void)ud2;
    struct engine *engine = ud1;
    if (status == WGPURequestDeviceStatus_Success)
        engine->device = device;
    else
        LOGE("request_device failed: %.*s", (int)msg.length, msg.data);
}

static void on_wgpu_log(WGPULogLevel level, WGPUStringView msg, void *ud) {
    (void)ud;
    int prio = (level == WGPULogLevel_Error) ? ANDROID_LOG_ERROR : ANDROID_LOG_WARN;
    __android_log_print(prio, "NativeExample", "%.*s", (int)msg.length, msg.data);
}

// ─── wgsl ──────────────────────────────────────────────────────────────────

static const char *SHADER_WGSL =
    "@vertex\n"
    "fn vs_main(@builtin(vertex_index) vi: u32) -> @builtin(position) vec4f {\n"
    "  var pos = array<vec2f, 3>(\n"
    "    vec2f( 0.0,  0.5),\n"
    "    vec2f(-0.5, -0.5),\n"
    "    vec2f( 0.5, -0.5)\n"
    "  );\n"
    "  return vec4f(pos[vi], 0.0, 1.0);\n"
    "}\n"
    "@fragment\n"
    "fn fs_main() -> @location(0) vec4f {\n"
    "  return vec4f(1.0, 0.5, 0.0, 1.0);\n"
    "}\n";

// ─── init / draw / term ────────────────────────────────────────────────────

static int engine_init_display(struct engine *engine)
{
    wgpuSetLogCallback(on_wgpu_log, NULL);
    wgpuSetLogLevel(WGPULogLevel_Warn);

    engine->instance = wgpuCreateInstance(NULL);
    assert(engine->instance);

    engine->surface = wgpuInstanceCreateSurface(
        engine->instance,
        &(WGPUSurfaceDescriptor){
            .nextInChain = (WGPUChainedStruct *)&(WGPUSurfaceSourceAndroidNativeWindow){
                .chain  = { .sType = WGPUSType_SurfaceSourceAndroidNativeWindow },
                .window = engine->app->window,
            },
        }
    );
    assert(engine->surface);

    wgpuInstanceRequestAdapter(
        engine->instance,
        &(WGPURequestAdapterOptions){ .compatibleSurface = engine->surface },
        (WGPURequestAdapterCallbackInfo){ .callback = on_adapter, .userdata1 = engine }
    );
    assert(engine->adapter);

    wgpuAdapterRequestDevice(
        engine->adapter, NULL,
        (WGPURequestDeviceCallbackInfo){ .callback = on_device, .userdata1 = engine }
    );
    assert(engine->device);

    engine->queue = wgpuDeviceGetQueue(engine->device);
    assert(engine->queue);

    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(
        engine->device,
        &(WGPUShaderModuleDescriptor){
            .nextInChain = (WGPUChainedStruct *)&(WGPUShaderSourceWGSL){
                .chain = { .sType = WGPUSType_ShaderSourceWGSL },
                .code  = { SHADER_WGSL, WGPU_STRLEN },
            },
        }
    );
    assert(shader);

    WGPUSurfaceCapabilities caps = {0};
    wgpuSurfaceGetCapabilities(engine->surface, engine->adapter, &caps);
    WGPUTextureFormat fmt = caps.formats[0];

    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(
        engine->device,
        &(WGPUPipelineLayoutDescriptor){ .label = {"pipeline_layout", WGPU_STRLEN} }
    );

    engine->pipeline = wgpuDeviceCreateRenderPipeline(
        engine->device,
        &(WGPURenderPipelineDescriptor){
            .label  = {"triangle", WGPU_STRLEN},
            .layout = layout,
            .vertex = {
                .module     = shader,
                .entryPoint = {"vs_main", WGPU_STRLEN},
            },
            .fragment = &(WGPUFragmentState){
                .module      = shader,
                .entryPoint  = {"fs_main", WGPU_STRLEN},
                .targetCount = 1,
                .targets     = &(WGPUColorTargetState){
                    .format    = fmt,
                    .writeMask = WGPUColorWriteMask_All,
                },
            },
            .primitive   = { .topology = WGPUPrimitiveTopology_TriangleList },
            .multisample = { .count = 1, .mask = 0xFFFFFFFF },
        }
    );
    assert(engine->pipeline);

    int32_t w = ANativeWindow_getWidth(engine->app->window);
    int32_t h = ANativeWindow_getHeight(engine->app->window);

    engine->config = (WGPUSurfaceConfiguration){
        .device      = engine->device,
        .usage       = WGPUTextureUsage_RenderAttachment,
        .format      = fmt,
        .presentMode = WGPUPresentMode_Fifo,
        .alphaMode   = caps.alphaModes[0],
        .width       = (uint32_t)w,
        .height      = (uint32_t)h,
    };
    wgpuSurfaceConfigure(engine->surface, &engine->config);

    wgpuShaderModuleRelease(shader);
    wgpuPipelineLayoutRelease(layout);
    wgpuSurfaceCapabilitiesFreeMembers(caps);

    LOG("wgpu init complete (%dx%d)", w, h);
    return 0;
}

static void engine_draw_frame(struct engine *engine)
{
    if (!engine->device) return;

    WGPUSurfaceTexture st;
    wgpuSurfaceGetCurrentTexture(engine->surface, &st);

    switch (st.status) {
        case WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal:
        case WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal:
            break;
        default:
            if (st.texture) wgpuTextureRelease(st.texture);
            wgpuSurfaceConfigure(engine->surface, &engine->config);
            return;
    }

    WGPUTextureView frame = wgpuTextureCreateView(st.texture, NULL);

    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(
        engine->device,
        &(WGPUCommandEncoderDescriptor){ .label = {"enc", WGPU_STRLEN} }
    );

    WGPURenderPassEncoder rp = wgpuCommandEncoderBeginRenderPass(
        enc,
        &(WGPURenderPassDescriptor){
            .colorAttachmentCount = 1,
            .colorAttachments = &(WGPURenderPassColorAttachment){
                .view       = frame,
                .loadOp     = WGPULoadOp_Clear,
                .storeOp    = WGPUStoreOp_Store,
                .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
                .clearValue = { .r=0.1, .g=0.1, .b=0.1, .a=1.0 },
            },
        }
    );

    wgpuRenderPassEncoderSetPipeline(rp, engine->pipeline);
    wgpuRenderPassEncoderDraw(rp, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(rp);
    wgpuRenderPassEncoderRelease(rp);

    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(
        enc, &(WGPUCommandBufferDescriptor){ .label = {"cmd", WGPU_STRLEN} }
    );
    wgpuQueueSubmit(engine->queue, 1, &cmd);
    wgpuSurfacePresent(engine->surface);

    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(enc);
    wgpuTextureViewRelease(frame);
    wgpuTextureRelease(st.texture);
}

static void engine_term_display(struct engine *engine)
{
    if (!engine->device) return;

    wgpuRenderPipelineRelease(engine->pipeline);
    wgpuQueueRelease(engine->queue);
    wgpuDeviceRelease(engine->device);
    wgpuAdapterRelease(engine->adapter);
    wgpuSurfaceRelease(engine->surface);
    wgpuInstanceRelease(engine->instance);

    engine->pipeline = NULL;
    engine->queue    = NULL;
    engine->device   = NULL;
    engine->adapter  = NULL;
    engine->surface  = NULL;
    engine->instance = NULL;
    engine->active   = 0;
}

// ─── app glue callbacks ────────────────────────────────────────────────────

static int32_t engine_handle_input(struct android_app *app, AInputEvent *event)
{
    return 0;
}

static void engine_handle_cmd(struct android_app *app, int32_t cmd)
{
    struct engine *engine = (struct engine *)app->userData;
    switch (cmd)
    {
        case APP_CMD_INIT_WINDOW:
        {
            if (engine->app->window != NULL)
            {
                engine_init_display(engine);
                engine->active = 1;
            }
        } break;

        case APP_CMD_TERM_WINDOW:
        {
            engine_term_display(engine);
        } break;

        case APP_CMD_GAINED_FOCUS:
        {
            engine->active = 1;
        } break;

        case APP_CMD_LOST_FOCUS:
        {
            engine->active = 0;
        } break;
    }
}

// ─── JNI ──────────────────────────────────────────────────────────────────

static int safe_top = 0;
static int safe_bottom = 0;
static int safe_left = 0;
static int safe_right = 0;

JNIEXPORT void JNICALL
Java_com_example_NativeExample_MainActivity_nativeSetSafeArea(JNIEnv *env, jobject this,
                                                              jint top, jint bottom,
                                                              jint left, jint right) {
    safe_top    = top;
    safe_bottom = bottom;
    safe_left   = left;
    safe_right  = right;

    LOG("[SafeArea] Safe area: %d %d %d %d", top, bottom, left, right);
}

void java__vibrate(ANativeActivity *activity) {
    JNIEnv *env = NULL;
    JavaVM *vm = activity->vm;

    (*vm)->AttachCurrentThread(vm, &env, NULL);

    jclass clazz = (*env)->GetObjectClass(env, activity->clazz);
    jmethodID methodID = (*env)->GetMethodID(env, clazz, "vibrate", "()V");
    (*env)->CallVoidMethod(env, activity->clazz, methodID);
    (*env)->DeleteLocalRef(env, clazz);

    (*vm)->DetachCurrentThread(vm);
}

// ─── main ─────────────────────────────────────────────────────────────────

void android_main(struct android_app *app)
{
    LOG("---");

    struct engine engine;
    memset(&engine, 0, sizeof(engine));

    app->userData    = &engine;
    app->onAppCmd    = engine_handle_cmd;
    app->onInputEvent = engine_handle_input;
    engine.app       = app;

    java__vibrate(app->activity);

    while (1)
    {
        int events;
        struct android_poll_source *source;

        while (ALooper_pollAll(engine.active ? 0 : -1, NULL, &events, (void **)&source) >= 0)
        {
            if (source != NULL)
                source->process(app, source);

            if (app->destroyRequested != 0)
            {
                engine_term_display(&engine);
                return;
            }
        }

        if (engine.active)
            engine_draw_frame(&engine);
    }
}
