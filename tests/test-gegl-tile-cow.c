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

#include <string.h>
#include <babl/babl.h>

#include "gegl.h"
#include "gegl-gpu-init.h"
#include "gegl-gpu-texture.h"

#include "../../gegl/buffer/gegl-tile.h"

#define SUCCESS 0
#define FAILURE (-1)

#define TEXTURE_WIDTH  50
#define TEXTURE_HEIGHT 50

gint
main (gint    argc,
      gchar **argv)
{
  gint      retval = SUCCESS;

  GeglTile *tile;
  GeglTile *tile2;

  gegl_init (&argc, &argv);

  tile  = gegl_tile_new (TEXTURE_WIDTH,
                         TEXTURE_HEIGHT,
                         babl_format ("RGBA float"));

  tile2 = gegl_tile_dup (tile);

  if (gegl_tile_get_data (tile) != gegl_tile_get_data (tile2))
    {
      g_printerr ("Test on tile data equality after tile duplication failed. "
                  "Aborting\n");

      retval = FAILURE;
      goto abort;
    }

  if (   gegl_gpu_is_accelerated ()
      && gegl_tile_get_gpu_data (tile) != gegl_tile_get_gpu_data (tile2))
    {
      g_printerr ("Test on tile GPU data equality after tile duplication "
                  "failed. Aborting\n");

      retval = FAILURE;
      goto abort;
    }

  gegl_tile_lock   (tile2, GEGL_TILE_LOCK_WRITE);
  gegl_tile_unlock (tile2);

  if (gegl_tile_get_data (tile) == gegl_tile_get_data (tile2))
    {
      g_printerr ("Test on tile data inequality after uncloning failed. "
                  "Aborting\n");

      retval = FAILURE;
      goto abort;
    }

  if (   gegl_gpu_is_accelerated ()
      && gegl_tile_get_gpu_data (tile) == gegl_tile_get_gpu_data (tile2))
    {
      g_printerr ("Test on tile GPU data inequality after uncloning failed. "
                  "Aborting\n");

      retval = FAILURE;
      goto abort;
    }

abort:
  g_object_unref (tile2);
  g_object_unref (tile);

  gegl_exit ();

  return retval;
}
