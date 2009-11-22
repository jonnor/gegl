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
 * Copyright 2006-2009 Øyvind Kolås <pippin@gimp.org>
 */

#include "config.h"

#include "string.h" /* memcpy */

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <glib-object.h>

#include "gegl-types-internal.h"

#include "gegl.h"
#include "gegl-buffer.h"
#include "gegl-buffer-private.h"
#include "gegl-tile.h"
#include "gegl-tile-source.h"
#include "gegl-tile-storage.h"

#if HAVE_GPU
#include "gegl-gpu-types.h"
#include "gegl-gpu-texture.h"
#include "gegl-gpu-init.h"
#endif
#include "gegl-utils.h"

static void
default_free (gpointer        data,
#if HAVE_GPU
              GeglGpuTexture *gpu_data,
#endif
              gpointer        userdata)
{
  if (data != NULL)
    gegl_free (data);

#if HAVE_GPU
  if (gpu_data != NULL)
    gegl_gpu_texture_free (gpu_data);
#endif
}

GeglTile *gegl_tile_ref (GeglTile *tile)
{
  g_atomic_int_inc (&tile->ref_count);
  return tile;
}

void gegl_tile_unref (GeglTile *tile)
{
  if (!g_atomic_int_dec_and_test (&tile->ref_count))
    return;

  if (!gegl_tile_is_stored (tile))
    gegl_tile_store (tile);

  if (tile->data != NULL 
#if HAVE_GPU 
   || tile->gpu_data != NULL
#endif
   )
    {
      if (tile->next_shared == tile)
        {
          /* no clones */
          if (tile->destroy_notify)
            tile->destroy_notify (tile->data,
#if HAVE_GPU 
                                  tile->gpu_data,
#endif
                                  tile->destroy_notify_data);

          tile->data     = NULL;
#if HAVE_GPU 
          tile->gpu_data = NULL;
#endif
        }
      else
        {
          tile->prev_shared->next_shared = tile->next_shared;
          tile->next_shared->prev_shared = tile->prev_shared;
        }
    }

#if ENABLE_MT
  if (tile->mutex)
    {
      g_mutex_free (tile->mutex);
      tile->mutex = NULL;
    }
#endif
  g_slice_free (GeglTile, tile);
}

GeglTile *
gegl_tile_new_bare (void)
{
  GeglTile *tile = g_slice_new0 (GeglTile);

  tile->ref_count = 1;
  tile->tile_storage = NULL;
  tile->stored_rev = 0;

  tile->data     = NULL;
#if HAVE_GPU 
  tile->gpu_data = NULL;
  tile->gpu_rev    = 0;
#endif

  tile->rev        = 0;
  tile->stored_rev = 0;

  tile->read_locks  = 0;
  tile->write_locks = 0;
  tile->lock_mode   = GEGL_TILE_LOCK_NONE;

  tile->next_shared = tile;
  tile->prev_shared = tile;

#if ENABLE_MT
  tile->mutex = g_mutex_new ();
#endif

  tile->destroy_notify = default_free;

  return tile;
}

GeglTile *
gegl_tile_dup (GeglTile *src)
{
  GeglTile *tile = gegl_tile_new_bare ();

#if HAVE_GPU
  if (gegl_gpu_is_accelerated ())
    {
      if (src->rev > src->gpu_rev)
        {
          gegl_gpu_texture_set (src->gpu_data, NULL, src->data,
                                gegl_tile_get_format (src));

          src->gpu_rev = src->rev;
        }
      else if (src->gpu_rev > src->rev)
        {
          gegl_gpu_texture_get (src->gpu_data, NULL, src->data,
                                gegl_tile_get_format (src));

          src->rev = src->gpu_rev;
        }
    }
  tile->gpu_data = src->gpu_data;
  tile->gpu_rev    = 1;
#endif

  tile->data     = src->data;
  tile->size     = src->size;

  tile->tile_storage = src->tile_storage;

  tile->rev        = 1;
  tile->stored_rev = 1;

  tile->next_shared              = src->next_shared;
  src->next_shared               = tile;
  tile->prev_shared              = src;
#if ENABLE_MT
  if (tile->next_shared != src)
    {
      g_mutex_lock (tile->next_shared->mutex);
    }
#endif
  tile->next_shared->prev_shared = tile;
#if ENABLE_MT
  if (tile->next_shared != src)
    {
      g_mutex_unlock (tile->next_shared->mutex);
    }
#endif

  return tile;
}

GeglTile *
gegl_tile_new (gint        width,
               gint        height,
               const Babl *format)
{
  GeglTile *tile = gegl_tile_new_bare ();

  tile->size = width * height * babl_format_get_bytes_per_pixel (format);
  tile->data = gegl_malloc (tile->size);

#if HAVE_GPU
  if (gegl_gpu_is_accelerated ())
    tile->gpu_data = gegl_gpu_texture_new (width, height, format);
#endif

  tile->stored_rev = 1;

  return tile;
}

Babl *
gegl_tile_get_format (GeglTile *tile)
{
  return (tile->tile_storage != NULL)
           ? tile->tile_storage->format
           : NULL;
}

