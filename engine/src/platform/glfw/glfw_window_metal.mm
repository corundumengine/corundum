#include "glfw_window_metal.h"

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

void *metal_setup_layer(GLFWwindow *win) {
  NSWindow *nswin = glfwGetCocoaWindow(win);

  CAMetalLayer *layer = [CAMetalLayer layer];
  layer.device = MTLCreateSystemDefaultDevice();
  layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
  // Match the display's backing scale so drawableSize = logical_size * scale
  // (i.e. physical framebuffer pixels). Without this, contentsScale defaults
  // to 1.0 on Retina and the drawable is half the expected size, causing the
  // "only top-left visible" zoom artefact.
  layer.contentsScale = nswin.backingScaleFactor;

  nswin.contentView.layer = layer;
  nswin.contentView.wantsLayer = YES;

  CFRetain((__bridge CFTypeRef)layer);
  return (__bridge void *)layer;
}

const void *metal_device(void *layer_ptr) {
  CAMetalLayer *layer = (__bridge CAMetalLayer *)layer_ptr;
  return (__bridge const void *)layer.device;
}

const void *metal_next_drawable(void *layer_ptr) {
  CAMetalLayer *layer = (__bridge CAMetalLayer *)layer_ptr;
  id<CAMetalDrawable> drawable = [layer nextDrawable];
  return (__bridge const void *)drawable;
}

void *metal_get_layer(GLFWwindow *win) {
  NSWindow *nswin = glfwGetCocoaWindow(win);
  return (__bridge void *)nswin.contentView.layer;
}

void metal_set_display_sync(void *layer_ptr, int enabled) {
  CAMetalLayer *layer = (__bridge CAMetalLayer *)layer_ptr;
  layer.displaySyncEnabled = enabled ? YES : NO;
}
