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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright 2008 Øyvind Kolås <pippin@gimp.org>
 */

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <glib-object.h>
#include <glib/gprintf.h>

#include "gegl.h"
#include "gegl-types-internal.h"
#include "gegl-buffer-types.h"
#include "gegl-buffer-iterator.h"
#include "gegl-buffer-private.h"
#include "gegl-tile-storage.h"
#include "gegl-utils.h"

#if HAVE_GPU
#include "gegl-gpu-types.h"
#include "gegl-gpu-texture.h"
#include "gegl-gpu-init.h"
#endif

typedef struct GeglBufferTileIterator
{
  GeglBuffer      *buffer;
  GeglRectangle    roi;      /* the rectangular region we're iterating over */
  GeglTile        *tile;     /* current tile */

  GeglTileLockMode lock_mode;

  GeglRectangle    subrect;  /* the rectangular subregion of data in the
                              * buffer represented by this scan
                              */
  gpointer         sub_data; /* pointer to the data as indicated by subrect */
#if HAVE_GPU
  GeglGpuTexture  *gpu_data; /* pointer to the tile's full GPU data */
#endif

  /* used internally */
  gint             next_col;
  gint             next_row;

} _GeglBufferTileIterator;

#define GEGL_BUFFER_SCAN_COMPATIBLE   128 /* should be integrated into enum */
#define GEGL_BUFFER_FORMAT_COMPATIBLE 256 /* should be integrated into enum */

#define DEBUG_DIRECT 0

typedef struct _GeglBufferIterator
{
  /* current region of interest */
  gint                    length; /* length of current data in pixels */
  gpointer                data    [GEGL_BUFFER_MAX_ITERABLES];
  GeglRectangle           roi     [GEGL_BUFFER_MAX_ITERABLES];
#if HAVE_GPU
  GeglGpuTexture         *gpu_data[GEGL_BUFFER_MAX_ITERABLES];
#endif

  /* the following is private */
  gint                    iterable_count;
  gint                    iteration_no;
  gboolean                is_done;

  GeglBuffer             *buffer  [GEGL_BUFFER_MAX_ITERABLES];
  GeglRectangle           rect    [GEGL_BUFFER_MAX_ITERABLES];
  const Babl             *format  [GEGL_BUFFER_MAX_ITERABLES];
  guint                   flags   [GEGL_BUFFER_MAX_ITERABLES];
  _GeglBufferTileIterator i       [GEGL_BUFFER_MAX_ITERABLES];

} _GeglBufferIterator;

static void
gegl_buffer_tile_iterator_init (_GeglBufferTileIterator *i,
                                GeglBuffer              *buffer,
                                GeglRectangle            roi,
                                GeglTileLockMode         lock_mode);
static gboolean
gegl_buffer_tile_iterator_next (_GeglBufferTileIterator *i);

/* checks whether iterations on two buffers starting from the given coordinates
 * with the same width and height would be able to run in parallel.
 */
static gboolean
gegl_buffer_scan_compatible (GeglBuffer *buffer1,
                             gint        x1,
                             gint        y1,
                             GeglBuffer *buffer2,
                             gint        x2,
                             gint        y2)
{
  if (buffer1->tile_storage->tile_width != buffer2->tile_storage->tile_width)
    return FALSE;

  if (buffer1->tile_storage->tile_height != buffer2->tile_storage->tile_height)
    return FALSE;

  /* essentially equivalent to (pseudo-code):
   * tile_offset (shift_x1 + x1) == tile_offset (shift_x2 + x2)
   */
  if (abs((buffer1->shift_x + x1) - (buffer2->shift_x + x2))
      % buffer1->tile_storage->tile_width != 0)
    return FALSE;

  /* essentially equivalent to (pseudo-code):
   * tile_offset (shift_y1 + y1) == tile_offset (shift_y2 + y2)
   */
  if (abs((buffer1->shift_y + y1) - (buffer2->shift_y + y2))
      % buffer1->tile_storage->tile_height != 0)
    return FALSE;

  return TRUE;
}

static void
gegl_buffer_tile_iterator_init (_GeglBufferTileIterator *i,
                                GeglBuffer              *buffer,
                                GeglRectangle            roi,
                                GeglTileLockMode         lock_mode)
{
  g_assert (i != NULL);

  if (roi.width == 0 || roi.height == 0)
    g_error ("eeek");

  i->buffer = buffer;
  i->roi    = roi;
  i->tile   = NULL;

  i->lock_mode = lock_mode;

  memset (&i->subrect, 0, sizeof (GeglRectangle));
  i->sub_data = NULL;
#if HAVE_GPU
  i->gpu_data = NULL;
#endif

  i->next_row = 0;
  i->next_col = 0;
}

