/* This file is part of GEGL.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GEGL; if not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright 2009 Jerson Michael Perpetua <jersonperpetua@gmail.com>
 */

#ifndef __GEGL_GPU_TYPES_H__
#define __GEGL_GPU_TYPES_H__

#include <GL/gl.h>
#include <babl/babl.h>
#include <glib-object.h>

G_BEGIN_DECLS

typedef GLuint GeglGpuTextureHandle;
typedef GLuint GeglGpuFramebufferHandle;
typedef GLuint GeglGpuShaderProgramHandle;

struct _GeglGpuTexture
{
  GeglGpuTextureHandle handle;

  gint                 width;
  gint                 height;
  Babl                *format;
};
GType gegl_gpu_texture_get_type (void) G_GNUC_CONST;
#define GEGL_TYPE_GPU_TEXTURE   (gegl_gpu_texture_get_type())

typedef struct _GeglGpuTexture GeglGpuTexture;

G_END_DECLS

#endif  /* __GEGL_GPU_TYPES_H__ */
