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

#define SUCCESS 0
#define FAILURE (-1)

#define ARRAY_SIZE(array) (sizeof (array) / sizeof (array[0]))

#define TEXTURE_WIDTH  50
#define TEXTURE_HEIGHT 50

#define TEST_STRIP_WIDTH  10
#define TEST_STRIP_HEIGHT 10

#define RANDOM_PIXEL_COUNT 400

typedef struct GeglGpuTextureSetSubrectTestCase
{
  gchar        *name;
  GeglRectangle roi;

} GeglGpuTextureSetSubrectTestCase;

gint
main (gint    argc,
      gchar **argv)
{
  gint retval = SUCCESS;

  GeglGpuTextureSetSubrectTestCase test_cases[5] =
    {
      {
        "top-left corner",
        { 0,  0, TEST_STRIP_WIDTH, TEST_STRIP_HEIGHT}
      },
      {
        "top-right corner",
        {
          TEXTURE_WIDTH - TEST_STRIP_WIDTH,
          0,
          TEST_STRIP_WIDTH,
          TEST_STRIP_HEIGHT}
      },
      {
        "bottom-left corner",
        {
          0,
          TEXTURE_HEIGHT - TEST_STRIP_HEIGHT,
          TEST_STRIP_WIDTH,
          TEST_STRIP_HEIGHT}
      },
      {
        "bottom-right corner",
        {
          TEXTURE_WIDTH - TEST_STRIP_WIDTH,
          TEXTURE_HEIGHT - TEST_STRIP_HEIGHT,
          TEST_STRIP_WIDTH,
          TEST_STRIP_HEIGHT}
      },
      {
        "center",
        {
          (TEXTURE_WIDTH - TEST_STRIP_WIDTH) / 2,
          (TEXTURE_HEIGHT - TEST_STRIP_HEIGHT) / 2,
          TEST_STRIP_WIDTH,
          TEST_STRIP_HEIGHT
        }
      }
    };

  GeglGpuTexture *texture;

  gfloat *components1;
  gfloat *components2;

  gegl_init (&argc, &argv);

  texture = gegl_gpu_texture_new (TEXTURE_WIDTH,
                                  TEXTURE_HEIGHT,
                                  babl_format ("RGBA float"));

  components1 = g_new0 (gfloat, TEST_STRIP_WIDTH * TEST_STRIP_HEIGHT * 4);
  components2 = g_new0 (gfloat, TEST_STRIP_WIDTH * TEST_STRIP_HEIGHT * 4);

    {
      gint i;

      for (i = 0; i < ARRAY_SIZE (test_cases); i++)
        {
          gint j;

          memset (components1, 0, sizeof (gfloat)
                                    * TEST_STRIP_WIDTH
                                    * TEST_STRIP_HEIGHT * 4);

          memset (components2, 0, sizeof (gfloat)
                                    * TEST_STRIP_WIDTH
                                    * TEST_STRIP_HEIGHT * 4);

          for (j = 0; j < RANDOM_PIXEL_COUNT; j++)
            {
              gint x = g_random_int_range (0, TEST_STRIP_WIDTH);
              gint y = g_random_int_range (0, TEST_STRIP_HEIGHT);

              gfloat *pixel = &components1[((y * TEST_STRIP_WIDTH) + x) * 4];

              pixel[0] = g_random_double ();
              pixel[1] = g_random_double ();
              pixel[2] = g_random_double ();
              pixel[3] = g_random_double ();
            }

          /* set the texture subregion to a random image */
          gegl_gpu_texture_set (texture,
                                &test_cases[i].roi,
                                components1,
                                NULL);

          /* get the texture and put it in a different buffer (actually, this
           * test should also be considered as a test for
           * gegl_gpu_texture_get())
           */
          gegl_gpu_texture_get (texture,
                                &test_cases[i].roi,
                                components2,
                                NULL);

          /* test subregion */
          for (j = 0; j < TEST_STRIP_WIDTH * TEST_STRIP_HEIGHT * 4; j++)
            if (!GEGL_FLOAT_EQUAL (components1[j], components2[j]))
              {
                g_printerr ("The gegl_gpu_texture_set() (%s) test failed. "
                            "Aborting.\n", test_cases[i].name);

                retval = FAILURE;
                goto abort;
              }
        }
    }

abort:
  g_free (components2);
  g_free (components1);

  gegl_gpu_texture_free (texture);

  gegl_exit ();

  return retval;
}
