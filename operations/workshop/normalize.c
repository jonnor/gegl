/* This file is an image processing operation for GEGL
 *
 * GEGL is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * GEGL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GEGL; if not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright 2006-2014 Øyvind Kolås <pippin@gimp.org>
 * Copyright 2014 The Grid, Jon Nordby <jononor@gmail.com>
 */

#include "config.h"
#include <glib/gi18n-lib.h>
#include "math.h"


#ifdef GEGL_PROPERTIES
property_boolean (enabled, _("Enabled"), TRUE)
    description (_("Whether the normalize effect is enable"))
property_double (low, _("Low"),  0.0)
   description  (_("Lowest value to rescale to"))
   value_range  (0.0, 1.0)
   ui_range     (0.0, 1.0)
property_double (high, _("High"),  1.0)
   description  (_("Highest value to rescale to"))
   value_range  (0.0, 1.0)
   ui_range     (0.0, 1.0)
#else

#define GEGL_OP_POINT_FILTER
#define GEGL_OP_C_FILE "normalize.c"

#include "gegl-op.h"


typedef struct NormalizeProperties_ {

} NormalizeProperties;


static void inline
process_pixel_pass(gfloat *in, gfloat *out) {
    out[0] = in[0];
    out[1] = in[1];
    out[2] = in[2];
    out[3] = in[3];
}

static void inline
process_pixel_normalize(gfloat *in, gfloat *out, gfloat low, gfloat high) {
    const gfloat range = high-low;


    for (x = param.min; x <= param.max; x++)
      param.lut[x] = 255 * (x - param.min) / range;

    out[0] = mapped[0];
    out[1] = mapped[1];
    out[2] = mapped[2];
    out[3] = mapped[3] * in[1];
}

static void prepare (GeglOperation *operation)
{
  GeglProperties *o = GEGL_PROPERTIES (operation);
  GradientMapProperties *props = (GradientMapProperties*)o->user_data;

  gegl_operation_set_format (operation, "input", babl_format ("RGBA float"));
  gegl_operation_set_format (operation, "output", babl_format ("RGBA float"));

/*
  if (!props)
    {
      props = g_new(GradientMapProperties, 1);
      props->gradient = NULL;
      o->user_data = props;
    }
*/
}

static void finalize (GObject *object)
{
    GeglOperation *op = (void*)object;
    GeglProperties *o = GEGL_PROPERTIES (op);
/*
    if (o->user_data) {
      GradientMapProperties *props = (GradientMapProperties *) o->user_data;
      o->user_data = NULL;
    }
*/
    G_OBJECT_CLASS(gegl_op_parent_class)->finalize (object);
}


static gboolean
process (GeglOperation       *op,
         void                *in_buf,
         void                *out_buf,
         glong                n_pixels,
         const GeglRectangle *roi,
         gint                 level)
{
  GeglProperties *o = GEGL_PROPERTIES (op);
  gfloat     * GEGL_ALIGNED in_pixel = in_buf;
  gfloat     * GEGL_ALIGNED out_pixel = out_buf;
//  GradientMapProperties *props = (GradientMapProperties*)o->user_data;

  if (o->enabled) {
      for (int i=0; i<n_pixels; i++)
        {
          process_pixel_normalize(in_pixel, out_pixel, props->low, props->high);
          in_pixel  += 4;
          out_pixel += 4;
        }
  } else {
      for (int i=0; i<n_pixels; i++)
        {
          process_pixel_pass(in_pixel, out_pixel);
          in_pixel  += 4;
          out_pixel += 4;
        }
  }
  return TRUE;
}

static void
gegl_op_class_init (GeglOpClass *klass)
{
  GeglOperationClass            *operation_class;
  GeglOperationPointFilterClass *point_filter_class;
  gchar                         *composition = "<?xml version='1.0' encoding='UTF-8'?>"
    "<gegl>"
    "<node operation='gegl:gradient-map'>"
    "  <params>"
    "    <param name='color1'>#x410404</param>"
    "    <param name='color2'>#x22FFFA</param>"
    "  </params>"
    "</node>"
    "<node operation='gegl:load'>"
    "  <params>"
    "    <param name='path'>standard-input.png</param>"
    "  </params>"
    "</node>"
    "</gegl>";

  operation_class    = GEGL_OPERATION_CLASS (klass);
  point_filter_class = GEGL_OPERATION_POINT_FILTER_CLASS (klass);

  G_OBJECT_CLASS (klass)->finalize = finalize;
  operation_class->prepare = prepare;
  point_filter_class->process = process;

  gegl_operation_class_set_keys (operation_class,
      "name",       "gegl:normalize",
      "categories", "color",
      "description", _("Applies a color gradient."),
      "reference-composition", composition,
      NULL);
}

#endif /* #ifdef GEGL_PROPERTIES */
