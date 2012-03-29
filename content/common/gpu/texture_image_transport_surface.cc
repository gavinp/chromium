// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/gpu/texture_image_transport_surface.h"

#include "content/common/gpu/gpu_channel.h"
#include "content/common/gpu/gpu_channel_manager.h"
#include "content/common/gpu/gpu_messages.h"
#include "gpu/command_buffer/service/context_group.h"
#include "gpu/command_buffer/service/gpu_scheduler.h"
#include "gpu/command_buffer/service/texture_manager.h"
#include "ui/gfx/gl/scoped_make_current.h"

using gpu::gles2::ContextGroup;
using gpu::gles2::TextureManager;
typedef TextureManager::TextureInfo TextureInfo;

namespace {

class ScopedFrameBufferBinder {
 public:
  ScopedFrameBufferBinder(unsigned int fbo) {
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &old_fbo_);
    glBindFramebufferEXT(GL_FRAMEBUFFER, fbo);
  }

  ~ScopedFrameBufferBinder() {
    glBindFramebufferEXT(GL_FRAMEBUFFER, old_fbo_);
  }

 private:
  int old_fbo_;
};

class ScopedTextureBinder {
 public:
  ScopedTextureBinder(unsigned int id) {
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_id_);
    glBindTexture(GL_TEXTURE_2D, id);
  }

  ~ScopedTextureBinder() {
    glBindTexture(GL_TEXTURE_2D, old_id_);
  }

 private:
  int old_id_;
};

}  // anonymous namespace