static gboolean
gegl_buffer_tile_iterator_next (_GeglBufferTileIterator *i)
{
  GeglBuffer *buffer = i->buffer;

  gint tile_width  = buffer->tile_storage->tile_width;
  gint tile_height = buffer->tile_storage->tile_height;

  gint buffer_x = buffer->extent.x + buffer->shift_x;
  gint buffer_y = buffer->extent.y + buffer->shift_y;

  if (i->roi.width == 0 || i->roi.height == 0)
    return FALSE;

gulp:

  /* unref previously held tile */
  if (i->tile != NULL)
    {
      if ((i->lock_mode & GEGL_TILE_LOCK_WRITE)
       && i->subrect.width == tile_width)
        {
          gegl_tile_unlock (i->tile);
        }
      gegl_tile_unref (i->tile);
      i->tile = NULL;

      i->sub_data = NULL;
#if HAVE_GPU
      i->gpu_data = NULL;
#endif
    }

  memset (&i->subrect, 0, sizeof (GeglRectangle));

  if (i->next_col < i->roi.width)
    {
      /* return tile on this row */
      gint x = buffer_x + i->next_col;
      gint y = buffer_y + i->next_row;

      gint offset_x = gegl_tile_offset (x, tile_width);
      gint offset_y = gegl_tile_offset (y, tile_height);

      GeglRectangle rect = {offset_x, offset_y, 0, 0};

      gboolean direct_access;
#if HAVE_GPU
      gboolean gpu_direct_access;
#endif

      if (i->roi.width + offset_x - i->next_col < tile_width)
        rect.width = (i->roi.width + offset_x - i->next_col) - offset_x;
      else
        rect.width = tile_width - offset_x;

      if (i->roi.height + offset_y - i->next_row < tile_height)
        rect.height = (i->roi.height + offset_y - i->next_row) - offset_y;
      else
        rect.height = tile_height - offset_y;

      direct_access = ((i->lock_mode & GEGL_TILE_LOCK_READ
                                 || i->lock_mode & GEGL_TILE_LOCK_WRITE)
                                && tile_width == rect.width);

#if HAVE_GPU
      gpu_direct_access = ((i->lock_mode & GEGL_TILE_LOCK_GPU_READ
                            || i->lock_mode & GEGL_TILE_LOCK_GPU_WRITE)
                           && tile_width == rect.width
                           && tile_height == rect.height);
#endif

      if (direct_access 
#if HAVE_GPU
       || gpu_direct_access
#endif
       )
        {
          i->tile = gegl_tile_source_get_tile ((GeglTileSource *) buffer,
                                               gegl_tile_index (x, tile_width),
                                               gegl_tile_index (y, tile_height),
                                               0);

          gegl_tile_lock (i->tile, i->lock_mode);

          if (direct_access)
            {
              gpointer data = gegl_tile_get_data (i->tile);
              gint bpp = babl_format_get_bytes_per_pixel (buffer->format);
              i->sub_data = (guchar *) data + (bpp * rect.y * tile_width);
            }
#if HAVE_GPU
          if (gpu_direct_access)
            i->gpu_data = gegl_tile_get_gpu_data (i->tile);
#endif
        }

      i->subrect.x      = i->roi.x + i->next_col;
      i->subrect.y      = i->roi.y + i->next_row;
      i->subrect.width  = rect.width;
      i->subrect.height = rect.height;

      i->next_col += tile_width - offset_x;

      return TRUE;
    }
  else
    {
      /* move down to the next row */
      gint y        = buffer_y + i->next_row;
      gint offset_y = gegl_tile_offset (y, tile_height);

      i->next_row += tile_height - offset_y;
      i->next_col  = 0;

      /* return the first tile in the next row */
      if (i->next_row < i->roi.height)
        goto gulp;

      return FALSE;
    }

  return FALSE;
}

