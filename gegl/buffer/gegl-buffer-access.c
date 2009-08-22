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
 * Copyright 2006-2008 Øyvind Kolås <pippin@gimp.org>
 */

#include "config.h"
#include <string.h>
#include <math.h>

#include <glib-object.h>
#include <glib/gprintf.h>
#if HAVE_GIO
#include <gio/gio.h>
#endif

#include "gegl.h"
#include "gegl-types-internal.h"
#include "gegl-buffer-types.h"
#include "gegl-buffer.h"
#include "gegl-buffer-private.h"
#include "gegl-tile-storage.h"
#include "gegl-utils.h"
#include "gegl-sampler-nearest.h"
#include "gegl-sampler-linear.h"
#include "gegl-sampler-cubic.h"
#include "gegl-sampler-lanczos.h"
#include "gegl-sampler-sharp.h"
#include "gegl-sampler-yafr.h"
#include "gegl-buffer-index.h"
#include "gegl-tile-backend.h"
#include "gegl-buffer-iterator.h"

#include "gegl-region.h"
#include "gegl-gpu-types.h"
#include "gegl-gpu-texture.h"

#if ENABLE_MP
GStaticRecMutex mutex = G_STATIC_REC_MUTEX_INIT;
#endif


#if 0
static inline void
gegl_buffer_pixel_set (GeglBuffer *buffer,
                       gint        x,
                       gint        y,
                       const Babl *format,
                       guchar     *buf)
{
  gint  tile_width  = buffer->tile_storage->tile_width;
  gint  tile_height  = buffer->tile_storage->tile_width;
  gint  px_size     = gegl_buffer_px_size (buffer);
  gint  bpx_size    = babl_format_get_bytes_per_pixel (format);
  Babl *fish        = NULL;

  gint  abyss_x_total  = buffer->abyss.x + buffer->abyss.width;
  gint  abyss_y_total  = buffer->abyss.y + buffer->abyss.height;
  gint  buffer_x       = buffer->extent.x;
  gint  buffer_y       = buffer->extent.y;
  gint  buffer_abyss_x = buffer->abyss.x;
  gint  buffer_abyss_y = buffer->abyss.y;

  if (format != buffer->format)
    {
      fish = babl_fish (buffer->format, format);
    }

  {
    if (!(buffer_y + y >= buffer_abyss_y &&
          buffer_y + y < abyss_y_total &&
          buffer_x + x >= buffer_abyss_x &&
          buffer_x + x < abyss_x_total))
      { /* in abyss */
        return;
      }
    else
      {
        gint      tiledy = buffer_y + buffer->shift_y + y;
        gint      tiledx = buffer_x + buffer->shift_x + x;

        GeglTile *tile = gegl_tile_source_get_tile ((GeglTileSource *) (buffer),
                                                gegl_tile_index (tiledx, tile_width),
                                                gegl_tile_index (tiledy, tile_height),
                                                0);

        if (tile)
          {
            gint    offsetx = gegl_tile_offset (tiledx, tile_width);
            gint    offsety = gegl_tile_offset (tiledy, tile_height);
            guchar *tp;

            gegl_tile_lock (tile, GEGL_TILE_LOCK_WRITE);
            tp = gegl_tile_get_data (tile) +
                 (offsety * tile_width + offsetx) * px_size;
            if (fish)
              babl_process (fish, buf, tp, 1);
            else
              memcpy (tp, buf, bpx_size);

            gegl_tile_unlock (tile);
            g_object_unref (tile);
          }
      }
  }
  return;
}
#endif

static gboolean
gegl_buffer_in_abyss( GeglBuffer *buffer,
                      gint        x,
                      gint        y )
{
  gint  buffer_shift_x = buffer->shift_x;
  gint  buffer_shift_y = buffer->shift_y;
  gint  buffer_abyss_x = buffer->abyss.x + buffer_shift_x;
  gint  buffer_abyss_y = buffer->abyss.y + buffer_shift_y;
  gint  abyss_x_total  = buffer_abyss_x + buffer->abyss.width;
  gint  abyss_y_total  = buffer_abyss_y + buffer->abyss.height;


  gint tiledy = y + buffer_shift_y;
  gint tiledx = x + buffer_shift_x;

  return !(tiledy >= buffer_abyss_y &&
           tiledy <  abyss_y_total  &&
           tiledx >= buffer_abyss_x &&
           tiledx <  abyss_x_total);
}

static inline void
gegl_buffer_set_pixel (GeglBuffer *buffer,
                       gint        x,
                       gint        y,
                       const Babl *format,
                       gpointer    data)
{
  guchar *buf         = data;
  gint    tile_width  = buffer->tile_storage->tile_width;
  gint    tile_height = buffer->tile_storage->tile_height;
  gint    bpx_size    = babl_format_get_bytes_per_pixel (format);
  Babl   *fish        = NULL;

  gint  buffer_shift_x = buffer->shift_x;
  gint  buffer_shift_y = buffer->shift_y;
  gint  px_size        = babl_format_get_bytes_per_pixel (buffer->format);

  if (format != buffer->format)
    {
      fish = babl_fish ((gpointer) buffer->format,
                        (gpointer) format);
    }

  {
    gint tiledy   = y + buffer_shift_y;
    gint tiledx   = x + buffer_shift_x;

    if (gegl_buffer_in_abyss( buffer, x, y))
      { /* in abyss */
        return;
      }
    else
      {
        gint      index_x = gegl_tile_index (tiledx, tile_width);
        gint      index_y = gegl_tile_index (tiledy, tile_height);
        GeglTile *tile     = NULL;

        if (buffer->hot_tile &&
            buffer->hot_tile->x == index_x &&
            buffer->hot_tile->y == index_y)
          {
            tile = buffer->hot_tile;
          }
        else
          {
            if (buffer->hot_tile)
              {
                g_object_unref (buffer->hot_tile);
                buffer->hot_tile = NULL;
              }
            tile = gegl_tile_source_get_tile ((GeglTileSource *) (buffer),
                                              index_x, index_y,
                                              0);
          }

        if (tile)
          {
            gint    offsetx = gegl_tile_offset (tiledx, tile_width);
            gint    offsety = gegl_tile_offset (tiledy, tile_height);
            guchar *tp;

            gegl_tile_lock (tile, GEGL_TILE_LOCK_WRITE);

            tp = gegl_tile_get_data (tile) +
                 (offsety * tile_width + offsetx) * px_size;
            if (fish)
              babl_process (fish, buf, tp, 1);
            else
              memcpy (tp, buf, bpx_size);

            gegl_tile_unlock (tile);
            buffer->hot_tile = tile;
          }
      }
  }
}

