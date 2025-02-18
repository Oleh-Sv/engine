// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/android/surface_texture_external_texture.h"

#include <GLES/glext.h>

#include <utility>

#include "flutter/display_list/effects/dl_color_source.h"
#include "third_party/skia/include/core/SkAlphaType.h"
#include "third_party/skia/include/core/SkColorSpace.h"
#include "third_party/skia/include/core/SkColorType.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/gpu/GrBackendSurface.h"
#include "third_party/skia/include/gpu/GrDirectContext.h"
#include "third_party/skia/include/gpu/ganesh/SkImageGanesh.h"
#include "third_party/skia/include/gpu/ganesh/gl/GrGLBackendSurface.h"
#include "third_party/skia/include/gpu/gl/GrGLTypes.h"

namespace flutter {

SurfaceTextureExternalTexture::SurfaceTextureExternalTexture(
    int64_t id,
    const fml::jni::ScopedJavaGlobalRef<jobject>& surface_texture,
    const std::shared_ptr<PlatformViewAndroidJNI>& jni_facade)
    : Texture(id),
      jni_facade_(jni_facade),
      surface_texture_(surface_texture),
      transform_(SkMatrix::I()) {}

SurfaceTextureExternalTexture::~SurfaceTextureExternalTexture() {}

void SurfaceTextureExternalTexture::OnGrContextCreated() {
  state_ = AttachmentState::kUninitialized;
}

void SurfaceTextureExternalTexture::MarkNewFrameAvailable() {
  new_frame_ready_ = true;
}

void SurfaceTextureExternalTexture::Paint(PaintContext& context,
                                          const SkRect& bounds,
                                          bool freeze,
                                          const DlImageSampling sampling) {
  if (state_ == AttachmentState::kDetached) {
    return;
  }
  const bool should_process_frame =
      (!freeze && new_frame_ready_) || dl_image_ == nullptr;
  if (should_process_frame) {
    ProcessFrame(context, bounds);
    new_frame_ready_ = false;
  }
  FML_CHECK(state_ == AttachmentState::kAttached);

  if (dl_image_) {
    context.canvas->DrawImageRect(
        dl_image_,                                     // image
        SkRect::Make(dl_image_->bounds()),             // source rect
        bounds,                                        // destination rect
        sampling,                                      // sampling
        context.paint,                                 // paint
        flutter::DlCanvas::SrcRectConstraint::kStrict  // enforce edges
    );
  } else {
    FML_LOG(WARNING) << "No DlImage available.";
  }
}

void SurfaceTextureExternalTexture::OnGrContextDestroyed() {
  if (state_ == AttachmentState::kAttached) {
    Detach();
  }
  state_ = AttachmentState::kDetached;
}

void SurfaceTextureExternalTexture::OnTextureUnregistered() {}

void SurfaceTextureExternalTexture::Detach() {
  jni_facade_->SurfaceTextureDetachFromGLContext(
      fml::jni::ScopedJavaLocalRef<jobject>(surface_texture_));
  dl_image_.reset();
}

void SurfaceTextureExternalTexture::Attach(int gl_tex_id) {
  jni_facade_->SurfaceTextureAttachToGLContext(
      fml::jni::ScopedJavaLocalRef<jobject>(surface_texture_), gl_tex_id);
  state_ = AttachmentState::kAttached;
}

void SurfaceTextureExternalTexture::Update() {
  jni_facade_->SurfaceTextureUpdateTexImage(
      fml::jni::ScopedJavaLocalRef<jobject>(surface_texture_));

  jni_facade_->SurfaceTextureGetTransformMatrix(
      fml::jni::ScopedJavaLocalRef<jobject>(surface_texture_), transform_);

  // Android's SurfaceTexture transform matrix works on texture coordinate
  // lookups in the range 0.0-1.0, while Skia's Shader transform matrix works on
  // the image itself, as if it were inscribed inside a clip rect.
  // An Android transform that scales lookup by 0.5 (displaying 50% of the
  // texture) is the same as a Skia transform by 2.0 (scaling 50% of the image
  // outside of the virtual "clip rect"), so we invert the incoming matrix.
  SkMatrix inverted;
  if (!transform_.invert(&inverted)) {
    FML_LOG(FATAL)
        << "Invalid (not invertable) SurfaceTexture transformation matrix";
  }
  transform_ = inverted;
}

}  // namespace flutter