gint
gegl_buffer_iterator_add (GeglBufferIterator  *iterator,
                          GeglBuffer          *buffer,
                          const GeglRectangle *roi,
                          const Babl          *format,
                          guint                flags)
{
  gint self = 0;

  _GeglBufferIterator *i         = (gpointer) iterator;
  GeglTileLockMode     lock_mode = GEGL_TILE_LOCK_NONE;

  if (i->iterable_count + 1 > GEGL_BUFFER_MAX_ITERABLES)
    g_error ("too many iterables (%i)", i->iterable_count + 1);

  /* for sanity, we zero at init */
  if (i->iterable_count == 0)
    memset (i, 0, sizeof (_GeglBufferIterator));

  self = i->iterable_count++;

  if (roi == NULL)
    roi = (self == 0) ? &buffer->extent: &i->rect[0];

  i->buffer[self] =  gegl_buffer_create_sub_buffer (buffer, roi);
  i->rect  [self] = *roi;
  i->format[self] =  (format != NULL) ? format : buffer->format;
  i->flags [self] =  flags;

  if (flags & GEGL_BUFFER_READ)
    lock_mode |= GEGL_TILE_LOCK_READ;
  if (flags & GEGL_BUFFER_WRITE)
    lock_mode |= GEGL_TILE_LOCK_WRITE;

#if HAVE_GPU
  if (gegl_gpu_is_accelerated ())
    {
      if (flags & GEGL_BUFFER_GPU_READ)
        lock_mode |= GEGL_TILE_LOCK_GPU_READ;
      if (flags & GEGL_BUFFER_GPU_WRITE)
        lock_mode |= GEGL_TILE_LOCK_GPU_WRITE;
    }
  else
    /* do not allow iteration on GPU data when GPU acceleration
     * is disabled
     */
    i->flags[self] &= ~GEGL_BUFFER_GPU_READ & ~GEGL_BUFFER_GPU_WRITE;
#endif

  if (self == 0)
    {
      /* the first buffer is where we base scan alignment on */
      i->flags[self] |= GEGL_BUFFER_SCAN_COMPATIBLE;
      gegl_buffer_tile_iterator_init (&i->i[self],
                                      i->buffer[self],
                                      i->rect[self],
                                      lock_mode);
    }
  else
    {
      /* we make all subsequently added iterables share the width and height of
       * the first one
       */
      i->rect[self].width  = i->rect[0].width;
      i->rect[self].height = i->rect[0].height;

      if (gegl_buffer_scan_compatible (i->buffer[0],
                                       i->rect[0].x,
                                       i->rect[0].y,
                                       i->buffer[self],
                                       i->rect[self].x,
                                       i->rect[self].y))
        {
          i->flags[self] |= GEGL_BUFFER_SCAN_COMPATIBLE;
          gegl_buffer_tile_iterator_init (&i->i[self],
                                          i->buffer[self],
                                          i->rect[self],
                                          lock_mode);
        }
    }

  if (i->format[self] == i->buffer[self]->format)
    i->flags[self] |= GEGL_BUFFER_FORMAT_COMPATIBLE;

  return self;
}

/* XXX: keeping a small pool of such buffers around for the used formats
 * would probably improve performance (old note from pippin, kept as a
 * reminder)
 */

typedef struct BufInfo {
  gint     size;
  gint     used; /* if this buffer is currently allocated */
  gpointer buf;
} BufInfo;

static GArray *buf_pool = NULL;

#if ENABLE_MT
static GStaticMutex pool_mutex = G_STATIC_MUTEX_INIT;
#endif

static gpointer iterator_buf_pool_get (gint size)
{
  gint cnt;
#if ENABLE_MT
  g_static_mutex_lock (&pool_mutex);
#endif

  if (G_UNLIKELY (!buf_pool))
    buf_pool = g_array_new (TRUE, TRUE, sizeof (BufInfo));

  for (cnt = 0; cnt < buf_pool->len; cnt++)
    {
      BufInfo *info = &g_array_index (buf_pool, BufInfo, cnt);

      if (info->size >= size && info->used == 0)
        {
          info->used ++;
#if ENABLE_MT
          g_static_mutex_unlock (&pool_mutex);
#endif
          return info->buf;
        }
    }
  {
    BufInfo info = {0, 1, NULL};
    info.size = size;
    info.buf = gegl_malloc (size);
    g_array_append_val (buf_pool, info);
#if ENABLE_MT
    g_static_mutex_unlock (&pool_mutex);
#endif
    return info.buf;
  }
}

static void
iterator_buf_pool_release (gpointer buf)
{
  gint i;
#if ENABLE_MT
  g_static_mutex_lock (&pool_mutex);
#endif
  for (i=0; i<buf_pool->len; i++)
    {
      BufInfo *info = &g_array_index (buf_pool, BufInfo, i);

      if (info->buf == buf)
        info->used--;
    }
#if ENABLE_MT
  g_static_mutex_unlock (&pool_mutex);
#endif
}