static inline void
gegl_buffer_gpu_set_pixel (GeglBuffer           *buffer,
                           gint                  x,
                           gint                  y,
                           const GeglGpuTexture *src)
{
  x += buffer->shift_x;
  y += buffer->shift_y;

  if (gegl_buffer_in_abyss (buffer, x, y))
    return;
  else
    {
      gint tile_width  = buffer->tile_storage->tile_width;
      gint tile_height = buffer->tile_storage->tile_height;

      GeglTile *tile    = NULL;
      gint      index_x = gegl_tile_index (x, tile_width);
      gint      index_y = gegl_tile_index (y, tile_height);

      if (buffer->hot_tile
          && buffer->hot_tile->x == index_x
          && buffer->hot_tile->y == index_y)
        {
          tile = buffer->hot_tile;
        }
      else
        {
          if (buffer->hot_tile)
            {
              g_object_unref (buffer->hot_tile);
              buffer->hot_tile = NULL;
            }

          tile = gegl_tile_source_get_tile ((GeglTileSource *) buffer,
                                            index_x,
                                            index_y,
                                            0);
        }

      if (tile != NULL)
        {
          gint offset_x = gegl_tile_offset (x, tile_width);
          gint offset_y = gegl_tile_offset (y, tile_height);

          GeglGpuTexture *dest;
          GeglRectangle   one_pixel = {0, 0, 1, 1};

          gegl_tile_lock (tile, GEGL_TILE_LOCK_GPU_WRITE);
          dest = gegl_tile_get_gpu_data (tile);
          gegl_gpu_texture_copy (src,
                                 &one_pixel,
                                 dest,
                                 offset_x,
                                 offset_y);
          gegl_tile_unlock (tile);

          buffer->hot_tile = tile;
        }
    }
}

static inline void
gegl_buffer_get_pixel (GeglBuffer *buffer,
                       gint        x,
                       gint        y,
                       const Babl *format,
                       gpointer    data)
{
  guchar *buf         = data;
  gint    tile_width  = buffer->tile_storage->tile_width;
  gint    tile_height = buffer->tile_storage->tile_height;
  gint    bpx_size    = babl_format_get_bytes_per_pixel (format);
  Babl   *fish        = NULL;

  gint  buffer_shift_x = buffer->shift_x;
  gint  buffer_shift_y = buffer->shift_y;
  gint  px_size        = babl_format_get_bytes_per_pixel (buffer->format);

  if (format != buffer->format)
    {
      fish = babl_fish ((gpointer) buffer->format,
                        (gpointer) format);
    }

  {
    gint tiledy = y + buffer_shift_y;
    gint tiledx = x + buffer_shift_x;

    if (gegl_buffer_in_abyss (buffer, x, y))
      { /* in abyss */
        memset (buf, 0x00, bpx_size);
        return;
      }
    else
      {
        gint      index_x = gegl_tile_index (tiledx, tile_width);
        gint      index_y = gegl_tile_index (tiledy, tile_height);
        GeglTile *tile     = NULL;

        if (buffer->hot_tile &&
            buffer->hot_tile->x == index_x &&
            buffer->hot_tile->y == index_y)
          {
            tile = buffer->hot_tile;
          }
        else
          {
            if (buffer->hot_tile)
              {
                g_object_unref (buffer->hot_tile);
                buffer->hot_tile = NULL;
              }
            tile = gegl_tile_source_get_tile ((GeglTileSource *) (buffer),
                                              index_x, index_y,
                                              0);
          }

        if (tile)
          {
            gint    offsetx = gegl_tile_offset (tiledx, tile_width);
            gint    offsety = gegl_tile_offset (tiledy, tile_height);
            guchar *tp;

            gegl_tile_lock (tile, GEGL_TILE_LOCK_READ);

            tp = gegl_tile_get_data (tile)
              + (offsety * tile_width + offsetx) * px_size;

            if (fish)
              babl_process (fish, tp, buf, 1);
            else
              memcpy (buf, tp, px_size);

            gegl_tile_unlock (tile);

            /*g_object_unref (tile);*/
            buffer->hot_tile = tile;
          }
      }
  }
}

static inline void
gegl_buffer_gpu_get_pixel (GeglBuffer     *buffer,
                           gint            x,
                           gint            y,
                           GeglGpuTexture *dest)
{
  x += buffer->shift_x;
  y += buffer->shift_y;

  if (gegl_buffer_in_abyss (buffer, x, y))
    {
      GeglRectangle one_pixel = {0, 0, 1, 1};
      gegl_gpu_texture_clear (dest, &one_pixel);
      return;
    }
  else
    {
      gint tile_width  = buffer->tile_storage->tile_width;
      gint tile_height = buffer->tile_storage->tile_height;

      GeglTile *tile    = NULL;
      gint      index_x = gegl_tile_index (x, tile_width);
      gint      index_y = gegl_tile_index (y, tile_height);

      if (buffer->hot_tile &&
          buffer->hot_tile->x == index_x &&
          buffer->hot_tile->y == index_y)
        {
          tile = buffer->hot_tile;
        }
      else
        {
          if (buffer->hot_tile)
            {
              g_object_unref (buffer->hot_tile);
              buffer->hot_tile = NULL;
            }

          tile = gegl_tile_source_get_tile ((GeglTileSource *) buffer,
                                            index_x,
                                            index_y,
                                            0);
        }

      if (tile != NULL)
        {
          gint offset_x = gegl_tile_offset (x, tile_width);
          gint offset_y = gegl_tile_offset (y, tile_height);

          GeglGpuTexture *src;
          GeglRectangle   one_pixel = {offset_x, offset_y, 1, 1};

          gegl_tile_lock (tile, GEGL_TILE_LOCK_GPU_READ);
          src = gegl_tile_get_gpu_data (tile);
          gegl_gpu_texture_copy (src,
                                 &one_pixel,
                                 dest,
                                 0,
                                 0);
          gegl_tile_unlock (tile);

          buffer->hot_tile = tile;
        }
    }
}

/* flush any unwritten data (flushes the hot-cache of a single
 * tile used by gegl_buffer_set for 1x1 pixel sized rectangles
 */