gint
gegl_tile_get_width (GeglTile *tile)
{
  return (tile->tile_storage != NULL)
           ? tile->tile_storage->tile_width
           : -1;
}

gint
gegl_tile_get_height (GeglTile *tile)
{
  return (tile->tile_storage != NULL)
           ? tile->tile_storage->tile_height
           : -1;
}

guchar *
gegl_tile_get_data (GeglTile *tile)
{
  return tile->data;
}

#if HAVE_GPU
GeglGpuTexture *
gegl_tile_get_gpu_data (GeglTile *tile)
{
  return tile->gpu_data;
}
#endif

static gpointer
gegl_memdup (gpointer src,
             gsize    size)
{
  gpointer ret;
  ret = gegl_malloc (size);
  memcpy (ret, src, size);
  return ret;
}

static void
gegl_tile_unclone (GeglTile *tile)
{
  if (tile->next_shared != tile)
    {
      /* the tile data is shared with other tiles,
       * create a local copy
       */
      tile->data                     = gegl_memdup (tile->data, tile->size);
      tile->prev_shared->next_shared = tile->next_shared;
      tile->next_shared->prev_shared = tile->prev_shared;
      tile->prev_shared              = tile;
      tile->next_shared              = tile;

#if HAVE_GPU
      if (gegl_gpu_is_accelerated ())
        tile->gpu_data = gegl_gpu_texture_dup (tile->gpu_data);
#endif
    }
}

static gint total_write_locks   = 0;
static gint total_write_unlocks = 0;

static gint total_read_locks    = 0;
static gint total_read_unlocks  = 0;

void gegl_bt (void);

void
gegl_tile_lock (GeglTile        *tile,
                GeglTileLockMode lock_mode)
{
#if HAVE_GPU
  if (!gegl_gpu_is_accelerated ())
    lock_mode &= ~GEGL_TILE_LOCK_GPU_READ & ~GEGL_TILE_LOCK_GPU_WRITE;
#endif

  if (tile->write_locks > 0)
    {
      if (lock_mode & GEGL_TILE_LOCK_WRITE
          || lock_mode & GEGL_TILE_LOCK_GPU_WRITE)
        {
          g_print ("hm\n");
          g_warning ("strange tile write-lock count: %i", tile->write_locks);
        }

      if (lock_mode & GEGL_TILE_LOCK_READ
          || lock_mode & GEGL_TILE_LOCK_GPU_READ)
        g_warning ("shouldn't lock for reading while write-lock (%i) is active",
                   tile->write_locks);
    }

  if (tile->read_locks > 0)
    {
      if (lock_mode & GEGL_TILE_LOCK_READ
          || lock_mode & GEGL_TILE_LOCK_GPU_READ)
        {
          g_print ("hm\n");
          g_warning ("strange tile read-lock count: %i", tile->read_locks);
        }

      if (lock_mode & GEGL_TILE_LOCK_WRITE
          || lock_mode & GEGL_TILE_LOCK_GPU_WRITE)
        g_warning ("shouldn't lock for writing while read-lock (%i) is active",
                   tile->read_locks);
    }

  if (lock_mode != GEGL_TILE_LOCK_NONE)
    {
#if ENABLE_MT
      g_mutex_lock (tile->mutex);
#endif
    }
  else
    g_warning ("%s called with lock_mode GEGL_TILE_LOCK_NONE", G_STRFUNC);

  if (lock_mode & GEGL_TILE_LOCK_READ
      || lock_mode & GEGL_TILE_LOCK_GPU_READ)
    {
      tile->read_locks++;
      total_read_locks++;
    }

  if (lock_mode & GEGL_TILE_LOCK_WRITE
      || lock_mode & GEGL_TILE_LOCK_GPU_WRITE)
    {
      tile->write_locks++;
      total_write_locks++;

      /*fprintf (stderr, "global tile locking: %i %i\n", locks, unlocks);*/
      gegl_tile_unclone (tile);
      /*gegl_buffer_add_dirty (tile->buffer, tile->x, tile->y);*/
    }

#if HAVE_GPU
  if (gegl_gpu_is_accelerated ())
    {
      if (lock_mode & GEGL_TILE_LOCK_GPU_READ && tile->rev > tile->gpu_rev)
        {
          gegl_gpu_texture_set (tile->gpu_data, NULL, tile->data,
                                gegl_tile_get_format (tile));

          tile->gpu_rev = tile->rev;
        }

      if (lock_mode & GEGL_TILE_LOCK_READ && tile->gpu_rev > tile->rev)
        {
          gegl_gpu_texture_get (tile->gpu_data, NULL, tile->data,
                                gegl_tile_get_format (tile));

          tile->rev = tile->gpu_rev;
        }
    }
#endif

  tile->lock_mode = lock_mode;
}

static void
_gegl_tile_void_pyramid (GeglTileSource *source,
                         gint            x,
                         gint            y,
                         gint            z)
{
  if (z > ((GeglTileStorage*) source)->seen_zoom)
    return;

  gegl_tile_source_void (source, x, y, z);
  _gegl_tile_void_pyramid (source, x / 2, y / 2, z + 1);
}