TextureImageTransportSurface::TextureImageTransportSurface(
    GpuChannelManager* manager,
    GpuCommandBufferStub* stub,
    const gfx::GLSurfaceHandle& handle)
      : fbo_id_(0),
        front_(0),
        stub_destroyed_(false) {
  GpuChannel* parent_channel = manager->LookupChannel(handle.parent_client_id);
  DCHECK(parent_channel);
  GpuCommandBufferStub* parent_stub = parent_channel->LookupCommandBuffer(
      handle.parent_context_id);
  DCHECK(parent_stub);
  parent_stub_ = parent_stub->AsWeakPtr();
  TextureManager* texture_manager =
      parent_stub_->decoder()->GetContextGroup()->texture_manager();
  DCHECK(texture_manager);

  for (int i = 0; i < 2; ++i) {
    Texture& texture = textures_[i];
    texture.client_id = handle.parent_texture_id[i];
    TextureInfo* info = texture_manager->GetTextureInfo(texture.client_id);
    DCHECK(info);
    if (!info->target())
      texture_manager->SetInfoTarget(info, GL_TEXTURE_2D);
    texture_manager->SetParameter(info, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    texture_manager->SetParameter(info, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    texture_manager->SetParameter(info, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    texture_manager->SetParameter(info, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  helper_.reset(new ImageTransportHelper(this,
                                         manager,
                                         stub,
                                         gfx::kNullPluginWindow));

  stub->AddDestructionObserver(this);
}

TextureImageTransportSurface::~TextureImageTransportSurface() {
  DCHECK(stub_destroyed_);
  Destroy();
}

bool TextureImageTransportSurface::Initialize() {
  return helper_->Initialize();
}

void TextureImageTransportSurface::Destroy() {
  for (int i = 0; i < 2; ++i) {
    Texture& texture = textures_[i];
    if (!texture.sent_to_client)
      continue;
    GpuHostMsg_AcceleratedSurfaceRelease_Params params;
    params.identifier = texture.client_id;
    helper_->SendAcceleratedSurfaceRelease(params);
  }

  helper_->Destroy();
}

bool TextureImageTransportSurface::Resize(const gfx::Size&) {
  return true;
}

bool TextureImageTransportSurface::IsOffscreen() {
  return false;
}

bool TextureImageTransportSurface::OnMakeCurrent(gfx::GLContext* context) {
  if (stub_destroyed_) {
    // Early-exit so that we don't recreate the fbo. We still want to return
    // true, so that the context is made current and the GLES2DecoderImpl can
    // release its own resources.
    return true;
  }

  if (!fbo_id_) {
    glGenFramebuffersEXT(1, &fbo_id_);
    glBindFramebufferEXT(GL_FRAMEBUFFER, fbo_id_);
    CreateBackTexture(gfx::Size(1, 1));

#ifndef NDEBUG
    GLenum status = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
      DLOG(ERROR) << "Framebuffer incomplete.";
      return false;
    }
#endif
  }

  return true;
}

unsigned int TextureImageTransportSurface::GetBackingFrameBufferObject() {
  return fbo_id_;
}

void TextureImageTransportSurface::SetBufferAllocation(
    BufferAllocationState state) {
  if (!helper_->MakeCurrent())
    return;
  switch (state) {
    case BUFFER_ALLOCATION_FRONT_AND_BACK:
      CreateBackTexture(textures_[back()].size);
      break;
    case BUFFER_ALLOCATION_FRONT_ONLY:
    case BUFFER_ALLOCATION_NONE:
      ReleaseBackTexture();
      break;
  };
}

void* TextureImageTransportSurface::GetShareHandle() {
  return GetHandle();
}

void* TextureImageTransportSurface::GetDisplay() {
  return parent_stub_.get() ? parent_stub_->surface()->GetDisplay() : NULL;
}

void* TextureImageTransportSurface::GetConfig() {
  return parent_stub_.get() ? parent_stub_->surface()->GetConfig() : NULL;
}

void TextureImageTransportSurface::OnResize(gfx::Size size) {
  CreateBackTexture(size);
}

void TextureImageTransportSurface::OnWillDestroyStub(
    GpuCommandBufferStub* stub) {
  glDeleteFramebuffersEXT(1, &fbo_id_);
  CHECK_GL_ERROR();
  fbo_id_ = 0;

  stub->RemoveDestructionObserver(this);
  stub_destroyed_ = true;
}

bool TextureImageTransportSurface::SwapBuffers() {
  glFlush();
  front_ = back();
  previous_damage_rect_ = gfx::Rect(textures_[front_].size);

  DCHECK(textures_[front_].client_id != 0);

  GpuHostMsg_AcceleratedSurfaceBuffersSwapped_Params params;
  params.surface_handle = textures_[front_].client_id;
  helper_->SendAcceleratedSurfaceBuffersSwapped(params);
  helper_->SetScheduled(false);
  return true;
}

bool TextureImageTransportSurface::PostSubBuffer(
    int x, int y, int width, int height) {
  if (!parent_stub_.get())
    return false;

  TextureInfo* info = GetParentInfo(textures_[back()].client_id);
  if (!info)
    return false;
  int back_texture_service_id = info->service_id();

  info = GetParentInfo(textures_[front_].client_id);
  if (!info)
    return false;
  int front_texture_service_id = info->service_id();

  gfx::Size expected_size = textures_[back()].size;
  bool surfaces_same_size = textures_[front_].size == expected_size;

  const gfx::Rect new_damage_rect(x, y, width, height);

  // An empty damage rect is a successful no-op.
  if (new_damage_rect.IsEmpty())
    return true;

  if (surfaces_same_size) {
    std::vector<gfx::Rect> regions_to_copy;
    GetRegionsToCopy(previous_damage_rect_, new_damage_rect, &regions_to_copy);

    ScopedFrameBufferBinder fbo_binder(fbo_id_);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        front_texture_service_id,
        0);
    ScopedTextureBinder texture_binder(back_texture_service_id);

    for (size_t i = 0; i < regions_to_copy.size(); ++i) {
      const gfx::Rect& region_to_copy = regions_to_copy[i];
      if (!region_to_copy.IsEmpty()) {
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, region_to_copy.x(),
            region_to_copy.y(), region_to_copy.x(), region_to_copy.y(),
            region_to_copy.width(), region_to_copy.height());
      }
    }
  } else {
    DCHECK(new_damage_rect == gfx::Rect(expected_size));
  }

  glFlush();
  front_ = back();

  GpuHostMsg_AcceleratedSurfacePostSubBuffer_Params params;
  params.surface_handle = textures_[front_].client_id;
  params.x = x;
  params.y = y;
  params.width = width;
  params.height = height;
  helper_->SendAcceleratedSurfacePostSubBuffer(params);
  helper_->SetScheduled(false);

  previous_damage_rect_ = new_damage_rect;
  return true;
}

std::string TextureImageTransportSurface::GetExtensions() {
  std::string extensions = gfx::GLSurface::GetExtensions();
  extensions += extensions.empty() ? "" : " ";
  extensions += "GL_CHROMIUM_front_buffer_cached ";
  extensions += "GL_CHROMIUM_post_sub_buffer";
  return extensions;
}

gfx::Size TextureImageTransportSurface::GetSize() {
  return textures_[back()].size;
}

void* TextureImageTransportSurface::GetHandle() {
  return parent_stub_.get() ? parent_stub_->surface()->GetHandle() : NULL;
}


void TextureImageTransportSurface::OnNewSurfaceACK(
    uint64 surface_handle, TransportDIB::Handle /*shm_handle*/) {
}

void TextureImageTransportSurface::OnBuffersSwappedACK() {
  if (helper_->MakeCurrent()) {
    if (textures_[front_].size != textures_[back()].size) {
      CreateBackTexture(textures_[front_].size);
    } else {
      AttachBackTextureToFBO();
    }
  }

  // Even if MakeCurrent fails, schedule anyway, to trigger the lost context
  // logic.
  helper_->SetScheduled(true);
}

void TextureImageTransportSurface::OnPostSubBufferACK() {
  OnBuffersSwappedACK();
}

void TextureImageTransportSurface::OnResizeViewACK() {
  NOTREACHED();
}

void TextureImageTransportSurface::ReleaseBackTexture() {
  if (!parent_stub_.get())
    return;
  TextureInfo* info = GetParentInfo(textures_[back()].client_id);
  if (!info)
    return;

  GLuint service_id = info->service_id();
  if (!service_id)
    return;
  info->SetServiceId(0);

  {
    ScopedFrameBufferBinder fbo_binder(fbo_id_);
    glDeleteTextures(1, &service_id);
  }
  glFlush();
  CHECK_GL_ERROR();
}

void TextureImageTransportSurface::CreateBackTexture(const gfx::Size& size) {
  if (!parent_stub_.get())
    return;
  Texture& texture = textures_[back()];
  TextureInfo* info = GetParentInfo(texture.client_id);
  if (!info)
    return;

  GLuint service_id = info->service_id();

  if (service_id && texture.size == size)
    return;

  if (!service_id) {
    glGenTextures(1, &service_id);
    info->SetServiceId(service_id);
  }

  if (size != texture.size) {
    texture.size = size;
    TextureManager* texture_manager =
        parent_stub_->decoder()->GetContextGroup()->texture_manager();
    texture_manager->SetLevelInfo(
        info,
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        size.width(),
        size.height(),
        1,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        true);
  }

  {
    ScopedTextureBinder texture_binder(service_id);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
        size.width(), size.height(), 0,
        GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    CHECK_GL_ERROR();
  }

  AttachBackTextureToFBO();

  GpuHostMsg_AcceleratedSurfaceNew_Params params;
  params.width = size.width();
  params.height = size.height();
  params.surface_handle = texture.client_id;
  helper_->SendAcceleratedSurfaceNew(params);
  texture.sent_to_client = true;
}

void TextureImageTransportSurface::AttachBackTextureToFBO() {
  if (!parent_stub_.get())
    return;
  TextureInfo* info = GetParentInfo(textures_[back()].client_id);
  if (!info)
    return;

  ScopedFrameBufferBinder fbo_binder(fbo_id_);
  glFramebufferTexture2DEXT(GL_FRAMEBUFFER,
      GL_COLOR_ATTACHMENT0,
      GL_TEXTURE_2D,
      info->service_id(),
      0);
  glFlush();
  CHECK_GL_ERROR();

#ifndef NDEBUG
  GLenum status = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    DLOG(ERROR) << "Framebuffer incomplete.";
  }
#endif
}

TextureInfo* TextureImageTransportSurface::GetParentInfo(uint32 client_id) {
  DCHECK(parent_stub_.get());
  TextureManager* texture_manager =
      parent_stub_->decoder()->GetContextGroup()->texture_manager();
  TextureInfo* info = texture_manager->GetTextureInfo(client_id);
  return info;
}