void
gegl_buffer_flush (GeglBuffer *buffer)
{
  g_return_if_fail (GEGL_IS_BUFFER (buffer));

  if (buffer->hot_tile)
    {
      g_object_unref (buffer->hot_tile);
      buffer->hot_tile = NULL;
    }
  if ((GeglBufferHeader*)(gegl_buffer_backend (buffer)->header))
    {
      ((GeglBufferHeader*)(gegl_buffer_backend (buffer)->header))->x =buffer->extent.x;
      ((GeglBufferHeader*)(gegl_buffer_backend (buffer)->header))->y =buffer->extent.y;
      ((GeglBufferHeader*)(gegl_buffer_backend (buffer)->header))->width =buffer->extent.width;
      ((GeglBufferHeader*)(gegl_buffer_backend (buffer)->header))->height =buffer->extent.height;
    }

  gegl_tile_source_command (GEGL_TILE_SOURCE (buffer),
                            GEGL_TILE_FLUSH, 0,0,0,NULL);
}



static void inline
gegl_buffer_iterate (GeglBuffer          *buffer,
                     const GeglRectangle *roi, /* or NULL for extent */
                     guchar              *buf,
                     gint                 rowstride,
                     gboolean             write,
                     const Babl          *format,
                     gint                 level)
{
  gint  tile_width  = buffer->tile_storage->tile_width;
  gint  tile_height = buffer->tile_storage->tile_height;
  gint  px_size     = babl_format_get_bytes_per_pixel (buffer->format);
  gint  bpx_size    = babl_format_get_bytes_per_pixel (format);
  gint  tile_stride = px_size * tile_width;
  gint  buf_stride;
  gint  bufy = 0;
  Babl *fish;

  gint  buffer_shift_x = buffer->shift_x;
  gint  buffer_shift_y = buffer->shift_y;

  gint  width          = buffer->extent.width;
  gint  height         = buffer->extent.height;
  gint  buffer_x       = buffer->extent.x + buffer_shift_x;
  gint  buffer_y       = buffer->extent.y + buffer_shift_y;

  gint  buffer_abyss_x = buffer->abyss.x + buffer_shift_x;
  gint  buffer_abyss_y = buffer->abyss.y + buffer_shift_y;
  gint  abyss_x_total  = buffer_abyss_x + buffer->abyss.width;
  gint  abyss_y_total  = buffer_abyss_y + buffer->abyss.height;
  gint  i;
  gint  factor = 1;

  /* roi specified, override buffers extent */
  if (roi)
    {
      width  = roi->width;
      height = roi->height;
      buffer_x = roi->x + buffer_shift_x;
      buffer_y = roi->y + buffer_shift_y;
    }

  for (i = 0; i < level; i++)
    {
      factor *= 2;
    }

  buffer_abyss_x /= factor;
  buffer_abyss_y /= factor;
  abyss_x_total  /= factor;
  abyss_y_total  /= factor;
  buffer_x       /= factor;
  buffer_y       /= factor;
  width          /= factor;
  height         /= factor;

  buf_stride = width * bpx_size;
  if (rowstride != GEGL_AUTO_ROWSTRIDE)
    buf_stride = rowstride;

  if (format == buffer->format)
    {
      fish = NULL;
    }
  else
    {
      if (write)
        {
          fish = babl_fish ((gpointer) format,
                            (gpointer) buffer->format);
        }
      else
        {
          fish = babl_fish ((gpointer) buffer->format,
                            (gpointer) format);
        }
    }

  while (bufy < height)
    {
      gint tiledy  = buffer_y + bufy;
      gint offsety = gegl_tile_offset (tiledy, tile_height);



      gint bufx    = 0;

      if (!(buffer_y + bufy + (tile_height) >= buffer_abyss_y &&
            buffer_y + bufy < abyss_y_total))
        { /* entire row of tiles is in abyss */
          if (!write)
            {
              gint    row;
              gint    y  = bufy;
              guchar *bp = buf + ((bufy) * width) * bpx_size;

              for (row = offsety;
                   row < tile_height && y < height;
                   row++, y++)
                {
                  memset (bp, 0x00, buf_stride);
                  bp += buf_stride;
                }
            }
        }
      else

        while (bufx < width)
          {
            gint    tiledx  = buffer_x + bufx;
            gint    offsetx = gegl_tile_offset (tiledx, tile_width);
            gint    pixels;
            guchar *bp;

            bp = buf + bufy * buf_stride + bufx * bpx_size;

            if (width + offsetx - bufx < tile_width)
              pixels = (width + offsetx - bufx) - offsetx;
            else
              pixels = tile_width - offsetx;


            if (!(buffer_x + bufx + tile_width >= buffer_abyss_x &&
                  buffer_x + bufx < abyss_x_total))
              { /* entire tile is in abyss */
                if (!write)
                  {
                    gint row;
                    gint y = bufy;

                    for (row = offsety;
                         row < tile_height && y < height;
                         row++, y++)
                      {
                        memset (bp, 0x00, pixels * bpx_size);
                        bp += buf_stride;
                      }
                  }
              }
            else
              {
                guchar   *tile_base, *tp;
                GeglTile *tile = gegl_tile_source_get_tile ((GeglTileSource *) (buffer),
                                                           gegl_tile_index (tiledx, tile_width),
                                                           gegl_tile_index (tiledy, tile_height),
                                                           level);

                gint lskip = (buffer_abyss_x) - (buffer_x + bufx);
                /* gap between left side of tile, and abyss */
                gint rskip = (buffer_x + bufx + pixels) - abyss_x_total;
                /* gap between right side of tile, and abyss */

                if (lskip < 0)
                  lskip = 0;
                if (lskip > pixels)
                  lskip = pixels;
                if (rskip < 0)
                  rskip = 0;
                if (rskip > pixels)
                  rskip = pixels;

                if (!tile)
                  {
                    g_warning ("didn't get tile, trying to continue");
                    bufx += (tile_width - offsetx);
                    continue;
                  }

                if (write)
                  gegl_tile_lock (tile, GEGL_TILE_LOCK_WRITE);
                else
                  gegl_tile_lock (tile, GEGL_TILE_LOCK_READ);

                tile_base = gegl_tile_get_data (tile);
                tp        = ((guchar *) tile_base) + (offsety * tile_width + offsetx) * px_size;

                if (write)
                  {
                    gint row;
                    gint y = bufy;


                    if (fish)
                      {
                        for (row = offsety;
                             row < tile_height &&
                             y < height &&
                             buffer_y + y < abyss_y_total;
                             row++, y++)
                          {

                            if (buffer_y + y >= buffer_abyss_y &&
                                buffer_y + y < abyss_y_total)
                              {
                                babl_process (fish, bp + lskip * bpx_size, tp + lskip * px_size,
                                 pixels - lskip - rskip);
                              }

                            tp += tile_stride;
                            bp += buf_stride;
                          }
                      }
                    else
                      {
                        for (row = offsety;
                             row < tile_height && y < height;
                             row++, y++)
                          {

                            if (buffer_y + y >= buffer_abyss_y &&
                                buffer_y + y < abyss_y_total)
                              {

                                memcpy (tp + lskip * px_size, bp + lskip * px_size,
                                      (pixels - lskip - rskip) * px_size);
                              }

                            tp += tile_stride;
                            bp += buf_stride;
                          }
                      }
                  }
                else /* read */
                  {
                    gint row;
                    gint y = bufy;

                    for (row = offsety;
                         row < tile_height && y < height;
                         row++, y++)
                      {
                        if (buffer_y + y >= buffer_abyss_y &&
                            buffer_y + y < abyss_y_total)
                          {
                            if (fish)
                              babl_process (fish, tp, bp, pixels);
                            else
                              memcpy (bp, tp, pixels * px_size);
                          }
                        else
                          {
                            /* entire row in abyss */
                            memset (bp, 0x00, pixels * bpx_size);
                          }

                          /* left hand zeroing of abyss in tile */
                        if (lskip)
                          {
                            memset (bp, 0x00, bpx_size * lskip);
                          }

                        /* right side zeroing of abyss in tile */
                        if (rskip)
                          {
                            memset (bp + (pixels - rskip) * bpx_size, 0x00, bpx_size * rskip);
                          }
                        tp += tile_stride;
                        bp += buf_stride;
                      }
                  }
                gegl_tile_unlock (tile);
                g_object_unref (tile);
              }
            bufx += (tile_width - offsetx);
          }
      bufy += (tile_height - offsety);
    }
}