static void
gegl_tile_void_pyramid (GeglTile *tile)
{
  if (tile->tile_storage && 
      tile->tile_storage->seen_zoom &&
      tile->z == 0) /* we only accept voiding the base level */
    {
      _gegl_tile_void_pyramid (GEGL_TILE_SOURCE (tile->tile_storage), 
                               tile->x / 2,
                               tile->y / 2,
                               tile->z + 1);
      return;
    }
}

void
gegl_tile_unlock (GeglTile *tile)
{
  if (tile->lock_mode & GEGL_TILE_LOCK_WRITE
      || tile->lock_mode & GEGL_TILE_LOCK_GPU_WRITE)
    {
      total_write_unlocks++;

      if (tile->write_locks == 0)
        g_warning ("unlocked a tile with write-lock count == 0");

      tile->write_locks--;

      if (tile->write_locks == 0)
        {
          guint rev     = tile->rev;
#if HAVE_GPU
          guint gpu_rev = tile->gpu_rev;

          if (tile->lock_mode & GEGL_TILE_LOCK_GPU_WRITE)
            tile->gpu_rev = MAX (gpu_rev, rev) + 1;
#endif

          if (tile->lock_mode & GEGL_TILE_LOCK_WRITE)
            tile->rev = 
#if HAVE_GPU 
              MAX (rev, gpu_rev) + 1;
#else
          rev + 1;
#endif

          /* TODO: examine how this can be improved with h/w mipmaps */
          if (tile->z == 0)
            gegl_tile_void_pyramid (tile);
        }
    }

  if (tile->lock_mode & GEGL_TILE_LOCK_READ
      || tile->lock_mode & GEGL_TILE_LOCK_GPU_READ)
    {
      total_read_unlocks++;

      if (tile->read_locks == 0)
        g_warning ("unlocked a tile with read-lock count == 0");

      tile->read_locks--;
    }
#if ENABLE_MT
  g_mutex_unlock (tile->mutex);
#endif
  tile->lock_mode = GEGL_TILE_LOCK_NONE;
}

gboolean
gegl_tile_is_stored (GeglTile *tile)
{
  return tile->stored_rev == 
#if HAVE_GPU
    MAX (tile->rev, tile->gpu_rev);
#else
    tile->rev;
#endif
}

void
gegl_tile_void (GeglTile *tile)
{
  tile->stored_rev   = 
#if HAVE_GPU
    MAX (tile->rev, tile->gpu_rev);
#else
    tile->rev;
#endif
  tile->tile_storage = NULL;

  if (tile->z == 0)
    gegl_tile_void_pyramid (tile);
}

void
gegl_tile_cpy (GeglTile *src,
               GeglTile *dst)
{
  gegl_tile_lock (src, GEGL_TILE_LOCK_ALL_READ);
  gegl_tile_lock (dst, GEGL_TILE_LOCK_ALL_WRITE);

  default_free (dst->data,
#if HAVE_GPU
   dst->gpu_data, 
#endif
   NULL);

  dst->next_shared              = src->next_shared;
  src->next_shared              = dst;
  dst->prev_shared              = src;
  dst->next_shared->prev_shared = dst;

  dst->data     = src->data;
#if HAVE_GPU
  dst->gpu_data = src->gpu_data;
#endif

  gegl_tile_unlock (dst);
  gegl_tile_unlock (src);
}

void
gegl_tile_swp (GeglTile *a,
               GeglTile *b)
{
  guchar         *tmp_data;
#if HAVE_GPU
  GeglGpuTexture *tmp_gpu_data;
#endif

  gegl_tile_lock (a, GEGL_TILE_LOCK_ALL);
  gegl_tile_lock (b, GEGL_TILE_LOCK_ALL);

  gegl_tile_unclone (a);
  gegl_tile_unclone (b);

/* gegl_buffer_add_dirty (a->buffer, a->x, a->y);
   gegl_buffer_add_dirty (b->buffer, b->x, b->y);*/

  g_assert (a->size == b->size);

  tmp_data = a->data;
  a->data  = b->data;
  b->data  = tmp_data;

#if HAVE_GPU
  tmp_gpu_data = a->gpu_data;
  a->gpu_data  = b->gpu_data;
  b->gpu_data  = tmp_gpu_data;
#endif

  gegl_tile_unlock (a);
  gegl_tile_unlock (b);
}

gboolean
gegl_tile_store (GeglTile *tile)
{
  gboolean stored;

  if (gegl_tile_is_stored (tile))
    return TRUE;

  if (tile->tile_storage == NULL)
    return FALSE;

  gegl_tile_lock (tile, GEGL_TILE_LOCK_ALL_READ);
  stored = gegl_tile_source_set_tile (GEGL_TILE_SOURCE (tile->tile_storage),
                                      tile->x,
                                      tile->y,
                                      tile->z,
                                      tile);
  gegl_tile_unlock (tile);
  return stored;
}
