#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>
#include <android/native_activity.h>
#include <android/log.h>
#include <android/native_window.h>
#include <jni.h>
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define TAG "NativeExample"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

typedef struct {
  ANativeActivity     *activity;
  ANativeWindow       *window;
  bool                 running;

  WGPUInstance         instance;
  WGPUSurface          surface;
  WGPUAdapter          adapter;
  WGPUDevice           device;
  WGPUQueue            queue;
  WGPURenderPipeline   pipeline;
  WGPUSurfaceConfiguration config;
} AppState;

// ─── wgpu callbacks ────────────────────────────────────────────────────────

static void on_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                       WGPUStringView msg, void *ud1, void *ud2) {
  (void)ud2;
  AppState *app = ud1;
  if (status == WGPURequestAdapterStatus_Success)
    app->adapter = adapter;
  else
    LOGE("request_adapter failed: %.*s", (int)msg.length, msg.data);
}

static void on_device(WGPURequestDeviceStatus status, WGPUDevice device,
                      WGPUStringView msg, void *ud1, void *ud2) {
  (void)ud2;
  AppState *app = ud1;
  if (status == WGPURequestDeviceStatus_Success)
    app->device = device;
  else
    LOGE("request_device failed: %.*s", (int)msg.length, msg.data);
}

static void on_wgpu_log(WGPULogLevel level, WGPUStringView msg, void *ud) {
  (void)ud;
  int prio = (level == WGPULogLevel_Error) ? ANDROID_LOG_ERROR : ANDROID_LOG_WARN;
  __android_log_print(prio, TAG, "%.*s", (int)msg.length, msg.data);
}

// ─── init / teardown ───────────────────────────────────────────────────────

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

