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

#define RANDOM_PIXEL_COUNT 1000

typedef struct GeglGpuTextureCopySubrectTestCase
{
  gchar        *name;
  GeglRectangle roi;

} GeglGpuTextureCopySubrectTestCase;

gint
main (gint    argc,
      gchar **argv)
{
  gint retval = SUCCESS;

  GeglGpuTextureCopySubrectTestCase test_cases[5] =
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

  GeglGpuTexture *texture1;
  GeglGpuTexture *texture2;

  gfloat *components1;
  gfloat *components2;

  gegl_init (&argc, &argv);

  texture1 = gegl_gpu_texture_new (TEXTURE_WIDTH,
                                   TEXTURE_HEIGHT,
                                   babl_format ("RGBA float"));
  texture2 = gegl_gpu_texture_new (TEXTURE_WIDTH,
                                   TEXTURE_HEIGHT,
                                   babl_format ("RGBA float"));

  components1 = g_new0 (gfloat, TEXTURE_WIDTH * TEXTURE_HEIGHT * 4);
  components2 = g_new0 (gfloat, TEXTURE_WIDTH * TEXTURE_HEIGHT * 4);

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

      /* set the texture to a random image (note: this test assumes that
       * gegl_gpu_texture_set() works as expected)
       */
      gegl_gpu_texture_set   (texture1, NULL, components1, NULL);
      gegl_gpu_texture_clear (texture2, NULL);

      /* copy individual subregions from the first texture to the
       * second texture
       */
      for (cnt = 0; cnt < ARRAY_SIZE (test_cases); cnt++)
        gegl_gpu_texture_copy (texture1,
                               &test_cases[cnt].roi,
                               texture2,
                               test_cases[cnt].roi.x,
                               test_cases[cnt].roi.y);

      gegl_gpu_texture_get (texture2, NULL, components2, NULL);

      /* test individual subregions */
      for (cnt = 0; cnt < ARRAY_SIZE (test_cases); cnt++)
        {
          gint x, y;

          gint last_x = test_cases[cnt].roi.x + test_cases[cnt].roi.width - 1;
          gint last_y = test_cases[cnt].roi.y + test_cases[cnt].roi.height - 1;

          for (y = test_cases[cnt].roi.y; y <= last_y; y++)
            for (x = test_cases[cnt].roi.x; x <= last_x; x++)
              {
                gint index = ((y * TEXTURE_WIDTH) + x) * 4;

                gfloat *pixel1 = &components1[index];
                gfloat *pixel2 = &components2[index];

                if (   !GEGL_FLOAT_EQUAL (pixel1[0], pixel2[0])
                    || !GEGL_FLOAT_EQUAL (pixel1[1], pixel2[1])
                    || !GEGL_FLOAT_EQUAL (pixel1[2], pixel2[2])
                    || !GEGL_FLOAT_EQUAL (pixel1[3], pixel2[3]))
                  {
                    g_printerr ("The gegl_gpu_texture_copy() (%s) test failed. "
                                "Aborting.\n", test_cases[cnt].name);

                    retval = FAILURE;
                    goto abort;
                  }
              }
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
