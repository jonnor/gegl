/* This file is part of GEGL
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
 * Copyright 2006 Øyvind Kolås
 */


#include "config.h"

#include <glib-object.h>

#include "gegl.h"
#include "gegl-types-internal.h"
#include "gegl-operation-point-filter.h"
#include "graph/gegl-pad.h"
#include "graph/gegl-node.h"
#include "gegl-utils.h"
#include <string.h>

#include "gegl-buffer-private.h"
#include "gegl-tile-storage.h"

#include "gegl-gpu-types.h"
#include "gegl-gpu-init.h"

static gboolean gegl_operation_point_filter_process
                              (GeglOperation       *operation,
                               GeglBuffer          *input,
                               GeglBuffer          *output,
                               const GeglRectangle *result);

G_DEFINE_TYPE (GeglOperationPointFilter, gegl_operation_point_filter, GEGL_TYPE_OPERATION_FILTER)

static void prepare (GeglOperation *operation)
{
  gegl_operation_set_format (operation, "input", babl_format ("RGBA float"));
  gegl_operation_set_format (operation, "output", babl_format ("RGBA float"));
}

static void
gegl_operation_point_filter_class_init (GeglOperationPointFilterClass *klass)
{
  GeglOperationFilterClass *filter_class = GEGL_OPERATION_FILTER_CLASS (klass);
  GeglOperationClass *operation_class = GEGL_OPERATION_CLASS (klass);

  filter_class->process = gegl_operation_point_filter_process;
  operation_class->prepare = prepare;
  operation_class->no_cache = TRUE;
}

static void
gegl_operation_point_filter_init (GeglOperationPointFilter *self)
{
}

static gboolean
gegl_operation_point_filter_process (GeglOperation       *operation,
                                     GeglBuffer          *input,
                                     GeglBuffer          *output,
                                     const GeglRectangle *result)
{
  const Babl *in_format  = gegl_operation_get_format (operation, "input");
  const Babl *out_format = gegl_operation_get_format (operation, "output");

  GeglOperationClass            *operation_class;
  GeglOperationPointFilterClass *point_filter_class;

  operation_class    = GEGL_OPERATION_GET_CLASS (operation);
  point_filter_class = GEGL_OPERATION_POINT_FILTER_GET_CLASS (operation);

  if (result->width > 0 && result->height > 0)
    {
#if HAVE_GPU
      GeglOperationPointFilterGpuProcessor gpu_process
        = (gpointer) gegl_operation_class_get_gpu_processor (operation_class);

      gboolean use_gpu = (gegl_gpu_is_accelerated () && gpu_process != NULL);
#endif

      GeglBufferIterator *i = gegl_buffer_iterator_new (
                                output,
                                result,
                                out_format,
#if HAVE_GPU
                                use_gpu
                                  ? GEGL_BUFFER_GPU_WRITE
                                  : GEGL_BUFFER_WRITE
#else
                                  GEGL_BUFFER_WRITE
#endif
                                  );

      gint read  = gegl_buffer_iterator_add (i,
                                             input,
                                             result,
                                             in_format,
#if HAVE_GPU
                                             use_gpu
                                               ? GEGL_BUFFER_GPU_READ
                                               : GEGL_BUFFER_READ
#else
                                               GEGL_BUFFER_READ
#endif
                                               );

#if HAVE_GPU
      if (use_gpu)
        {
          while (gegl_buffer_iterator_next (i))
            gpu_process (operation,
                         i->gpu_data[read],
                         i->gpu_data[0],
                         i->length,
                         &i->roi[0]);
        }
      else
        {
#endif
          while (gegl_buffer_iterator_next (i))
            point_filter_class->process (operation,
                                         i->data[read],
                                         i->data[0],
                                         i->length,
                                         &i->roi[0]);
#if HAVE_GPU
        }
#endif

      gegl_buffer_iterator_free (i);
    }

  return TRUE;
}