static void inline
gegl_buffer_gpu_iterate (GeglBuffer          *buffer,
                         const GeglRectangle *roi, /* or NULL for extent */
                         GeglGpuTexture      *texture,
                         gboolean             write,
                         gint                 level)
{
  GeglRectangle original_rect;
  GeglRectangle abyss;
  GeglRectangle final_rect;

  gint factor = 1;

  gint tile_width  = buffer->tile_storage->tile_width;
  gint tile_height = buffer->tile_storage->tile_height;

  gint last_x;
  gint last_y;

  gint first_tile_index_x;
  gint first_tile_index_y;
  gint last_tile_index_x;
  gint last_tile_index_y;

  gint first_tile_offset_x;
  gint first_tile_offset_y;
  gint last_tile_offset_x;
  gint last_tile_offset_y;

  gint first_texture_x;
  gint first_texture_y;

  gint tile_index_y;
  gint tile_offset_y;
  gint texture_y;
  gint copy_area_height;

  gint tile_index_x;
  gint tile_offset_x;
  gint texture_x;
  gint copy_area_width;

  gint cnt;

  for (cnt = 0; cnt < level; cnt++)
    factor *= 2;

  if (roi != NULL)
    {
      original_rect.x      = (roi->x + buffer->shift_x) / factor;
      original_rect.y      = (roi->y + buffer->shift_y) / factor;
      original_rect.width  = roi->width / factor;
      original_rect.height = roi->height / factor;
    }
  else
    {
      original_rect.x      = (buffer->extent.x + buffer->shift_x) / factor;
      original_rect.y      = (buffer->extent.y + buffer->shift_y) / factor;
      original_rect.width  = buffer->extent.width / factor;
      original_rect.height = buffer->extent.height / factor;
    }

  abyss.x      = (buffer->abyss.x + buffer->shift_x) / factor;
  abyss.y      = (buffer->abyss.y + buffer->shift_y) / factor;
  abyss.width  = buffer->abyss.width / factor;
  abyss.height = buffer->abyss.height / factor;

  gegl_rectangle_intersect (&final_rect, &abyss, &original_rect);

  last_x = final_rect.x + (final_rect.width - 1);
  last_y = final_rect.y + (final_rect.height - 1);

  first_tile_index_x = gegl_tile_index (final_rect.x, tile_width);
  first_tile_index_y = gegl_tile_index (final_rect.y, tile_height);
  last_tile_index_x  = gegl_tile_index (last_x, tile_width);
  last_tile_index_y  = gegl_tile_index (last_y, tile_height);

  first_tile_offset_x = gegl_tile_offset (final_rect.x, tile_width);
  first_tile_offset_y = gegl_tile_offset (final_rect.y, tile_height);
  last_tile_offset_x  = gegl_tile_offset (last_x, tile_width);
  last_tile_offset_y  = gegl_tile_offset (last_y, tile_height);

  first_texture_x = final_rect.x - original_rect.x;
  first_texture_y = final_rect.y - original_rect.y;

  for (tile_index_y       = first_tile_index_y,
         tile_offset_y    = first_tile_offset_y,
         texture_y        = first_texture_y,
         copy_area_height = tile_index_y < last_tile_index_y
                              ? tile_height - tile_offset_y
                              : last_tile_offset_y - tile_offset_y + 1;

       tile_index_y <= last_tile_index_y;

       tile_index_y++,
         tile_offset_y    = 0,
         texture_y       += copy_area_height,
         copy_area_height = tile_index_y < last_tile_index_y
                              ? tile_height
                              : last_tile_offset_y + 1)
    {
      for (tile_index_x      = first_tile_index_x,
             tile_offset_x   = first_tile_offset_x,
             texture_x       = first_texture_x,
             copy_area_width = tile_index_x < last_tile_index_x
                                 ? tile_width - tile_offset_x
                                 : last_tile_offset_x - tile_offset_x + 1;

           tile_index_x <= last_tile_index_x;

           tile_index_x++,
             tile_offset_x   = 0,
             texture_x      += copy_area_width,
             copy_area_width = tile_index_x < last_tile_index_x
                                 ? tile_width
                                 : last_tile_offset_x + 1)
        {
          GeglTile *tile = gegl_tile_source_get_tile ((GeglTileSource *) buffer,
                                                      tile_index_x,
                                                      tile_index_y,
                                                      level);

          if (tile == NULL)
            {
              g_warning ("didn't get tile, trying to continue");
              continue;
            }

          if (write)
            {
              GeglGpuTexture *dest;
              GeglRectangle   temp_rect = {
                                            texture_x,
                                            texture_y,
                                            copy_area_width,
                                            copy_area_height
                                          };

              gegl_tile_lock (tile, GEGL_TILE_LOCK_GPU_WRITE);

              dest = gegl_tile_get_gpu_data (tile);
              gegl_gpu_texture_copy (texture,
                                     &temp_rect,
                                     dest,
                                     tile_offset_x,
                                     tile_offset_y);

              gegl_tile_unlock (tile);
            }
          else
            {
              GeglGpuTexture *src;
              GeglRectangle   temp_rect = {
                                            tile_offset_x,
                                            tile_offset_y,
                                            copy_area_width,
                                            copy_area_height
                                          };

              gegl_tile_lock (tile, GEGL_TILE_LOCK_GPU_READ);

              src = gegl_tile_get_gpu_data (tile);
              gegl_gpu_texture_copy (src,
                                     &temp_rect,
                                     texture,
                                     texture_x,
                                     texture_y);

              gegl_tile_unlock (tile);
            }
        }
    }

  /* "read" from abyss */
    {
      /* original requested rectangle on the texture */
      GeglRectangle abyss_rect   = {
                                     0,
                                     0,
                                     original_rect.width,
                                     original_rect.height
                                   };

      /* rectangle on the texture written with actual pixel
       * data from the buffer
       */
      GeglRectangle texture_rect = {
                                     first_texture_x,
                                     first_texture_y,
                                     final_rect.width,
                                     final_rect.height
                                   };

      if (!write && !gegl_rectangle_equal (&abyss_rect, &texture_rect))
        {
          GeglRegion *abyss_region   = gegl_region_rectangle (&abyss_rect);
          GeglRegion *texture_region = gegl_region_rectangle (&texture_rect);

          GeglRectangle *abyss_rects;
          gint rect_count;

          gegl_region_subtract (abyss_region, texture_region);
          gegl_region_destroy  (texture_region);

          gegl_region_get_rectangles (abyss_region, &abyss_rects, &rect_count);

          for (cnt = 0; cnt < rect_count; cnt++)
            gegl_gpu_texture_clear (texture, &abyss_rects[cnt]);

          g_free (abyss_rects);
          gegl_region_destroy (abyss_region);
        }
    }
}

