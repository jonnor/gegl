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
#include "gegl-gpu-texture.h"

#define SUCCESS 0
#define FAILURE (-1)

gint
main (gint    argc,
      gchar **argv)
{
  gint retval = SUCCESS;

  GeglGpuTexture *texture;
  gfloat         *components;

  gegl_init (&argc, &argv);

  texture    = gegl_gpu_texture_new (50, 50, babl_format ("RGBA float"));
  components = g_new (gfloat, 4 * 50 * 50);

    {
      gint cnt;

      gegl_gpu_texture_clear (texture, NULL);
      gegl_gpu_texture_get   (texture, NULL, components, NULL);

      for (cnt = 0; cnt < 4 * 50 * 50; cnt++)
        if (components[cnt] != 0.0)
          {
            g_printerr ("The gegl_gpu_texture_clear() test failed. "
                        "Aborting.\n");

            retval = FAILURE;
            goto abort;
          }
    }

abort:
  g_free (components);
  gegl_gpu_texture_free (texture);

  gegl_exit ();

  return retval;
}
