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
#include "gegl-gpu-init.h"
#include "gegl-gpu-texture.h"

#define SUCCESS 0
#define FAILURE (-1)

#define TEXTURE_WIDTH  50
#define TEXTURE_HEIGHT 50

#define RANDOM_PIXEL_COUNT 1000

gint
main (gint    argc,
      gchar **argv)
{
  gint retval = SUCCESS;

  GeglGpuTexture *texture;

  gfloat *components1;
  gfloat *components2;

  gegl_init (&argc, &argv);

  if (!gegl_gpu_is_accelerated ())
    {
      g_warning ("GPU-support is disabled. Skipping.\n");
      goto skip;
    }

  texture = gegl_gpu_texture_new (TEXTURE_WIDTH,
                                  TEXTURE_HEIGHT,
                                  babl_format ("RGBA float"));

  components1 = g_new (gfloat, TEXTURE_WIDTH * TEXTURE_HEIGHT * 4);
  components2 = g_new (gfloat, TEXTURE_WIDTH * TEXTURE_HEIGHT * 4);

    {
      gint cnt;

      for (cnt = 0; cnt < RANDOM_PIXEL_COUNT; cnt++)
        {
          gint x = g_random_int_range (0, TEXTURE_WIDTH);
          gint y = g_random_int_range (0, TEXTURE_HEIGHT);

          gfloat *pixel = &components1[((y * TEXTURE_WIDTH) + x) * 4];

          pixel[0] = g_random_double ();
          pixel[1] = g_random_double ();
          pixel[2] = g_random_double ();
          pixel[3] = g_random_double ();
        }

      /* set the texture to a random image */
      gegl_gpu_texture_set (texture, NULL, components1, NULL);

      /* get the texture and put it in a different buffer (actually, this test
       * should also be considered as a test for gegl_gpu_texture_get())
       */
      gegl_gpu_texture_get (texture, NULL, components2, NULL);

      for (cnt = 0; cnt < TEXTURE_WIDTH * TEXTURE_HEIGHT * 4; cnt++)
        if (!GEGL_FLOAT_EQUAL (components1[cnt], components2[cnt]))
          {
            g_printerr ("The gegl_gpu_texture_set() test failed. Aborting.\n");
            retval = FAILURE;
            goto abort;
          }
    }

abort:
  g_free (components2);
  g_free (components1);

  gegl_gpu_texture_free (texture);

skip:
  gegl_exit ();

  return retval;
}