void
gegl_buffer_set (GeglBuffer          *buffer,
                 const GeglRectangle *rect,
                 const Babl          *format,
                 void                *src,
                 gint                 rowstride)
{
  g_return_if_fail (GEGL_IS_BUFFER (buffer));

#if ENABLE_MP
  g_static_rec_mutex_lock (&mutex);
#endif
  gegl_buffer_lock (buffer);

  if (format == NULL)
    format = buffer->format;

  if (rect && rect->width == 1 && rect->height == 1) /* fast path */
    {
      gegl_buffer_set_pixel (buffer, rect->x, rect->y, format, src);
    }
  else
    {
      gegl_buffer_iterate (buffer, rect, src, rowstride, TRUE, format, 0);
    }

  if (gegl_buffer_is_shared(buffer))
    {
      gegl_buffer_flush (buffer);
    }
  gegl_buffer_unlock (buffer); /* XXX: should this happen before flush? */
#if ENABLE_MP
  g_static_rec_mutex_unlock (&mutex);
#endif
}

void
gegl_buffer_gpu_set (GeglBuffer           *buffer,
                     const GeglRectangle  *rect,
                     const GeglGpuTexture *src)
{
  g_return_if_fail (GEGL_IS_BUFFER (buffer));

#if ENABLE_MP
  g_static_rec_mutex_lock (&mutex);
#endif
  gegl_buffer_lock (buffer);

  if (rect != NULL && rect->width == 1 && rect->height == 1) /* fast path */
    gegl_buffer_gpu_set_pixel (buffer, rect->x, rect->y, src);
  else
    gegl_buffer_gpu_iterate (buffer, rect, (GeglGpuTexture *) src, TRUE, 0);

  if (gegl_buffer_is_shared (buffer))
    gegl_buffer_flush (buffer);

  gegl_buffer_unlock (buffer); /* XXX: should this happen before flush? */
#if ENABLE_MP
  g_static_rec_mutex_unlock (&mutex);
#endif
}



#if 0

/*
 *  slow nearest neighbour resampler that seems to be
 *  completely correct.
 */

static void
resample_nearest (void   *dest_buf,
                  void   *source_buf,
                  gint    dest_w,
                  gint    dest_h,
                  gint    source_w,
                  gint    source_h,
                  gdouble offset_x,
                  gdouble offset_y,
                  gdouble scale,
                  gint    bpp,
                  gint    rowstride)
{
  gint x, y;

  if (rowstride == GEGL_AUTO_ROWSTRIDE)
     rowstride = dest_w * bpp;

  for (y = 0; y < dest_h; y++)
    {
      gint    sy;
      guchar *dst;
      guchar *src_base;

      sy = (y + offset_y) / scale;


      if (sy >= source_h)
        sy = source_h - 1;

      dst      = ((guchar *) dest_buf) + y * rowstride;
      src_base = ((guchar *) source_buf) + sy * source_w * bpp;

      for (x = 0; x < dest_w; x++)
        {
          gint    sx;
          guchar *src;
          sx = (x + offset_x) / scale;

          if (sx >= source_w)
            sx = source_w - 1;
          src = src_base + sx * bpp;

          memcpy (dst, src, bpp);
          dst += bpp;
        }
    }
}
#endif

/* Optimized|obfuscated version of the nearest neighbour resampler
 * XXX: seems to contains some very slight inprecision in the rendering.
 */
static void
resample_nearest (void   *dest_buf,
                  void   *source_buf,
                  gint    dest_w,
                  gint    dest_h,
                  gint    source_w,
                  gint    source_h,
                  gdouble offset_x,
                  gdouble offset_y,
                  gdouble scale,
                  gint    bpp,
                  gint    rowstride)
{
  gint x, y;
  guint xdiff, ydiff, xstart, sy;

  if (rowstride == GEGL_AUTO_ROWSTRIDE)
     rowstride = dest_w * bpp;

  xdiff = 65536 / scale;
  ydiff = 65536 / scale;
  xstart = (offset_x * 65536) / scale;
  sy = (offset_y * 65536) / scale;

  for (y = 0; y < dest_h; y++)
    {
      guchar *dst;
      guchar *src_base;
      guint sx;
      guint px = 0;
      guchar *src;

      if (sy >= source_h << 16)
        sy = (source_h - 1) << 16;

      dst      = ((guchar *) dest_buf) + y * rowstride;
      src_base = ((guchar *) source_buf) + (sy >> 16) * source_w * bpp;

      sx = xstart;
      src = src_base;

      /* this is the loop that is actually properly optimized,
       * portions of the setup is done for all the rows outside the y
       * loop as well */
      for (x = 0; x < dest_w; x++)
        {
          gint diff;
          gint ssx = sx>>16;
          if ( (diff = ssx - px) > 0)
            {
              if (ssx < source_w)
                src += diff * bpp;
              px += diff;
            }
          memcpy (dst, src, bpp);
          dst += bpp;
          sx += xdiff;
        }
      sy += ydiff;
    }
}

