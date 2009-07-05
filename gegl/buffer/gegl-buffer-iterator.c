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

typedef struct _GeglBufferTileIterator
{
  GeglBuffer      *buffer;
  GeglRectangle    roi;       /* the rectangular region we're iterating over */
  GeglTile        *tile;      /* current tile */
  GeglTileLockMode lock_mode;
  gint             max_size;  /* maximum data buffer needed, in bytes */

  GeglRectangle    subrect;   /* the rectangular subregion of data in the
                               * buffer represented by this scan.
                               */
  gpointer         sub_data;  /* pointer to the data as indicated by subrect */

  gint             next_col;  /* used internally */
  gint             next_row;  /* used internally */
} _GeglBufferTileIterator;

#define GEGL_BUFFER_SCAN_COMPATIBLE   128 /* should be integrated into enum */
#define GEGL_BUFFER_FORMAT_COMPATIBLE 256 /* should be integrated into enum */

#define DEBUG_DIRECT 0

typedef struct _GeglBufferIterator
{
  /* current region of interest */
  gint                    length; /* length of current data in pixels */
  gpointer                data    [GEGL_BUFFER_MAX_ITERABLES];
  GeglGpuTexture         *gpu_data[GEGL_BUFFER_MAX_ITERABLES];
  GeglRectangle           roi     [GEGL_BUFFER_MAX_ITERABLES];

  /* the following is private */
  gint                    iterable_count;
  gint                    iteration_no;
  GeglBuffer             *buffer  [GEGL_BUFFER_MAX_ITERABLES];
  GeglRectangle           rect    [GEGL_BUFFER_MAX_ITERABLES];
  const Babl             *format  [GEGL_BUFFER_MAX_ITERABLES];
  guint                   flags   [GEGL_BUFFER_MAX_ITERABLES];
  gpointer                buf     [GEGL_BUFFER_MAX_ITERABLES]; 
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
  g_assert (i);

  memset (i, 0, sizeof (_GeglBufferTileIterator));

  if (roi.width == 0 || roi.height == 0)
    g_error ("eeek");

  i->buffer    = buffer;
  i->roi       = roi;
  i->tile      = NULL;
  i->lock_mode = lock_mode;
  i->next_row  = 0;
  i->next_col  = 0;
  i->max_size  = i->buffer->tile_storage->tile_width
                 * i->buffer->tile_storage->tile_height;
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
  if (i->tile)
    {
      if (tile_width == i->subrect.width)
        gegl_tile_unlock (i->tile);

      g_object_unref (i->tile);
      i->tile = NULL;
    }

  if (i->next_col < i->roi.width)
    {
      /* return tile on this row */
      gint x = buffer_x + i->next_col;
      gint y = buffer_y + i->next_row;

      gint offset_x = gegl_tile_offset (x, tile_width);
      gint offset_y = gegl_tile_offset (y, tile_height);

      GeglRectangle rect = {offset_x, offset_y, 0, 0};

      if (i->roi.width + offset_x - i->next_col < tile_width)
        rect.width = (i->roi.width + offset_x - i->next_col) - offset_x;
      else
        rect.width = tile_width - offset_x;

      if (i->roi.height + offset_y - i->next_row < tile_height)
        rect.height = (i->roi.height + offset_y - i->next_row) - offset_y;
      else
        rect.height = tile_height - offset_y;

      i->tile = gegl_tile_source_get_tile ((GeglTileSource *) buffer,
                                           gegl_tile_index (x, tile_width),
                                           gegl_tile_index (y, tile_height),
                                           0);

      if (tile_width == rect.width)
        {
          gegl_tile_lock (i->tile, i->lock_mode);
            {
              gpointer data = gegl_tile_get_data (i->tile);
              gint bpp = babl_format_get_bytes_per_pixel (buffer->format);

              i->sub_data = (guchar *) data
                            + bpp * (rect.y * tile_width + rect.x);
            }
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

  i->buffer[self] = gegl_buffer_create_sub_buffer (buffer, roi);
  i->rect  [self] = *roi;
  i->format[self] = (format != NULL) ? format : buffer->format;
  i->flags [self] = flags;
  i->buf   [self] = NULL;

  if (flags & GEGL_BUFFER_READ)
    lock_mode |= GEGL_TILE_LOCK_READ;
  if (flags & GEGL_BUFFER_WRITE)
    lock_mode |= GEGL_TILE_LOCK_WRITE;
  if (flags & GEGL_BUFFER_GPU_READ)
    lock_mode |= GEGL_TILE_LOCK_GPU_READ;
  if (flags & GEGL_BUFFER_GPU_WRITE)
    lock_mode |= GEGL_TILE_LOCK_GPU_WRITE;

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

/* FIXME: we are currently leaking this buf pool, it should be
 * freeing it when gegl is uninitialized
 */

typedef struct BufInfo {
  gint     size;
  gint     used;  /* if this buffer is currently allocated */
  gpointer buf;
} BufInfo;

static GArray *buf_pool = NULL;

static gpointer iterator_buf_pool_get (gint size)
{
  gint i;
  if (G_UNLIKELY (!buf_pool))
    {
      buf_pool = g_array_new (TRUE, TRUE, sizeof (BufInfo));
    }
  for (i=0; i<buf_pool->len; i++)
    {
      BufInfo *info = &g_array_index (buf_pool, BufInfo, i);
      if (info->size >= size && info->used == 0)
        {
          info->used ++;
          return info->buf;
        }
    }
  {
    BufInfo info = {0, 1, NULL};
    info.size = size;
    info.buf = gegl_malloc (size);
    g_array_append_val (buf_pool, info);
    return info.buf;
  }
}

static void iterator_buf_pool_release (gpointer buf)
{
  gint i;
  for (i=0; i<buf_pool->len; i++)
    {
      BufInfo *info = &g_array_index (buf_pool, BufInfo, i);
      if (info->buf == buf)
        {
          info->used --;
          return;
        }
    }
  g_assert (0);
}

static void ensure_buf (_GeglBufferIterator *i, gint no)
{
  /* XXX: keeping a small pool of such buffres around for the used formats
   * would probably improve performance
   */
  if (i->buf[no]==NULL)
    i->buf[no] = iterator_buf_pool_get (babl_format_get_bytes_per_pixel (i->format[no]) *
                                        i->i[0].max_size);
}

#if DEBUG_DIRECT
static glong direct_read = 0;
static glong direct_write = 0;
static glong in_direct_read = 0;
static glong in_direct_write = 0;
#endif

gboolean gegl_buffer_iterator_next     (GeglBufferIterator *iterator)
{
  _GeglBufferIterator *i = (gpointer)iterator;
  gboolean result = FALSE;
  gint no;
  /* first we need to finish off any pending write work */

  if (i->buf[0] == (void*)0xdeadbeef)
    g_error ("%s called on finished buffer iterator", G_STRFUNC);
  if (i->iteration_no > 0)
    {
      for (no=0; no<i->iterable_count;no++)
        {
          if (i->flags[no] & GEGL_BUFFER_WRITE)
            {

              if (i->flags[no] & GEGL_BUFFER_SCAN_COMPATIBLE &&
                  i->flags[no] & GEGL_BUFFER_FORMAT_COMPATIBLE &&
                  i->roi[no].width == i->i[no].buffer->tile_storage->tile_width && (i->flags[no] & GEGL_BUFFER_FORMAT_COMPATIBLE))
                {
                   /* direct access */
#if DEBUG_DIRECT
                   direct_write += i->roi[no].width * i->roi[no].height;
#endif
                }
              else
                {
#if DEBUG_DIRECT
                  in_direct_write += i->roi[no].width * i->roi[no].height;
#endif

                  ensure_buf (i, no);

                  gegl_buffer_set (i->buffer[no], &(i->roi[no]), i->format[no], i->buf[no], GEGL_AUTO_ROWSTRIDE);
                }
            }
        }
    }

  g_assert (i->iterable_count > 0);

  /* then we iterate all */
  for (no=0; no<i->iterable_count;no++)
    {
      if (i->flags[no] & GEGL_BUFFER_SCAN_COMPATIBLE)
        {
          gboolean res;
          res = gegl_buffer_tile_iterator_next (&i->i[no]);
          if (no == 0)
            {
              result = res;
            }
          i->roi[no] = i->i[no].subrect;

          /* since they were scan compatible this should be true */
          if (res != result)
            {
              g_print ("%i==%i != 0==%i\n", no, res, result);
             } 
          g_assert (res == result);

          if ((i->flags[no] & GEGL_BUFFER_FORMAT_COMPATIBLE) && 
              i->roi[no].width == i->i[no].buffer->tile_storage->tile_width 
           )
            {
              /* direct access */
              i->data[no]=i->i[no].sub_data;
#if DEBUG_DIRECT
              direct_read += i->roi[no].width * i->roi[no].height;
#endif
            }
          else
            {
              ensure_buf (i, no);

              if (i->flags[no] & GEGL_BUFFER_READ)
                {
                  gegl_buffer_get (i->buffer[no], 1.0, &(i->roi[no]), i->format[no], i->buf[no], GEGL_AUTO_ROWSTRIDE);
                }

              i->data[no]=i->buf[no];
#if DEBUG_DIRECT
              in_direct_read += i->roi[no].width * i->roi[no].height;
#endif
            }
        }
      else
        { 
          /* we copy the roi from iterator 0  */
          i->roi[no] = i->roi[0];
          i->roi[no].x += (i->rect[no].x-i->rect[0].x);
          i->roi[no].y += (i->rect[no].y-i->rect[0].y);

          ensure_buf (i, no);

          if (i->flags[no] & GEGL_BUFFER_READ)
            {
              gegl_buffer_get (i->buffer[no], 1.0, &(i->roi[no]), i->format[no], i->buf[no], GEGL_AUTO_ROWSTRIDE);
            }
          i->data[no]=i->buf[no];

#if DEBUG_DIRECT
          in_direct_read += i->roi[no].width * i->roi[no].height;
#endif
        }
      i->length = i->roi[no].width * i->roi[no].height;
    }

  i->iteration_no++;

  if (result == FALSE)
    {
      for (no=0; no<i->iterable_count;no++)
        {
          if (i->buf[no])
            iterator_buf_pool_release (i->buf[no]);
          i->buf[no]=NULL;
          g_object_unref (i->buffer[no]);
        }
#if DEBUG_DIRECT
      g_print ("%f %f\n", (100.0*direct_read/(in_direct_read+direct_read)),
                           100.0*direct_write/(in_direct_write+direct_write));
#endif
      i->buf[0]=(void*)0xdeadbeef;
      g_free (i);
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
