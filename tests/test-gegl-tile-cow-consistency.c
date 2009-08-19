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
#include "gegl-utils.h"
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

  gfloat   *gpu_components;

  gegl_init (&argc, &argv);

  tile = gegl_tile_new (TEXTURE_WIDTH,
                        TEXTURE_HEIGHT,
                        babl_format ("RGBA float"));

  gegl_tile_lock (tile, GEGL_TILE_LOCK_WRITE);

  /* clear tile data (not GPU data) and leave the tile unsynchronized */
  memset (gegl_tile_get_data (tile), 0x00, sizeof (gfloat)
                                             * TEXTURE_WIDTH
                                             * TEXTURE_HEIGHT * 4);

  gegl_tile_unlock (tile);

  tile2 = gegl_tile_dup (tile);

    {
      gint cnt;

      /* we don't need to lock the tile here if no other threads are/will be
       * accessing the tile, gegl_tile_dup() should have synchronized the
       * tile data and GPU data automatically for us
       */
      gfloat *components  = (gpointer) gegl_tile_get_data (tile2);

      gpu_components = g_new (gfloat, TEXTURE_WIDTH * TEXTURE_HEIGHT * 4);
      gegl_gpu_texture_get (gegl_tile_get_gpu_data (tile2),
                            NULL,
                            gpu_components,
                            babl_format ("RGBA float"));

      for (cnt = 0; cnt < TEXTURE_WIDTH * TEXTURE_HEIGHT * 4; cnt++)
        if (!GEGL_FLOAT_EQUAL (components[cnt], gpu_components[cnt]))
          {
            g_printerr ("Test on gegl_tile_dup() GPU data/data consistency "
                        "failed. Aborting.\n");

            retval = FAILURE;
            goto abort;
          }

abort:
      g_free (gpu_components);
    }

  g_object_unref (tile2);
  g_object_unref (tile);

  gegl_exit ();

  return retval;
}
