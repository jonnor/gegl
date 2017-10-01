/* This file is an image processing operation for GEGL
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) 2017 Ell
 *
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#ifdef GEGL_PROPERTIES

enum_start (gegl_spherize_mode)
  enum_value (GEGL_SPHERIZE_MODE_RADIAL,     "radial",     N_("Radial"))
  enum_value (GEGL_SPHERIZE_MODE_HORIZONTAL, "horizontal", N_("Horizontal"))
  enum_value (GEGL_SPHERIZE_MODE_VERTICAL,   "vertical",   N_("Vertical"))
enum_end (GeglSpherizeMode)

property_enum (mode, _("Mode"),
               GeglSpherizeMode, gegl_spherize_mode,
               GEGL_SPHERIZE_MODE_RADIAL)
  description (_("Displacement mode"))

property_double (angle_of_view, _("Angle of view"), 90.0)
  description (_("Camera angle of view"))
  value_range (0.0, 180.0)
  ui_meta ("unit", "degree")

property_double (amount, _("Amount"), 1.0)
  description (_("Spherical cap angle, as a fraction of the co-angle of view"))
  value_range (-1.0, 1.0)

property_enum (sampler_type, _("Resampling method"),
  GeglSamplerType, gegl_sampler_type, GEGL_SAMPLER_LINEAR)
  description(_("Mathematical method for reconstructing pixel values"))

property_boolean (keep_surroundings, _("Keep original surroundings"), TRUE)
  description(_("Keep image unchanged outside the sphewre"))
  ui_meta ("visible", "mode {radial}")

property_color (background_color, _("Background color"), "none")
  ui_meta ("role", "color-secondary")
  ui_meta ("visible", "$keep_surroundings.visible")
  ui_meta ("sensitive", "! keep_surroundings")

#else

#define GEGL_OP_FILTER
#define GEGL_OP_NAME     spherize
#define GEGL_OP_C_SOURCE spherize.c

#include "gegl-op.h"
#include <math.h>

static gboolean
is_identity (GeglOperation *operation)
{
  GeglProperties *o = GEGL_PROPERTIES (operation);

  return o->angle_of_view < 1e-10 || fabs (o->amount) < 1e-10;
}

static gboolean
is_nop (GeglOperation *operation)
{
  GeglProperties *o = GEGL_PROPERTIES (operation);

  return is_identity (operation) && ! (o->mode == GEGL_SPHERIZE_MODE_RADIAL &&
                                       ! o->keep_surroundings);
}

static void
prepare (GeglOperation *operation)
{
  gegl_operation_set_format (operation, "input",  babl_format ("RGBA float"));
  gegl_operation_set_format (operation, "output", babl_format ("RGBA float"));
}

static GeglRectangle
get_required_for_output (GeglOperation       *operation,
                         const gchar         *input_pad,
                         const GeglRectangle *roi)
{
  GeglRectangle result = *roi;

  if (! is_identity (operation))
    {
      GeglProperties *o       = GEGL_PROPERTIES (operation);
      GeglRectangle  *in_rect = gegl_operation_source_get_bounding_box (operation, "input");

      if (in_rect)
        {
          switch (o->mode)
            {
            case GEGL_SPHERIZE_MODE_RADIAL:
              result = *in_rect;
              break;

            case GEGL_SPHERIZE_MODE_HORIZONTAL:
              result.x     = in_rect->x;
              result.width = in_rect->width;
              break;

            case GEGL_SPHERIZE_MODE_VERTICAL:
              result.y      = in_rect->y;
              result.height = in_rect->height;
              break;
            }
        }
    }

  return result;
}

static gboolean
parent_process (GeglOperation        *operation,
                GeglOperationContext *context,
                const gchar          *output_prop,
                const GeglRectangle  *result,
                gint                  level)
{
  if (is_nop (operation))
    {
      GObject *input;

      input = gegl_operation_context_get_object (context, "input");

      gegl_operation_context_set_object (context, "output", input);

      return TRUE;
    }

  return GEGL_OPERATION_CLASS (gegl_op_parent_class)->process (operation,
                                                               context,
                                                               output_prop,
                                                               result, level);
}

static gboolean
process (GeglOperation       *operation,
         GeglBuffer          *input,
         GeglBuffer          *output,
         const GeglRectangle *roi,
         gint                 level)
{
  GeglProperties      *o      = GEGL_PROPERTIES (operation);
  const Babl          *format = babl_format ("RGBA float");
  GeglSampler         *sampler;
  GeglBufferIterator  *iter;
  gfloat               bg_color[4];
  const GeglRectangle *in_extent;
  gdouble              cx, cy;
  gdouble              dx = 0.0, dy = 0.0;
  gdouble              coangle_of_view_2;
  gdouble              cap_angle;
  gdouble              focal_length;
  gdouble              cap_radius;
  gdouble              cap_height;
  gdouble              f, f2, r, r2, h, f_h, f_h2, f_hf;
  gboolean             is_id;
  gboolean             inverse;
  gint                 i, j;

  sampler = gegl_buffer_sampler_new_at_level (input, format,
                                              o->sampler_type, level);

  iter = gegl_buffer_iterator_new (output, roi, level, format,
                                   GEGL_ACCESS_WRITE, GEGL_ABYSS_NONE);

  gegl_buffer_iterator_add (iter, input, roi, level, format,
                            GEGL_ACCESS_READ, GEGL_ABYSS_NONE);

  gegl_color_get_pixel (o->background_color, format, bg_color);

  in_extent = gegl_operation_source_get_bounding_box (operation, "input");

  cx = in_extent->x + in_extent->width  / 2.0;
  cy = in_extent->y + in_extent->height / 2.0;

  if (o->mode == GEGL_SPHERIZE_MODE_RADIAL ||
      o->mode == GEGL_SPHERIZE_MODE_HORIZONTAL)
    {
      dx = 2.0 / in_extent->width;
    }
  if (o->mode == GEGL_SPHERIZE_MODE_RADIAL ||
      o->mode == GEGL_SPHERIZE_MODE_VERTICAL)
    {
      dy = 2.0 / in_extent->height;
    }

  coangle_of_view_2 = MAX (180.0 - o->angle_of_view, 0.01) * G_PI / 360.0;
  cap_angle         = fabs (o->amount) * coangle_of_view_2;
  focal_length      = tan (coangle_of_view_2);
  cap_radius        = 1.0 / sin (cap_angle);
  cap_height        = cap_radius * cos (cap_angle);

  f    = focal_length;
  f2   = f * f;
  r    = cap_radius;
  r2   = r * r;
  h    = cap_height;
  f_h  = f + h;
  f_h2 = f_h * f_h;
  f_hf = f_h * f;

  is_id   = is_identity (operation);
  inverse = o->amount < 0.0;

  while (gegl_buffer_iterator_next (iter))
    {
      gfloat       *out_pixel = iter->data[0];
      const gfloat *in_pixel  = iter->data[1];
      gfloat        x,  y;

      y = dy * (iter->roi->y + 0.5 - cy);

      for (j = iter->roi->y; j < iter->roi->y + iter->roi->height; j++, y += dy)
        {
          x = dx * (iter->roi->x + 0.5 - cx);

          for (i = iter->roi->x; i < iter->roi->x + iter->roi->width; i++, x += dx)
            {
              gfloat d2;

              d2 = x * x + y * y;

              if (! is_id && d2 <= 1.0)
                {
                  gdouble d     = sqrt (d2);
                  gdouble d2_f2 = d2 + f2;
                  gdouble src_d;
                  gdouble src_x, src_y;

                  if (! inverse)
                    src_d = (f_hf - sqrt (d2_f2 * r2 - f_h2 * d2)) * d / d2_f2;
                  else
                    src_d = f * d / (f_h - sqrt (r2 - d2));

                  src_x = i + 0.5;
                  src_y = j + 0.5;

                  if (d)
                    {
                      if (dx) src_x = cx + src_d * x / (dx * d);
                      if (dy) src_y = cy + src_d * y / (dy * d);
                    }

                  gegl_sampler_get (sampler, src_x, src_y,
                                    NULL, out_pixel, GEGL_ABYSS_NONE);
                }
              else
                {
                  if (d2 <= 1.0 || o->keep_surroundings)
                    memcpy (out_pixel, in_pixel, sizeof (gfloat) * 4);
                  else
                    memcpy (out_pixel, bg_color, sizeof (gfloat) * 4);
                }

              out_pixel += 4;
              in_pixel  += 4;
            }
        }
    }

  g_object_unref (sampler);

  return TRUE;
}

static void
gegl_op_class_init (GeglOpClass *klass)
{
  GeglOperationClass       *operation_class;
  GeglOperationFilterClass *filter_class;

  operation_class = GEGL_OPERATION_CLASS (klass);
  filter_class    = GEGL_OPERATION_FILTER_CLASS (klass);

  operation_class->prepare                 = prepare;
  operation_class->get_required_for_output = get_required_for_output;
  operation_class->process                 = parent_process;

  filter_class->process                    = process;

  gegl_operation_class_set_keys (operation_class,
    "name",               "gegl:spherize",
    "title",              _("Spherize"),
    "categories",         "distort:map",
    "position-dependent", "true",
    "license",            "GPL3+",
    "description",        _("Project image atop a spherical cap"),
    NULL);
}

#endif