static inline void
box_filter (guint          left_weight,
            guint          center_weight,
            guint          right_weight,
            guint          top_weight,
            guint          middle_weight,
            guint          bottom_weight,
            guint          sum,
            const guchar **src,   /* the 9 surrounding source pixels */
            guchar        *dest,
            gint           components)
{
  /* NOTE: this box filter presumes pre-multiplied alpha, if there
   * is alpha.
   */
   guint lt, lm, lb;
   guint ct, cm, cb;
   guint rt, rm, rb;

   lt = left_weight * top_weight;
   lm = left_weight * middle_weight;
   lb = left_weight * bottom_weight;
   ct = center_weight * top_weight;
   cm = center_weight * middle_weight;
   cb = center_weight * bottom_weight;
   rt = right_weight * top_weight;
   rm = right_weight * middle_weight;
   rb = right_weight * bottom_weight;

#define docomponent(i) \
      dest[i] = (src[0][i] * lt + src[3][i] * lm + src[6][i] * lb + \
                 src[1][i] * ct + src[4][i] * cm + src[7][i] * cb + \
                 src[2][i] * rt + src[5][i] * rm + src[8][i] * rb) / sum
  switch (components)
    {
      case 5: docomponent(4);
      case 4: docomponent(3);
      case 3: docomponent(2);
      case 2: docomponent(1);
      case 1: docomponent(0);
    }
#undef docomponent
}

static void
resample_boxfilter_u8 (void   *dest_buf,
                       void   *source_buf,
                       gint    dest_w,
                       gint    dest_h,
                       gint    source_w,
                       gint    source_h,
                       gdouble offset_x,
                       gdouble offset_y,
                       gdouble scale,
                       gint    components,
                       gint    rowstride)
{
  gint x, y;
  gint iscale      = scale * 256;
  gint s_rowstride = source_w * components;
  gint d_rowstride = dest_w * components;

  gint          footprint_x;
  gint          footprint_y;
  guint         foosum;

  guint         left_weight;
  guint         center_weight;
  guint         right_weight;

  guint         top_weight;
  guint         middle_weight;
  guint         bottom_weight;

  footprint_y = (1.0 / scale) * 256;
  footprint_x = (1.0 / scale) * 256;
  foosum = footprint_x * footprint_y;

  if (rowstride != GEGL_AUTO_ROWSTRIDE)
    d_rowstride = rowstride;

  for (y = 0; y < dest_h; y++)
    {
      gint    sy;
      gint    dy;
      guchar *dst;
      const guchar *src_base;
      gint sx;
      gint xdelta;

      sy = ((y + offset_y) * 65536) / iscale;

      if (sy >= (source_h - 1) << 8)
        sy = (source_h - 2) << 8;/* is this the right thing to do? */

      dy = sy & 255;

      dst      = ((guchar *) dest_buf) + y * d_rowstride;
      src_base = ((guchar *) source_buf) + (sy >> 8) * s_rowstride;

      if (dy > footprint_y / 2)
        top_weight = 0;
      else
        top_weight = footprint_y / 2 - dy;

      if (0xff - dy > footprint_y / 2)
        bottom_weight = 0;
      else
        bottom_weight = footprint_y / 2 - (0xff - dy);

      middle_weight = footprint_y - top_weight - bottom_weight;

      sx = (offset_x *65536) / iscale;
      xdelta = 65536/iscale;

      /* XXX: needs quite a bit of optimization */
      for (x = 0; x < dest_w; x++)
        {
          gint          dx;
          const guchar *src[9];

          /*sx = (x << 16) / iscale;*/
          dx = sx & 255;

          if (dx > footprint_x / 2)
            left_weight = 0;
          else
            left_weight = footprint_x / 2 - dx;

          if (0xff - dx > footprint_x / 2)
            right_weight = 0;
          else
            right_weight = footprint_x / 2 - (0xff - dx);

          center_weight = footprint_x - left_weight - right_weight;

          src[4] = src_base + (sx >> 8) * components;
          src[1] = src[4] - s_rowstride;
          src[7] = src[4] + s_rowstride;

          src[2] = src[1] + components;
          src[5] = src[4] + components;
          src[8] = src[7] + components;

          src[0] = src[1] - components;
          src[3] = src[4] - components;
          src[6] = src[7] - components;

          if ((sx >>8) - 1<0)
            {
              src[0]=src[1];
              src[3]=src[4];
              src[6]=src[7];
            }
          if ((sy >> 8) - 1 < 0)
            {
              src[0]=src[3];
              src[1]=src[4];
              src[2]=src[5];
            }
          if ((sx >>8) + 1 >= source_w)
            {
              src[2]=src[1];
              src[5]=src[4];
              src[8]=src[7];
              break;
            }
          if ((sy >> 8) + 1 >= source_h)
            {
              src[6]=src[3];
              src[7]=src[4];
              src[8]=src[5];
            }

          box_filter (left_weight,
                      center_weight,
                      right_weight,
                      top_weight,
                      middle_weight,
                      bottom_weight,
                      foosum,
                      src,   /* the 9 surrounding source pixels */
                      dst,
                      components);


          dst += components;
          sx += xdelta;
        }
    }
}

