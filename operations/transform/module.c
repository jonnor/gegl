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
 * Copyright 2006 Philip Lafleur
 */

#include "config.h"
#include <gegl-plugin.h>
#include "module.h"
#include "transform-core.h"

static GTypeModule          *transform_module;
static const GeglModuleInfo  modinfo =
{
  GEGL_MODULE_ABI_VERSION
};

G_MODULE_EXPORT GTypeModule *
transform_module_get_module (void)
{
  return transform_module;
}

G_MODULE_EXPORT const GeglModuleInfo *
gegl_module_query (GTypeModule *module)
{
  return &modinfo;
}

GType gegl_op_rotate_register_type                (GTypeModule *module);
GType gegl_op_rotate_on_center_register_type      (GTypeModule *module);
GType gegl_op_reflect_register_type               (GTypeModule *module);
GType gegl_op_scale_ratio_register_type           (GTypeModule *module);
GType gegl_op_scale_size_register_type            (GTypeModule *module);
GType gegl_op_scale_size_keepaspect_register_type (GTypeModule *module);
GType gegl_op_shear_register_type                 (GTypeModule *module);
GType gegl_op_translate_register_type             (GTypeModule *module);
GType transform_get_type   (void);

#include <stdio.h>

G_MODULE_EXPORT gboolean
gegl_module_register (GTypeModule *module)
{
  GType dummy;
  transform_module = module;

  dummy = op_transform_get_type ();
  dummy = transform_get_type ();
  dummy = gegl_op_scale_ratio_register_type (module);
  dummy = gegl_op_scale_size_register_type (module);
  dummy = gegl_op_scale_size_keepaspect_register_type (module);
  dummy = gegl_op_rotate_register_type (module);
  dummy = gegl_op_reflect_register_type (module);
  dummy = gegl_op_shear_register_type (module);
  dummy = gegl_op_translate_register_type (module);
  dummy = gegl_op_rotate_on_center_register_type (module);

  dummy ++; /* silence gcc, having it is required to avoid optimizing
               away the _get_type calls themselves */

  return TRUE;
}
