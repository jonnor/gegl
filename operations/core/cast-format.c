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
 * Copyright 2007 Étienne Bersac <bersace03@laposte.net>
 * Copyright 2006 Øyvind Kolås <pippin@gimp.org>
 * Copyright 2013 Daniel Sabo
 *
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#ifdef GEGL_CHANT_PROPERTIES

gegl_chant_format (input_format, _("Input Format"),
                   _("The babl format of the input"))
gegl_chant_format (output_format, _("Output Format"),
                   _("The babl format of the output"))

#else

#define GEGL_CHANT_TYPE_FILTER
#define GEGL_CHANT_C_FILE "cast-format.c"

#include "gegl-chant.h"

static void
prepare (GeglOperation *operation)
{
  GeglChantO *o = GEGL_CHANT_PROPERTIES (operation);

  if (o->input_format)
    gegl_operation_set_format (operation, "input", o->input_format);

  if (o->output_format)
    gegl_operation_set_format (operation, "output", o->output_format);
}

static gboolean
process (GeglOperation        *operation,
         GeglOperationContext *context,
         const gchar          *output_prop,
         const GeglRectangle  *roi,
         gint                  level)
{
  GeglChantO *o = GEGL_CHANT_PROPERTIES (operation);
  GeglBuffer *input;
  GeglBuffer *output;

  if (! o->input_format || ! o->output_format)
    {
      g_warning ("cast-format: input-format or output-format are not set");
      return FALSE;
    }

  if (babl_format_get_bytes_per_pixel (o->input_format) !=
      babl_format_get_bytes_per_pixel (o->output_format))
    {
      g_warning ("cast-format: input-format and output-format have different bpp");
      return FALSE;
    }

  if (strcmp (output_prop, "output"))
    {
      g_warning ("cast-format: requested processing of %s pad", output_prop);
      return FALSE;
    }

  input = gegl_operation_context_get_source (context, "input");
  if (! input)
    {
      g_warning ("cast: received NULL input");
      return FALSE;
    }

  output = gegl_buffer_new (roi, o->input_format);

  gegl_buffer_copy (input,  roi,
                    output, roi);
  gegl_buffer_set_format (output, o->output_format);

  g_object_unref (input);

  gegl_operation_context_take_object (context, "output", G_OBJECT (output));

  return TRUE;
}

static void
gegl_chant_class_init (GeglChantClass *klass)
{
  GeglOperationClass *operation_class = GEGL_OPERATION_CLASS (klass);

  operation_class->prepare  = prepare;
  operation_class->process  = process;
  operation_class->no_cache = FALSE;

  gegl_operation_class_set_keys (operation_class,
    "name",       "gegl:cast-format",
    "categories", "core:color",
    "description", _("Cast the data between input_format and output_format, "
                     "both bormats must have the same bpp"),
    NULL);
}

#endif