void
gegl_buffer_get (GeglBuffer          *buffer,
                 gdouble              scale,
                 const GeglRectangle *rect,
                 const Babl          *format,
                 gpointer             dest_buf,
                 gint                 rowstride)
{
  g_return_if_fail (GEGL_IS_BUFFER (buffer));
#if ENABLE_MP
  g_static_rec_mutex_lock (&mutex);
#endif

  if (format == NULL)
    format = buffer->format;

  if (scale == 1.0 &&
      rect &&
      rect->width == 1 &&
      rect->height == 1)  /* fast path */
    {
      gegl_buffer_get_pixel (buffer, rect->x, rect->y, format, dest_buf);
#if ENABLE_MP
      g_static_rec_mutex_unlock (&mutex);
#endif
      return;
    }

  if (!rect && scale == 1.0)
    {
      gegl_buffer_iterate (buffer, NULL, dest_buf, rowstride, FALSE, format, 0);
#if ENABLE_MP
      g_static_rec_mutex_unlock (&mutex);
#endif
      return;
    }
  if (rect->width == 0 ||
      rect->height == 0)
    {
#if ENABLE_MP
      g_static_rec_mutex_unlock (&mutex);
#endif
      return;
    }
  if (GEGL_FLOAT_EQUAL (scale, 1.0))
    {
      gegl_buffer_iterate (buffer, rect, dest_buf, rowstride, FALSE, format, 0);
#if ENABLE_MP
      g_static_rec_mutex_unlock (&mutex);
#endif
      return;
    }
  else
    {
      gint          level       = 0;
      gint          buf_width   = rect->width / scale;
      gint          buf_height  = rect->height / scale;
      gint          bpp         = babl_format_get_bytes_per_pixel (format);
      GeglRectangle sample_rect;
      void         *sample_buf;
      gint          factor = 1;
      gdouble       offset_x;
      gdouble       offset_y;

      sample_rect.x = floor(rect->x/scale);
      sample_rect.y = floor(rect->y/scale);
      sample_rect.width = buf_width;
      sample_rect.height = buf_height;

      while (scale <= 0.5)
        {
          scale  *= 2;
          factor *= 2;
          level++;
        }

      buf_width  /= factor;
      buf_height /= factor;

      /* ensure we always have some data to sample from */
      sample_rect.width  += factor * 2;
      sample_rect.height += factor * 2;
      buf_width          += 2;
      buf_height         += 2;

      offset_x = rect->x-floor(rect->x/scale) * scale;
      offset_y = rect->y-floor(rect->y/scale) * scale;

      sample_buf = g_malloc (buf_width * buf_height * bpp);
      gegl_buffer_iterate (buffer, &sample_rect, sample_buf, GEGL_AUTO_ROWSTRIDE, FALSE, format, level);
#if 1
  /* slows testing of rendering code speed to much for now and
   * no time to make a fast implementation
   */

      if (babl_format_get_type (format, 0) == babl_type ("u8")
          && !(level == 0 && scale > 1.99))
        { /* do box-filter resampling if we're 8bit (which projections are) */

          /* XXX: use box-filter also for > 1.99 when testing and probably
           * later, there are some bugs when doing so
           */
          resample_boxfilter_u8 (dest_buf,
                                 sample_buf,
                                 rect->width,
                                 rect->height,
                                 buf_width,
                                 buf_height,
                                 offset_x,
                                 offset_y,
                                 scale,
                                 bpp,
                                 rowstride);
        }
      else
#endif
        {
          resample_nearest (dest_buf,
                            sample_buf,
                            rect->width,
                            rect->height,
                            buf_width,
                            buf_height,
                            offset_x,
                            offset_y,
                            scale,
                            bpp,
                            rowstride);
        }
      g_free (sample_buf);
    }
#if ENABLE_MP
  g_static_rec_mutex_unlock (&mutex);
#endif
}

void
gegl_buffer_gpu_get (GeglBuffer          *buffer,
                     gdouble              scale,
                     const GeglRectangle *rect,
                     GeglGpuTexture      *dest)
{
  g_return_if_fail (GEGL_IS_BUFFER (buffer));
#if ENABLE_MP
  g_static_rec_mutex_lock (&mutex);
#endif

  if (GEGL_FLOAT_EQUAL (scale, 1.0)
      && rect != NULL
      && rect->width == 1
      && rect->height == 1)  /* fast path */
    {
      gegl_buffer_gpu_get_pixel (buffer, rect->x, rect->y, dest);
#if ENABLE_MP
      g_static_rec_mutex_unlock (&mutex);
#endif
      return;
    }

  if (rect == NULL && GEGL_FLOAT_EQUAL (scale, 1.0))
    {
      gegl_buffer_gpu_iterate (buffer, NULL, dest, FALSE, 0);
#if ENABLE_MP
      g_static_rec_mutex_unlock (&mutex);
#endif
      return;
    }

  if (rect->width == 0 || rect->height == 0)
    {
#if ENABLE_MP
      g_static_rec_mutex_unlock (&mutex);
#endif
      return;
    }

  if (GEGL_FLOAT_EQUAL (scale, 1.0))
    {
      gegl_buffer_gpu_iterate (buffer, rect, dest, FALSE, 0);
#if ENABLE_MP
      g_static_rec_mutex_unlock (&mutex);
#endif
      return;
    }
  else
    {
      /* mostly, a direct copy from gegl_buffer_get()
       *
       * TODO: implement GPU-based resamplers to prevent reading back
       *       to the CPU like what we are doing here
       */
      gint level      = 0;
      gint buf_width  = rect->width / scale;
      gint buf_height = rect->height / scale;
      gint bpp        = babl_format_get_bytes_per_pixel (dest->format);

      void         *dest_buf;
      void         *sample_buf;
      GeglRectangle sample_rect;

      gint factor = 1;

      gdouble offset_x;
      gdouble offset_y;

      sample_rect.x      = floor (rect->x/scale);
      sample_rect.y      = floor (rect->y/scale);
      sample_rect.width  = buf_width;
      sample_rect.height = buf_height;

      while (scale <= 0.5)
        {
          scale  *= 2;
          factor *= 2;
          level++;
        }

      buf_width  /= factor;
      buf_height /= factor;

      /* ensure we always have some data to sample from */
      sample_rect.width  += factor * 2;
      sample_rect.height += factor * 2;
      buf_width          += 2;
      buf_height         += 2;

      offset_x = rect->x - floor (rect->x / scale) * scale;
      offset_y = rect->y - floor (rect->y / scale) * scale;

      dest_buf = g_malloc (dest->width * dest->height * bpp);
      sample_buf = g_malloc (buf_width * buf_height * bpp);

      gegl_buffer_iterate (buffer,
                           &sample_rect,
                           sample_buf,
                           GEGL_AUTO_ROWSTRIDE,
                           FALSE,
                           dest->format,
                           level);
#if 1
      /* slows testing of rendering code speed to much for now and
       * no time to make a fast implementation
       */
      if (babl_format_get_type (dest->format, 0) == babl_type ("u8")
          && !(level == 0 && scale > 1.99))
        {
          /* do box-filter resampling if we're 8bit (which projections are) */

          /* XXX: use box-filter also for > 1.99 when testing and probably
           * later, there are some bugs when doing so
           */
          resample_boxfilter_u8 (dest_buf,
                                 sample_buf,
                                 rect->width,
                                 rect->height,
                                 buf_width,
                                 buf_height,
                                 offset_x,
                                 offset_y,
                                 scale,
                                 bpp,
                                 GEGL_AUTO_ROWSTRIDE);
        }
      else
#endif
        {
          resample_nearest (dest_buf,
                            sample_buf,
                            rect->width,
                            rect->height,
                            buf_width,
                            buf_height,
                            offset_x,
                            offset_y,
                            scale,
                            bpp,
                            GEGL_AUTO_ROWSTRIDE);
        }

      gegl_gpu_texture_set (dest, NULL, dest_buf, NULL);

      g_free (sample_buf);
      g_free (dest_buf);
    }

#if ENABLE_MP
  g_static_rec_mutex_unlock (&mutex);
#endif
}