static void wgpu_init(AppState *app) {
  wgpuSetLogCallback(on_wgpu_log, NULL);
  wgpuSetLogLevel(WGPULogLevel_Warn);

  app->instance = wgpuCreateInstance(NULL);
  assert(app->instance);

  // surface from ANativeWindow
  app->surface = wgpuInstanceCreateSurface(
    app->instance,
    &(WGPUSurfaceDescriptor){
      .nextInChain = (WGPUChainedStruct *)&(WGPUSurfaceSourceAndroidNativeWindow){
        .chain = { .sType = WGPUSType_SurfaceSourceAndroidNativeWindow },
        .window = app->window,
      },
    }
  );
  assert(app->surface);

  wgpuInstanceRequestAdapter(
    app->instance,
    &(WGPURequestAdapterOptions){ .compatibleSurface = app->surface },
    (WGPURequestAdapterCallbackInfo){ .callback = on_adapter, .userdata1 = app }
  );
  assert(app->adapter);

  wgpuAdapterRequestDevice(
    app->adapter, NULL,
    (WGPURequestDeviceCallbackInfo){ .callback = on_device, .userdata1 = app }
  );
  assert(app->device);

  app->queue = wgpuDeviceGetQueue(app->device);
  assert(app->queue);

  // shader
  WGPUShaderModule shader = wgpuDeviceCreateShaderModule(
    app->device,
    &(WGPUShaderModuleDescriptor){
      .nextInChain = (WGPUChainedStruct *)&(WGPUShaderSourceWGSL){
        .chain = { .sType = WGPUSType_ShaderSourceWGSL },
        .code  = { SHADER_WGSL, WGPU_STRLEN },
      },
    }
  );
  assert(shader);

  WGPUSurfaceCapabilities caps = {0};
  wgpuSurfaceGetCapabilities(app->surface, app->adapter, &caps);
  WGPUTextureFormat fmt = caps.formats[0];

  WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(
    app->device,
    &(WGPUPipelineLayoutDescriptor){ .label = {"pipeline_layout", WGPU_STRLEN} }
  );

  app->pipeline = wgpuDeviceCreateRenderPipeline(
    app->device,
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
  assert(app->pipeline);

  int32_t w = ANativeWindow_getWidth(app->window);
  int32_t h = ANativeWindow_getHeight(app->window);

  app->config = (WGPUSurfaceConfiguration){
    .device      = app->device,
    .usage       = WGPUTextureUsage_RenderAttachment,
    .format      = fmt,
    .presentMode = WGPUPresentMode_Fifo,
    .alphaMode   = caps.alphaModes[0],
    .width       = (uint32_t)w,
    .height      = (uint32_t)h,
  };
  wgpuSurfaceConfigure(app->surface, &app->config);

  wgpuShaderModuleRelease(shader);
  wgpuPipelineLayoutRelease(layout);
  wgpuSurfaceCapabilitiesFreeMembers(caps);

  LOGI("wgpu init complete (%dx%d)", w, h);
}

static void wgpu_frame(AppState *app) {
  WGPUSurfaceTexture st;
  wgpuSurfaceGetCurrentTexture(app->surface, &st);

  switch (st.status) {
    case WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal:
    case WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal:
      break;
    case WGPUSurfaceGetCurrentTextureStatus_Timeout:
    case WGPUSurfaceGetCurrentTextureStatus_Outdated:
    case WGPUSurfaceGetCurrentTextureStatus_Lost:
      if (st.texture) wgpuTextureRelease(st.texture);
      wgpuSurfaceConfigure(app->surface, &app->config);
      return;
    default:
      LOGE("fatal surface status %d", st.status);
      abort();
  }

  WGPUTextureView frame = wgpuTextureCreateView(st.texture, NULL);

  WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(
    app->device,
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

  wgpuRenderPassEncoderSetPipeline(rp, app->pipeline);
  wgpuRenderPassEncoderDraw(rp, 3, 1, 0, 0);
  wgpuRenderPassEncoderEnd(rp);
  wgpuRenderPassEncoderRelease(rp);

  WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(
    enc, &(WGPUCommandBufferDescriptor){ .label = {"cmd", WGPU_STRLEN} }
  );
  wgpuQueueSubmit(app->queue, 1, &cmd);
  wgpuSurfacePresent(app->surface);

  wgpuCommandBufferRelease(cmd);
  wgpuCommandEncoderRelease(enc);
  wgpuTextureViewRelease(frame);
  wgpuTextureRelease(st.texture);
}

static void wgpu_destroy(AppState *app) {
  wgpuRenderPipelineRelease(app->pipeline);
  wgpuQueueRelease(app->queue);
  wgpuDeviceRelease(app->device);
  wgpuAdapterRelease(app->adapter);
  wgpuSurfaceRelease(app->surface);
  wgpuInstanceRelease(app->instance);
}

// ─── NativeActivity callbacks ───────────────────────────────────────────────

static void on_window_init(ANativeActivity *activity, ANativeWindow *window) {
  AppState *app  = activity->instance;
  app->window    = window;
  app->running   = true;
  wgpu_init(app);
}

static void on_window_term(ANativeActivity *activity, ANativeWindow *window) {
  (void)window;
  AppState *app = activity->instance;
  app->running  = false;
  wgpu_destroy(app);
  app->window = NULL;
}

static void on_destroy(ANativeActivity *activity) {
  AppState *app = activity->instance;
  free(app);
}

// main loop runs on the app thread via looper — simplest approach is to
// spin in onWindowFocusChanged or pump from a dedicated thread; here we
// just render one frame per input-queue flush by overriding onInputQueueCreated
// to NULL and driving from the main thread via ANativeActivity_finish on idle.
// For a proper game loop, spin a pthread here.
static void on_idle(ANativeActivity *activity, ANativeWindow *window) {
  (void)window;
  AppState *app = activity->instance;
  if (app->running && app->window)
    wgpu_frame(app);
}

JNIEXPORT void JNICALL
Java_com_example_NativeExample_MainActivity_nativeSetSafeArea(
    JNIEnv *env, jobject obj, jint top, jint bottom, jint left, jint right) {
  (void)env; (void)obj;
  LOGI("[SafeArea] Safe area: %d %d %d %d", top, bottom, left, right);
}

void ANativeActivity_onCreate(ANativeActivity *activity,
                               void *savedState, size_t savedStateSize) {
  (void)savedState; (void)savedStateSize;

  AppState *app = calloc(1, sizeof(AppState));
  app->activity = activity;
  activity->instance = app;

  activity->callbacks->onNativeWindowCreated    = on_window_init;
  activity->callbacks->onNativeWindowDestroyed  = on_window_term;
  activity->callbacks->onDestroy                = on_destroy;

  // NativeActivity doesn't have an explicit idle hook; drive the render
  // loop from onNativeWindowRedrawNeeded which fires on vsync/damage.
  activity->callbacks->onNativeWindowRedrawNeeded = on_idle;
}