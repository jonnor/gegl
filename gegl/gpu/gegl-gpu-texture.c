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

#include <GL/glew.h>
#include <GL/glut.h>
#include <babl/babl.h>

#include "gegl.h"
#include "gegl-types.h"
#include "gegl-gpu-types.h"
#include "gegl-gpu-texture.h"

GeglGpuTexture *
gegl_gpu_texture_new (gint        width,
                      gint        height,
                      const Babl *format)
{
  GeglGpuTexture *texture = g_new (GeglGpuTexture, 1);

  glGenTextures (1, &texture->handle);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture->handle);

  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB,
                   GL_TEXTURE_MIN_FILTER,
                   GL_NEAREST);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB,
                   GL_TEXTURE_MAG_FILTER,
                   GL_NEAREST);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP);

  glTexImage2D  (GL_TEXTURE_RECTANGLE_ARB,
                 0,
                 GL_RGBA32F_ARB,
                 width,
                 height,
                 0,
                 GL_RGBA,
                 GL_FLOAT,
                 0);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, 0);

  gegl_gpu_texture_clear (texture, NULL);

  texture->width  = width;
  texture->height = height;

  /* implement storing of more compact formats later, up-sampling to
   * RGBA floats will have to make do, for now
   */
  texture->format = babl_format ("RGBA float");
  return texture;
}

void
gegl_gpu_texture_free (GeglGpuTexture *texture)
{
  glDeleteTextures (1, &texture->handle);
  g_free (texture);
}

void
gegl_gpu_texture_get (const GeglGpuTexture *texture,
                      const GeglRectangle  *roi,
                      gpointer              dest,
                      const Babl           *format)
{
  gpointer buf;
  gint     pixel_count = (roi != NULL)
                           ? roi->width * roi->height
                           : gegl_gpu_texture_get_pixel_count (texture);

  if (format != NULL && format != texture->format)
    {
      gint bpp = babl_format_get_bytes_per_pixel (texture->format);
      buf      = g_malloc (pixel_count * bpp);
    }
  else
    buf = dest;

  if (roi == NULL || (roi->x == 0 && roi->y == 0
                      && roi->width == texture->width
                      && roi->height == texture->height))
    {
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture->handle);
      glGetTexImage (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, GL_FLOAT, buf);
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, 0);
    }
  else
    {
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture->handle);
      glReadPixels  (roi->x,
                     roi->y,
                     roi->width,
                     roi->height,
                     GL_RGBA,
                     GL_FLOAT,
                     buf);
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, 0);
    }

  if (buf != dest)
    {
      Babl *fish = babl_fish ((gpointer) texture->format,
                              (gpointer) format);

      babl_process (fish, buf, dest, pixel_count);
      g_free (buf);
    }
}

void
gegl_gpu_texture_set (GeglGpuTexture      *texture,
                      const GeglRectangle *roi,
                      gpointer             src,
                      const Babl          *format)
{
  gpointer buf;

  if (format != NULL && format != texture->format)
    {
      Babl *fish = babl_fish ((gpointer) format,
                              (gpointer) texture->format);

      gint pixel_count = roi->width * roi->height;
      gint bpp         = babl_format_get_bytes_per_pixel (texture->format);

      buf = g_malloc (pixel_count * bpp);
      babl_process (fish, src, buf, pixel_count);
    }
  else
    buf = src;

  if (roi == NULL || (roi->x == 0 && roi->y == 0
                      && roi->width == texture->width
                      && roi->height == texture->height))
    {
      gint width  = texture->width;
      gint height = texture->height;

      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture->handle);
      glTexImage2D  (GL_TEXTURE_RECTANGLE_ARB,
                     0,
                     GL_RGBA32F_ARB,
                     width,
                     height,
                     0,
                     GL_RGBA,
                     GL_FLOAT,
                     buf);
      glBindTexture (GL_TEXTURE_RECTANGLE_ARB, 0);
    }
  else
    {
      glBindTexture   (GL_TEXTURE_RECTANGLE_ARB, texture->handle);
      glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB,
                       0,
                       roi->x,
                       roi->y,
                       roi->width,
                       roi->height,
                       GL_RGBA,
                       GL_FLOAT,
                       buf);
      glBindTexture   (GL_TEXTURE_RECTANGLE_ARB, 0);
    }

  if (buf != src)
    g_free (buf);
}

void
gegl_gpu_texture_clear (GeglGpuTexture      *texture,
                        const GeglRectangle *roi)
{
  gint width   = (roi != NULL) ? roi->width : texture->width;
  gint height  = (roi != NULL) ? roi->height : texture->height;
  gint bpp     = babl_format_get_bytes_per_pixel (texture->format);
  gpointer buf = g_malloc0 (width * height * bpp);

  gegl_gpu_texture_set (texture, roi, buf, NULL);
  g_free (buf);
}

void
gegl_gpu_texture_copy (const GeglGpuTexture *src,
                       const GeglRectangle  *src_rect,
                       GeglGpuTexture       *dest,
                       gint                  dest_x,
                       gint                  dest_y)
{
  if (src->format == dest->format)
    {
      glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT,
                                 GL_COLOR_ATTACHMENT0_EXT,
                                 GL_TEXTURE_RECTANGLE_ARB,
                                 src->handle,
                                 0);

      glBindTexture       (GL_TEXTURE_RECTANGLE_ARB, dest->handle);
      glCopyTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB,
                           0,
                           dest_x,
                           dest_y,
                           src_rect->x,
                           src_rect->y,
                           src_rect->width,
                           src_rect->height);
      glBindTexture       (GL_TEXTURE_RECTANGLE_ARB, 0);

      glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT,
                                 GL_COLOR_ATTACHMENT0_EXT,
                                 GL_TEXTURE_RECTANGLE_ARB,
                                 0,
                                 0);
    }
  else
    g_assert (0);
}

GeglGpuTexture *
gegl_gpu_texture_dup (const GeglGpuTexture *texture)
{
  GeglGpuTexture *result = gegl_gpu_texture_new (texture->width,
                                                 texture->height,
                                                 texture->format);

  gegl_gpu_texture_copy (texture, NULL, result, 0, 0);
  return result;
}

static GeglGpuTexture *
gegl_gpu_texture_shallow_dup (const GeglGpuTexture *texture)
{
  GeglGpuTexture *result = g_new (GeglGpuTexture, 1);
  *result = *texture;
  return result;
}

GType
gegl_gpu_texture_get_type (void)
{
  static GType our_type = 0;

  if (our_type == 0)
    our_type = g_boxed_type_register_static (
                 g_intern_static_string ("GeglGpuTexture"),
                 (GBoxedCopyFunc) gegl_gpu_texture_shallow_dup,
                 (GBoxedFreeFunc) g_free);

  return our_type;
}