#if HAVE_GPU

typedef struct GpuTextureInfo {
  gint            used; /* if this texture is currently allocated */
  GeglGpuTexture *texture;
} GpuTextureInfo;

static GArray *gpu_texture_pool = NULL;

static GeglGpuTexture *
iterator_gpu_texture_pool_get (gint width,
                               gint height,
                               const Babl *format)
{
  gint cnt;

  if (G_UNLIKELY (!gpu_texture_pool))
    gpu_texture_pool = g_array_new (TRUE, TRUE, sizeof (GpuTextureInfo));

  for (cnt = 0; cnt < gpu_texture_pool->len; cnt++)
    {
      GpuTextureInfo *info = &g_array_index (gpu_texture_pool,
                                             GpuTextureInfo,
                                             cnt);

      if (info->texture->width == width
          && info->texture->height == height
          && info->texture->format == format
          && info->used == 0)
        {
          info->used++;
          return info->texture;
        }
    }
    {
      GpuTextureInfo info = {1, NULL};
      info.texture  = gegl_gpu_texture_new (width, height, format);

      g_array_append_val (gpu_texture_pool, info);
      return info.texture;
    }
}

static void
iterator_gpu_texture_pool_release (GeglGpuTexture *texture)
{
  gint cnt;

  for (cnt = 0; cnt < gpu_texture_pool->len; cnt++)
    {
      GpuTextureInfo *info = &g_array_index (gpu_texture_pool,
                                             GpuTextureInfo,
                                             cnt);

      if (info->texture == texture)
        info->used--;
    }
}
#endif

void
gegl_buffer_iterator_cleanup (void)
{
  gint cnt;

  if (buf_pool != NULL)
    {
      for (cnt = 0; cnt < buf_pool->len; cnt++)
        {
          BufInfo *info = &g_array_index (buf_pool, BufInfo, cnt);
          gegl_free (info->buf);
          info->buf = NULL;
        }

      g_array_free (buf_pool, TRUE);
      buf_pool = NULL;
    }

#if HAVE_GPU
  if (gpu_texture_pool != NULL)
    {
      for (cnt = 0; cnt < gpu_texture_pool->len; cnt++)
        {
          GpuTextureInfo *info = &g_array_index (gpu_texture_pool,
                                                 GpuTextureInfo,
                                                 cnt);

          gegl_gpu_texture_free (info->texture);
          info->texture = NULL;
        }

      g_array_free (gpu_texture_pool, TRUE);
      gpu_texture_pool = NULL;
    }
#endif
}



#if DEBUG_DIRECT
static glong direct_read = 0;
static glong direct_write = 0;
static glong in_direct_read = 0;
static glong in_direct_write = 0;
#endif