const GeglRectangle *
gegl_buffer_get_abyss (GeglBuffer *buffer)
{
  g_return_val_if_fail (GEGL_IS_BUFFER (buffer), NULL);

  return &buffer->abyss;
}

void
gegl_buffer_sample (GeglBuffer       *buffer,
                    gdouble           x,
                    gdouble           y,
                    gdouble           scale,
                    gpointer          dest,
                    const Babl       *format,
                    GeglInterpolation interpolation)
{
  GType desired_type;
  g_return_if_fail (GEGL_IS_BUFFER (buffer));

/*#define USE_WORKING_SHORTCUT*/
#ifdef USE_WORKING_SHORTCUT
  gegl_buffer_get_pixel (buffer, x, y, format, dest);
  return;
#endif

#if ENABLE_MP
  g_static_rec_mutex_lock (&mutex);
#endif

  desired_type = gegl_sampler_type_from_interpolation (interpolation);

  if (buffer->sampler != NULL &&
      !G_TYPE_CHECK_INSTANCE_TYPE (buffer->sampler, desired_type))
    {
      g_object_unref(buffer->sampler);
      buffer->sampler = NULL;
    }

  /* look up appropriate sampler,. */
  if (buffer->sampler == NULL)
    {
      buffer->sampler = g_object_new (desired_type,
                                      "buffer", buffer,
                                      "format", format,
                                      NULL);
      gegl_sampler_prepare (buffer->sampler);
    }
  gegl_sampler_get (buffer->sampler, x, y, dest);

#if ENABLE_MP
  g_static_rec_mutex_unlock (&mutex);
#endif

  /* if none found, create a singleton sampler for this buffer,
   * a function to clean up the samplers set for a buffer should
   * also be provided */

  /* if (scale < 1.0) do decimation, possibly using pyramid instead */

}

void
gegl_buffer_sample_cleanup (GeglBuffer *buffer)
{
  g_return_if_fail (GEGL_IS_BUFFER (buffer));

  if (buffer->sampler)
    {
      g_object_unref (buffer->sampler);
      buffer->sampler = NULL;
    }
}


void
gegl_buffer_copy (GeglBuffer          *src,
                  const GeglRectangle *src_rect,
                  GeglBuffer          *dst,
                  const GeglRectangle *dst_rect)
{
  Babl         *fish;

  g_return_if_fail (GEGL_IS_BUFFER (src));
  g_return_if_fail (GEGL_IS_BUFFER (dst));

  if (!src_rect)
    {
      src_rect = gegl_buffer_get_extent (src);
    }

  if (!dst_rect)
    {
      dst_rect = src_rect;
    }

  fish = babl_fish (src->format, dst->format);

    {
      GeglRectangle dest_rect_r = *dst_rect;
      GeglBufferIterator *i;
      gint read;

      dest_rect_r.width = src_rect->width;
      dest_rect_r.height = src_rect->height;

      i = gegl_buffer_iterator_new (dst, &dest_rect_r, dst->format, GEGL_BUFFER_WRITE);
      read = gegl_buffer_iterator_add (i, src, src_rect, src->format, GEGL_BUFFER_READ);
      while (gegl_buffer_iterator_next (i))
        babl_process (fish, i->data[read], i->data[0], i->length);

      gegl_buffer_iterator_free (i);
    }
}

void
gegl_buffer_clear (GeglBuffer          *dst,
                   const GeglRectangle *dst_rect)
{
  GeglBufferIterator *i;
  gint                pxsize;

  g_return_if_fail (GEGL_IS_BUFFER (dst));

  if (!dst_rect)
    {
      dst_rect = gegl_buffer_get_extent (dst);
    }
  if (dst_rect->width == 0 ||
      dst_rect->height == 0)
    return;

  pxsize = babl_format_get_bytes_per_pixel (dst->format);

  /* FIXME: this can be even further optimized by special casing it so
   * that fully voided tiles are dropped.
   */
  i = gegl_buffer_iterator_new (dst, dst_rect, dst->format, GEGL_BUFFER_WRITE);
  while (gegl_buffer_iterator_next (i))
    {
      memset (((guchar*)(i->data[0])), 0, i->length * pxsize);
    }
  gegl_buffer_iterator_free (i);
}

GeglBuffer *
gegl_buffer_dup (GeglBuffer *buffer)
{
  GeglBuffer *new_buffer;

  g_return_val_if_fail (GEGL_IS_BUFFER (buffer), NULL);

  new_buffer = gegl_buffer_new (gegl_buffer_get_extent (buffer), buffer->format);
  gegl_buffer_copy (buffer, gegl_buffer_get_extent (buffer),
                    new_buffer, gegl_buffer_get_extent (buffer));
  return new_buffer;
}

void
gegl_buffer_sampler (GeglBuffer       *buffer,
                    gdouble           x,
                    gdouble           y,
                    gdouble           scale,
                    gpointer          dest,
                    const Babl       *format,
                    gpointer          sampler2)
{
  GeglSampler *sampler = sampler2;
  g_return_if_fail (GEGL_IS_BUFFER (buffer));
  g_return_if_fail (GEGL_IS_SAMPLER (sampler));

#if ENABLE_MP
  g_static_rec_mutex_lock (&mutex);
#endif

  gegl_sampler_get (sampler, x, y, dest);

#if ENABLE_MP
  g_static_rec_mutex_unlock (&mutex);
#endif
}
