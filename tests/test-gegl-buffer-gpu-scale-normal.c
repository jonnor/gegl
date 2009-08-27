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
#include "gegl-buffer.h"

#define MAX_TEST_ITERATIONS 10

#define BUFFER_WIDTH  1000
#define BUFFER_HEIGHT 1000

#define DEFAULT_STRIP_WIDTH  100
#define DEFAULT_STRIP_HEIGHT 100

#define SUCCESS 0
#define FAILURE (-1)

#define ARRAY_SIZE(array) (sizeof (array) / sizeof (array[0]))

typedef struct GeglBufferGpuSubImageTestCase
{
  gchar        *name;
  GeglRectangle roi;

} GeglBufferGpuSubImageTestCase;

GeglRectangle *generate_random_rectangle (GeglRectangle       *rect,
                                          gint                 max_width,
                                          gint                 max_height);
GeglRectangle *generate_random_point     (GeglRectangle       *rect,
                                          gint                 width,
                                          gint                 height);
gint           compare_components        (gfloat              *components1,
                                          gfloat              *components2,
                                          gint                 width,
                                          gint                 height);
gint           compare_pixels            (gfloat              *pixel1,
                                          gfloat              *pixel2);

GeglRectangle *
generate_random_rectangle (GeglRectangle *rect,
                           gint           max_width,
                           gint           max_height)
{
  gint min_width  = MAX (max_width * 0.01, 1);
  gint min_height = MAX (max_height * 0.01, 1);

  rect->x = g_random_int_range (0, max_width - min_width);
  rect->y = g_random_int_range (0, max_height - min_height);

  rect->width  = g_random_int_range (min_width, max_width - rect->x);
  rect->height = g_random_int_range (min_height, max_height - rect->y);

  return rect;
}

GeglRectangle *
generate_random_point (GeglRectangle *rect,
                       gint           width,
                       gint           height)
{
  rect->x = g_random_int_range (0, width);
  rect->y = g_random_int_range (0, height);

  rect->width = 1;
  rect->height = 1;

  return rect;
}

gint
compare_components (gfloat  *components1,
                    gfloat  *components2,
                    gint     width,
                    gint     height)
{
  gint retval = SUCCESS;
  gint x, y;

  for (y = 0; y < height && retval != FAILURE; y++)
    {
      for (x = 0; x < width; x++)
        {
          gint index = ((y * width) + x) * 4;
          retval = compare_pixels (&components1[index], &components2[index]);

          if (retval == FAILURE)
            break;
        }
    }

  return retval;
}

gint
compare_pixels (gfloat  *pixel1,
                gfloat  *pixel2)
{
  gint retval = SUCCESS;

  if (!GEGL_FLOAT_EQUAL (pixel1[0], pixel2[0])
      || !GEGL_FLOAT_EQUAL (pixel1[1], pixel2[1])
      || !GEGL_FLOAT_EQUAL (pixel1[2], pixel2[2])
      || !GEGL_FLOAT_EQUAL (pixel1[3], pixel2[3]))
    {
      retval = FAILURE;
    }

  return retval;
}