gboolean
gegl_buffer_iterator_next (GeglBufferIterator *iterator)
{
  gint     no;
  gboolean result = FALSE;

  _GeglBufferIterator *i = (gpointer) iterator;

  if (i->is_done)
    g_error ("%s called on finished buffer iterator", G_STRFUNC);
  if (i->iteration_no == 0)
    {
#if ENABLE_MT
      for (no=0; no<i->iterable_count; no++)
        {
          gint j;
          gboolean found = FALSE;
          for (j=0; j<no; j++)
            if (i->buffer[no]==i->buffer[j])
              {
                found = TRUE;
                break;
              }
          if (!found)
            gegl_buffer_lock (i->buffer[no]);
        }
#endif
    }
  else
    {
      /* complete pending write work */
      for (no=0; no<i->iterable_count;no++)
        {
          gboolean direct_access
            = (i->flags[no] & GEGL_BUFFER_SCAN_COMPATIBLE
               && i->flags[no] & GEGL_BUFFER_FORMAT_COMPATIBLE
               && i->roi[no].width
                    == i->i[no].buffer->tile_storage->tile_width);

#if HAVE_GPU
          gboolean gpu_direct_access
            = (direct_access && i->roi[no].height
                 == i->i[no].buffer->tile_storage->tile_height);
#endif

          if (i->flags[no] & GEGL_BUFFER_READ
              || i->flags[no] & GEGL_BUFFER_WRITE)
            {
              if (direct_access)
                {
#if DEBUG_DIRECT
                  if (i->flags[no] & GEGL_BUFFER_WRITE)
                    direct_write += i->roi[no].width * i->roi[no].height;
#endif
                }
              else
                {
                  if (i->flags[no] & GEGL_BUFFER_WRITE)
                    {
#if DEBUG_DIRECT
                      in_direct_write += i->roi[no].width * i->roi[no].height;
#endif
                      gegl_buffer_set_unlocked (i->buffer[no],
                                                &(i->roi[no]),
                                                i->format[no],
                                                i->data[no],
                                                GEGL_AUTO_ROWSTRIDE);
                    }

                  /* XXX: might be inefficient given the current
                   * implementation, it should be easy to reimplement the
                   * pool as a hash table though
                   */
                  iterator_buf_pool_release (i->data[no]);
                }

              i->data[no] = NULL;
            }

#if HAVE_GPU
          if (i->flags[no] & GEGL_BUFFER_GPU_READ
              || i->flags[no] & GEGL_BUFFER_GPU_WRITE)
            {
              if (gpu_direct_access)
                {
#if DEBUG_DIRECT
                  if (i->flags[no] & GEGL_BUFFER_GPU_WRITE)
                    direct_write += i->roi[no].width * i->roi[no].height;
#endif
                }
              else
                {
                  if (i->flags[no] & GEGL_BUFFER_GPU_WRITE)
                    {
#if DEBUG_DIRECT
                      in_direct_write += i->roi[no].width * i->roi[no].height;
#endif
                      gegl_buffer_gpu_set (i->buffer[no],
                                           &i->roi[no],
                                           i->gpu_data[no]);
                    }

                  /* XXX: might be inefficient given the current
                   * implementation, it should be easy to reimplement the
                   * pool as a hash table though
                   */
                  iterator_gpu_texture_pool_release (i->gpu_data[no]);
                }

              i->gpu_data[no] = NULL;
            }
#endif

          memset (&i->roi[no], 0, sizeof (GeglRectangle));
        }
    }

  g_assert (i->iterable_count > 0);

  /* then we iterate all */
  for (no = 0; no < i->iterable_count; no++)
    {
      if (i->flags[no] & GEGL_BUFFER_SCAN_COMPATIBLE)
        {
          gboolean res = gegl_buffer_tile_iterator_next (&i->i[no]);

          gint tile_width  = i->i[no].buffer->tile_storage->tile_width;
          gint tile_height = i->i[no].buffer->tile_storage->tile_height;

          gboolean direct_access;
          gboolean gpu_direct_access;

          result     = (no == 0) ? res : result;
          i->roi[no] = i->i[no].subrect;

          /* since they were scan compatible, this shouldn't happen */
          if (res != result)
            g_error ("%i==%i != 0==%i\n", no, res, result);

          direct_access = (i->flags[no] & GEGL_BUFFER_FORMAT_COMPATIBLE
                           && i->roi[no].width == tile_width);

          gpu_direct_access = (i->roi[no].height == tile_height
                               && direct_access);

          if (i->flags[no] & GEGL_BUFFER_READ
              || i->flags[no] & GEGL_BUFFER_WRITE)
            {
              if (direct_access)
                {
                  i->data[no] = i->i[no].sub_data;
#if DEBUG_DIRECT
                  direct_read += i->roi[no].width * i->roi[no].height;
#endif
                }
              else
                {
                  /* unref held tile to prevent lock contention */
                  if (i->i[no].tile != NULL)
                    {
                      gegl_tile_unlock (i->i[no].tile);

                      gegl_tile_unref (i->i[no].tile);
                      i->i[no].tile = NULL;

                      i->i[no].sub_data = NULL;
                    }

                  i->data[no] = iterator_buf_pool_get (
                                  i->roi[no].width * i->roi[no].height *
                              babl_format_get_bytes_per_pixel (i->format[no]));

                  if (i->flags[no] & GEGL_BUFFER_READ)
                    gegl_buffer_get_unlocked (i->buffer[no],
                                              1.0, &(i->roi[no]),
                                              i->format[no], 
                                              i->data[no],
                                              GEGL_AUTO_ROWSTRIDE);
#if DEBUG_DIRECT
                  in_direct_read += i->roi[no].width * i->roi[no].height;
#endif
                }
            }

#if HAVE_GPU
          if (i->flags[no] & GEGL_BUFFER_GPU_READ
              || i->flags[no] & GEGL_BUFFER_GPU_WRITE)
            {
              if (gpu_direct_access)
                {
                  i->gpu_data[no] = i->i[no].gpu_data;
#if DEBUG_DIRECT
                  direct_read += i->roi[no].width * i->roi[no].height;
#endif
                }
              else
                {
                  /* unref held tile to prevent lock contention */
                  if (i->i[no].tile != NULL)
                    {
                      gegl_tile_unlock (i->i[no].tile);

                      gegl_tile_unref (i->i[no].tile);
                      i->i[no].tile = NULL;

                      i->i[no].gpu_data = NULL;
                    }

                  i->gpu_data[no] = iterator_gpu_texture_pool_get (
                                      i->roi[no].width,
                                      i->roi[no].height,
                                      i->format[no]);

                  if (i->flags[no] & GEGL_BUFFER_GPU_READ)
                    gegl_buffer_gpu_get (i->buffer[no],
                                         1.0,
                                         &i->roi[no],
                                         i->gpu_data[no]);
#if DEBUG_DIRECT
                  in_direct_read += i->roi[no].width * i->roi[no].height;
#endif
                }
            }
#endif
        }
      else
        {
          /* we copy the roi from iterator 0  */
          i->roi[no]    = i->roi[0];
          i->roi[no].x += i->rect[no].x - i->rect[0].x;
          i->roi[no].y += i->rect[no].y - i->rect[0].y;

          if (i->flags[no] & GEGL_BUFFER_READ
              || i->flags[no] & GEGL_BUFFER_WRITE)
            {
              i->data[no] = iterator_buf_pool_get (i->roi[no].width *
                                                   i->roi[no].height *
                               babl_format_get_bytes_per_pixel (i->format[no]));

              if (i->flags[no] & GEGL_BUFFER_READ)
                gegl_buffer_get (i->buffer[no],
                                 1.0,
                                 &i->roi[no],
                                 i->format[no],
                                 i->data[no],
                                 GEGL_AUTO_ROWSTRIDE);
#if DEBUG_DIRECT
              in_direct_read += i->roi[no].width * i->roi[no].height;
#endif
            }

#if HAVE_GPU
          if (i->flags[no] & GEGL_BUFFER_GPU_READ
              || i->flags[no] & GEGL_BUFFER_GPU_WRITE)
            {
              i->gpu_data[no] = iterator_gpu_texture_pool_get (
                                  i->roi[no].width,
                                  i->roi[no].height,
                                  i->format[no]);

              if (i->flags[no] & GEGL_BUFFER_GPU_READ)
                gegl_buffer_gpu_get (i->buffer[no],
                                     1.0,
                                     &i->roi[no],
                                     i->gpu_data[no]);
#if DEBUG_DIRECT
              in_direct_read += i->roi[no].width * i->roi[no].height;
#endif
            }
#endif
        }

      i->length = i->roi[no].width * i->roi[no].height;
    }

  i->iteration_no++;

  if (result == FALSE)
    {

#if ENABLE_MT
      for (no=0; no<i->iterable_count;no++)
        {
          gint j;
          gboolean found = FALSE;
          for (j=0; j<no; j++)
            if (i->buffer[no]==i->buffer[j])
              {
                found = TRUE;
                break;
              }
          if (!found)
            gegl_buffer_unlock (i->buffer[no]);
        }
#endif

      for (no=0; no<i->iterable_count;no++)
        {
          g_object_unref (i->buffer[no]);
          i->buffer[no] = NULL;
        }

#if DEBUG_DIRECT
      g_print ("%f %f\n",
               100.0 * direct_read / (in_direct_read + direct_read),
               100.0 * direct_write / (in_direct_write + direct_write));
#endif
      i->is_done = TRUE;
    }

  return result;
}

GeglBufferIterator *
gegl_buffer_iterator_new (GeglBuffer          *buffer,
                          const GeglRectangle *roi,
                          const Babl          *format,
                          guint                flags)
{
  GeglBufferIterator *i = (gpointer) g_new0 (_GeglBufferIterator, 1);
  gegl_buffer_iterator_add (i, buffer, roi, format, flags);
  return i;
}

void
gegl_buffer_iterator_free (GeglBufferIterator *iterator)
{
  gint cnt;

  _GeglBufferIterator *i = (gpointer) iterator;

  for (cnt = 0; cnt < i->iterable_count; cnt++)
    {
      if (i->buffer[cnt] != NULL)
        {
          g_object_unref (i->buffer[cnt]);
          i->buffer[cnt] = NULL;
        }

      if (i->data[cnt] != NULL)
        iterator_buf_pool_release (i->data[cnt]);
    }

  g_free (i);
}
