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

gint
main (gint    argc,
      gchar **argv)
{
  gint      retval = SUCCESS;

  GeglTile *tile;
  gfloat   *components;

  gegl_init (&argc, &argv);

  tile       = gegl_tile_new (50, 50, babl_format ("RGBA float"));
  components = g_new (gfloat, 4 * 50 * 50);

    {
      gint    cnt;
      gfloat *tile_components;

      memset (tile->data, 0, 4 * 50 * 50);
      gegl_gpu_texture_clear (tile->gpu_data, NULL);

      /* initialize tile to a random image */
      for (cnt = 0; cnt < 1000; cnt++)
        {
          gint x = g_random_int_range (0, 50);
          gint y = g_random_int_range (0, 50);

          gfloat *pixel = &components[(y * 4 * 50) + (x * 4)];

          pixel[0] = g_random_double ();
          pixel[1] = g_random_double ();
          pixel[2] = g_random_double ();
          pixel[3] = 1.0;
        }

      gegl_tile_lock (tile, GEGL_TILE_LOCK_WRITE);

      memcpy (gegl_tile_get_data (tile),
              components,
              sizeof (gfloat) * 4 * 50 * 50);

      gegl_tile_unlock (tile);

      /* check contents of tile for consistency with previous write */
      gegl_tile_lock (tile, GEGL_TILE_LOCK_READ);
      tile_components = (gpointer) gegl_tile_get_data (tile);

      for (cnt = 0; cnt < 4 * 50 * 50; cnt++)
        if (!GEGL_FLOAT_EQUAL (tile_components[cnt], components[cnt]))
          {
            g_printerr ("Tile data inconsistent with original image data. "
                        "Aborting.\n");

            retval = FAILURE;
            break;
          }

      gegl_tile_unlock (tile);
    }

  g_free (components);
  g_object_unref (tile);

  gegl_exit ();

  return retval;
}
