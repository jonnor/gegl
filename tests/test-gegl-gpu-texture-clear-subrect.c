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

#define ARRAY_SIZE(array) (sizeof (array) / sizeof (array[0]))

typedef struct GeglGpuTextureClearSubrectTestCase
{
  gchar        *name;
  GeglRectangle roi;

} GeglGpuTextureClearSubrectTestCase;

gint
main (gint    argc,
      gchar **argv)
{
  gint retval = SUCCESS;

  GeglGpuTextureClearSubrectTestCase test_cases[5] =
    {
      {
        "top-left corner",
        { 0,  0, 10, 10}
      },
      {
        "top-right corner",
        {40,  0, 10, 10}
      },
      {
        "bottom-left corner",
        { 0, 40, 10, 10}
      },
      {
        "bottom-right corner",
        {40, 40, 10, 10}
      },
      {
        "center",
        {20, 20, 10, 10}
      }
    };

  GeglGpuTexture *texture;
  gfloat         *components;

  gegl_init (&argc, &argv);

  texture    = gegl_gpu_texture_new (50, 50, babl_format ("RGBA float"));
  components = g_new (gfloat, 50 * 50 * 4);

    {
      gint   cnt;
      gfloat color[4];

      color[0] = g_random_double ();
      color[1] = g_random_double ();
      color[2] = g_random_double ();
      color[3] = g_random_double ();

      for (cnt = 0; cnt < 50 * 50; cnt++)
        {
          gint index = cnt * 4;

          components[index    ] = color[0];
          components[index + 1] = color[1];
          components[index + 2] = color[2];
          components[index + 3] = color[3];
        }

      /* set texture to some solid color to make sure that we aren't clearing
       * an empty texture (note: this assumes that gegl_gpu_texture_set()
       * works as expected)
       */
      gegl_gpu_texture_set (texture, NULL, components, NULL);

      /* clear individual subregions */
      for (cnt = 0; cnt < ARRAY_SIZE (test_cases); cnt++)
        gegl_gpu_texture_clear (texture, &test_cases[cnt].roi);

      gegl_gpu_texture_get (texture, NULL, components, NULL);

      /* test subregions */
      for (cnt = 0; cnt < ARRAY_SIZE (test_cases); cnt++)
        {
          gint x, y;

          gint last_x = test_cases[cnt].roi.x + test_cases[cnt].roi.width - 1;
          gint last_y = test_cases[cnt].roi.y + test_cases[cnt].roi.height - 1;

          for (y = test_cases[cnt].roi.y; y <= last_y; y++)
            for (x = test_cases[cnt].roi.x; x <= last_x; x++)
              {
                gfloat *pixel = &components[((y * 50) + x) * 4];

                if (   !GEGL_FLOAT_IS_ZERO (pixel[0])
                    || !GEGL_FLOAT_IS_ZERO (pixel[1])
                    || !GEGL_FLOAT_IS_ZERO (pixel[2])
                    || !GEGL_FLOAT_IS_ZERO (pixel[3]))
                  {
                    g_printerr ("The gegl_gpu_texture_clear() (%s) test "
                                "failed. Aborting.\n", test_cases[cnt].name);

                    retval = FAILURE;
                    goto abort;
                  }
              }
        }
    }

abort:
  g_free (components);
  gegl_gpu_texture_free (texture);

  gegl_exit ();

  return retval;
}
