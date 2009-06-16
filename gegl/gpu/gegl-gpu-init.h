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

#ifndef __GEGL_GPU_INIT_H__
#define __GEGL_GPU_INIT_H__

#include "gegl-gpu-types.h"

G_BEGIN_DECLS

void                     gegl_gpu_init                    (gint    *argc,
                                                           gchar ***argv);
void                     gegl_gpu_exit                    (void);
GeglGpuFramebufferHandle gegl_gpu_get_framebuffer         (void);
gboolean                 gegl_gpu_is_accelerated          (void);
void                     gegl_gpu_register_shader_program
                           (GeglGpuShaderProgramHandle shaderProgram);

G_END_DECLS

#endif  /* __GEGL_GPU_INIT_H__ */
