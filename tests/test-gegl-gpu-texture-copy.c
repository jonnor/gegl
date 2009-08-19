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

#include <babl/babl.h>

#include "gegl.h"
#include "gegl-utils.h"
#include "gegl-gpu-texture.h"

#define SUCCESS 0
#define FAILURE (-1)

gint
main (gint    argc,
      gchar **argv)
{
  gint retval = SUCCESS;

  GeglGpuTexture *texture1;
  GeglGpuTexture *texture2;

  gfloat *components1;
  gfloat *components2;

  gegl_init (&argc, &argv);

  texture1 = gegl_gpu_texture_new (50, 50, babl_format ("RGBA float"));
  texture2 = gegl_gpu_texture_new (50, 50, babl_format ("RGBA float"));

  components1 = g_new (gfloat, 4 * 50 * 50);
  components2 = g_new (gfloat, 4 * 50 * 50);

    {
      gint cnt;

      /* generate random image */
      for (cnt = 0; cnt < 1000; cnt++)
        {
          gint    x     = g_random_int_range (0, 50);
          gint    y     = g_random_int_range (0, 50);
          gfloat *pixel = &components1[(y * 50) + (x * 4)];

          pixel[0] = 1.0;
          pixel[1] = 1.0;
          pixel[2] = 1.0;
          pixel[3] = 1.0;
        }

      gegl_gpu_texture_set  (texture1, NULL, components1, NULL);

      /* copy first texture to second texture */
      gegl_gpu_texture_copy (texture1, NULL, texture2, 0, 0);
      gegl_gpu_texture_get  (texture2, NULL, components2, NULL);

      /* compare the two images */
      for (cnt = 0; cnt < 4 * 50 * 50; cnt++)
        if (!GEGL_FLOAT_EQUAL (components1[cnt], components2[cnt]))
          {
            g_printerr ("The gegl_gpu_texture_copy() test failed. "
                        "Aborting.\n");

            retval = FAILURE;
            goto abort;
          }
    }

abort:
  g_free (components2);
  g_free (components1);

  gegl_gpu_texture_free (texture2);
  gegl_gpu_texture_free (texture1);

  gegl_exit ();

  return retval;
}