gint
main (gint    argc,
      gchar **argv)
{
  gint iteration_cnt;
  gint retval = SUCCESS;

  GeglBuffer   *gpu_buffer;
  GeglRectangle buffer_extents = {0, 0, BUFFER_WIDTH, BUFFER_HEIGHT};

  gegl_init (&argc, &argv);

  if (!gegl_gpu_is_accelerated ())
    {
      g_warning ("GPU-support is disabled. Skipping.\n");
      goto skip;
    }

  gpu_buffer = gegl_buffer_new (&buffer_extents, babl_format ("RGBA float"));

  for (iteration_cnt = 0; iteration_cnt < MAX_TEST_ITERATIONS; iteration_cnt++)
    {
        /* TEST-SUITE 1: full-image tests */
        {
          gfloat *components1 = g_new (gfloat, 
                                       BUFFER_WIDTH
                                         * BUFFER_HEIGHT
                                         * 4);

          gfloat *components2 = g_new (gfloat,
                                       BUFFER_WIDTH
                                         * BUFFER_HEIGHT
                                         * 4);

          GeglGpuTexture *texture = gegl_gpu_texture_new (
                                      BUFFER_WIDTH,
                                      BUFFER_HEIGHT,
                                      babl_format ("RGBA float"));

          gint i;

          for (i = 0; i < BUFFER_WIDTH * BUFFER_HEIGHT; i++)
            {
              gint index = i * 4;

              components1[index]     = g_random_double ();
              components1[index + 1] = g_random_double ();
              components1[index + 2] = g_random_double ();
              components1[index + 3] = g_random_double ();
            }

          /* TEST 1.1: test gegl_buffer_gpu_get() */
          gegl_buffer_set (gpu_buffer,
                           NULL,
                           NULL,
                           components1,
                           GEGL_AUTO_ROWSTRIDE);

          gegl_buffer_gpu_get  (gpu_buffer, 1.0, NULL, texture);
          gegl_gpu_texture_get (texture, NULL, components2, NULL);

          retval = compare_components (components1,
                                       components2,
                                       BUFFER_WIDTH,
                                       BUFFER_HEIGHT);

          if (retval == FAILURE)
            {
              g_printerr ("TEST 1.1 (full image, gegl_buffer_gpu_get() test) "
                          "failed at test iteration #%d. Aborting.\n",
                          iteration_cnt + 1);

              goto abort_test1;
            }

          /* TEST 1.2: test gegl_buffer_gpu_set() */
          gegl_gpu_texture_set (texture, NULL, components1, NULL);
          gegl_buffer_gpu_set  (gpu_buffer, NULL, texture);

          gegl_buffer_get (gpu_buffer,
                           1.0,
                           NULL,
                           NULL,
                           components2,
                           GEGL_AUTO_ROWSTRIDE);

          retval = compare_components (components1,
                                       components2,
                                       BUFFER_WIDTH,
                                       BUFFER_HEIGHT);

          if (retval == FAILURE)
            {
              g_printerr ("TEST 1.2 (full image, gegl_buffer_gpu_set() test) "
                          "failed at test iteration #%d. Aborting.\n",
                          iteration_cnt + 1);

              goto abort_test1;
            }

abort_test1:

          gegl_gpu_texture_free (texture);
          g_free (components2);
          g_free (components1);

          if (retval == FAILURE)
            goto abort;
        }

        /* TEST-SUITE 2: sub-image tests */
        {
          GeglBufferGpuSubImageTestCase default_test_cases[5] =
            {
              {
                "top-left corner",
                {
                  0,
                  0,
                  DEFAULT_STRIP_WIDTH,
                  DEFAULT_STRIP_HEIGHT
                }
              },
              {
                "top-right corner",
                {
                  BUFFER_WIDTH - DEFAULT_STRIP_WIDTH,
                  0,
                  DEFAULT_STRIP_WIDTH,
                  DEFAULT_STRIP_HEIGHT
                }
              },
              {
                "bottom-left corner",
                {
                  0,
                  BUFFER_HEIGHT - DEFAULT_STRIP_HEIGHT,
                  DEFAULT_STRIP_WIDTH,
                  DEFAULT_STRIP_HEIGHT
                }
              },
              {
                "bottom-right corner",
                {
                  BUFFER_WIDTH - DEFAULT_STRIP_WIDTH,
                  BUFFER_HEIGHT - DEFAULT_STRIP_HEIGHT,
                  DEFAULT_STRIP_WIDTH,
                  DEFAULT_STRIP_HEIGHT
                }
              },
              {
                "center",
                {
                  (BUFFER_WIDTH - DEFAULT_STRIP_WIDTH) / 2,
                  (BUFFER_HEIGHT - DEFAULT_STRIP_HEIGHT) / 2,
                  DEFAULT_STRIP_WIDTH,
                  DEFAULT_STRIP_HEIGHT
                }
              }
            };

          gfloat *components1 = g_new (gfloat,
                                       DEFAULT_STRIP_WIDTH
                                         * DEFAULT_STRIP_HEIGHT
                                         * 4);

          gfloat *components2 = g_new (gfloat,
                                       DEFAULT_STRIP_WIDTH
                                         * DEFAULT_STRIP_HEIGHT
                                         * 4);

          GeglGpuTexture *texture = gegl_gpu_texture_new (
                                      DEFAULT_STRIP_WIDTH,
                                      DEFAULT_STRIP_HEIGHT,
                                      babl_format ("RGBA float"));

          gint i;

          for (i = 0; i < ARRAY_SIZE (default_test_cases); i++)
            {
              gint j;

              for (j = 0; j < DEFAULT_STRIP_WIDTH * DEFAULT_STRIP_HEIGHT; j++)
                {
                  gint index = j * 4;

                  components1[index]     = g_random_double ();
                  components1[index + 1] = g_random_double ();
                  components1[index + 2] = g_random_double ();
                  components1[index + 3] = g_random_double ();
                }

              /* TEST 2.1: test gegl_buffer_gpu_get() */
              gegl_buffer_set (gpu_buffer,
                               &default_test_cases[i].roi,
                               NULL,
                               components1,
                               GEGL_AUTO_ROWSTRIDE);

              gegl_buffer_gpu_get (gpu_buffer,
                                   1.0,
                                   &default_test_cases[i].roi,
                                   texture);

              gegl_gpu_texture_get (texture, NULL, components2, NULL);

              retval = compare_components (components1,
                                           components2,
                                           DEFAULT_STRIP_WIDTH,
                                           DEFAULT_STRIP_HEIGHT);

              if (retval == FAILURE)
                {
                  g_printerr ("TEST 2.1 (sub-image, gegl_buffer_gpu_get() "
                              "test) on %s of image failed at test iteration "
                              "#%d. Aborting.\n",
                              default_test_cases[i].name,
                              iteration_cnt + 1);

                  goto abort_test2;
                }

              /* TEST 2.2: test gegl_buffer_gpu_set() */
              gegl_gpu_texture_set (texture, NULL, components1, NULL);

              gegl_buffer_gpu_set (gpu_buffer,
                                   &default_test_cases[i].roi,
                                   texture);

              gegl_buffer_get (gpu_buffer,
                               1.0,
                               &default_test_cases[i].roi,
                               NULL,
                               components2,
                               GEGL_AUTO_ROWSTRIDE);

              retval = compare_components (components1,
                                           components2,
                                           DEFAULT_STRIP_WIDTH,
                                           DEFAULT_STRIP_HEIGHT);

              if (retval == FAILURE)
                {
                  g_printerr ("TEST 2.2 (sub-image, gegl_buffer_gpu_set() "
                              "test) on %s of image failed at test iteration "
                              "#%d. Aborting.\n",
                              default_test_cases[i].name,
                              iteration_cnt + 1);

                  goto abort_test2;
                }
            }

            {
              GeglRectangle rect;

              generate_random_rectangle (&rect, BUFFER_WIDTH, BUFFER_HEIGHT);

              gegl_gpu_texture_free (texture);
              g_free (components2);
              g_free (components1);

              components1 = g_new (gfloat, rect.width * rect.height * 4);
              components2 = g_new (gfloat, rect.width * rect.height * 4);

              texture = gegl_gpu_texture_new (rect.width,
                                              rect.height,
                                              babl_format ("RGBA float"));

              for (i = 0; i < rect.width * rect.height; i++)
                {
                  gint index = i * 4;

                  components1[index]     = g_random_double ();
                  components1[index + 1] = g_random_double ();
                  components1[index + 2] = g_random_double ();
                  components1[index + 3] = g_random_double ();
                }

              /* TEST 2.3: test gegl_buffer_gpu_get() */
              gegl_buffer_set (gpu_buffer,
                               &rect,
                               NULL,
                               components1,
                               GEGL_AUTO_ROWSTRIDE);

              gegl_buffer_gpu_get  (gpu_buffer, 1.0, &rect, texture);
              gegl_gpu_texture_get (texture, NULL, components2, NULL);

              retval = compare_components (components1,
                                           components2,
                                           rect.width,
                                           rect.height);

              if (retval == FAILURE)
                {
                  g_printerr ("TEST 2.3 (sub-image, gegl_buffer_gpu_get() "
                              "test) on random portion of image failed at test "
                              "iteration #%d. Aborting.\n",
                              iteration_cnt + 1);

                  goto abort_test2;
                }

              /* TEST 2.4: test gegl_buffer_gpu_set() */
              gegl_gpu_texture_set (texture, NULL, components1, NULL);
              gegl_buffer_gpu_set  (gpu_buffer, &rect, texture);

              gegl_buffer_get (gpu_buffer,
                               1.0,
                               &rect,
                               NULL,
                               components2,
                               GEGL_AUTO_ROWSTRIDE);

              retval = compare_components (components1,
                                           components2,
                                           rect.width,
                                           rect.height);

              if (retval == FAILURE)
                {
                  g_printerr ("TEST 2.4 (sub-image, gegl_buffer_gpu_set() "
                              "test) on random portion of image failed at test "
                              "iteration #%d. Aborting.\n",
                              iteration_cnt + 1);

                  goto abort_test2;
                }
            }

abort_test2:

          gegl_gpu_texture_free (texture);
          g_free (components2);
          g_free (components1);

          if (retval == FAILURE)
            goto abort;
        }

        /* TEST-SUITE 3: pixel tests */
        {
          GeglGpuTexture *texture = gegl_gpu_texture_new (1, 1,
                                      babl_format ("RGBA float"));

          gfloat color1[4];
          gfloat color2[4];

          GeglRectangle point;

          color1[0] = g_random_double ();
          color1[1] = g_random_double ();
          color1[2] = g_random_double ();
          color1[3] = g_random_double ();

          generate_random_point (&point, BUFFER_WIDTH, BUFFER_HEIGHT);

          /* TEST 3.1: test gegl_buffer_gpu_get_pixel()
           * (through gegl_buffer_gpu_get())
           */
          gegl_buffer_set (gpu_buffer,
                           &point,
                           NULL,
                           color1,
                           GEGL_AUTO_ROWSTRIDE);

          gegl_buffer_gpu_get  (gpu_buffer, 1.0, &point, texture);
          gegl_gpu_texture_get (texture, NULL, color2, NULL);

          retval = compare_pixels (color1, color2);

          if (retval == FAILURE)
            {
              g_printerr ("TEST 3.1 (pixel, gegl_buffer_gpu_get() test) failed "
                          "at test iteration #%d. Aborting.\n",
                          iteration_cnt + 1);

              goto abort_test3;
            }

          /* TEST 3.2: test gegl_buffer_gpu_set_pixel()
           * (through gegl_buffer_gpu_set())
           */
          gegl_gpu_texture_set (texture, NULL, color1, NULL);
          gegl_buffer_gpu_set  (gpu_buffer, NULL, texture);

          gegl_buffer_get (gpu_buffer,
                           1.0,
                           &point,
                           NULL,
                           color2,
                           GEGL_AUTO_ROWSTRIDE);

          retval = compare_pixels (color1, color2);

          if (retval == FAILURE)
            {
              g_printerr ("TEST 3.2 (pixel, gegl_buffer_gpu_set() test) failed "
                          "at test iteration #%d. Aborting.\n",
                          iteration_cnt + 1);

              goto abort_test3;
            }

abort_test3:

          gegl_gpu_texture_free (texture);

          if (retval == FAILURE)
            goto abort;
        }
    }

abort:
  gegl_buffer_destroy (gpu_buffer);

skip:
  gegl_exit ();

  return retval;
}
