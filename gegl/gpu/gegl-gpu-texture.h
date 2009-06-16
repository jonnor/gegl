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

#ifndef __GEGL_GPU_TEXTURE_H__
#define __GEGL_GPU_TEXTURE_H__

#include <babl/babl.h>

#include "gegl.h"
#include "gegl-types.h"
#include "gegl-gpu-types.h"

G_BEGIN_DECLS

GType                gegl_gpu_texture_get_type (void) G_GNUC_CONST;
GeglGpuTexture      *gegl_gpu_texture_new      (gint                  width,
                                                gint                  height,
                                                const Babl           *format);
void                 gegl_gpu_texture_free     (GeglGpuTexture       *texture);
void                 gegl_gpu_texture_get      (const GeglGpuTexture *texture,
                                                const GeglRectangle  *roi,
                                                gpointer              dest,
                                                const Babl           *format);
void                 gegl_gpu_texture_set      (GeglGpuTexture       *texture,
                                                const GeglRectangle  *roi,
                                                gpointer              src,
                                                const Babl           *format);
void                 gegl_gpu_texture_clear    (GeglGpuTexture       *texture,
                                                const GeglRectangle  *roi);
void                 gegl_gpu_texture_copy     (const GeglGpuTexture *src,
                                                const GeglRectangle  *src_roi,
                                                GeglGpuTexture       *dest,
                                                gint                  dest_x,
                                                gint                  dest_y);
GeglGpuTexture      *gegl_gpu_texture_dup      (const GeglGpuTexture *texture);

#define gegl_gpu_texture_get_width(texture)  (texture->width)
#define gegl_gpu_texture_get_height(texture) (texture->height)

#define gegl_gpu_texture_get_pixel_count(texture) \
          (gegl_gpu_texture_get_width(texture) \
           * gegl_gpu_texture_get_height(texture))

G_END_DECLS
                                               
#endif /* __GEGL_GPU_TEXTURE_H__ */
