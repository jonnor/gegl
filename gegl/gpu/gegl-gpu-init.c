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

#include <GL/glew.h>
#include <GL/glut.h>

#include "gegl-gpu-types.h"
#include "gegl-gpu-init.h"

static gint                     gpu_glut_window     = -1;
static gboolean                 gpu_is_accelerated  = FALSE;
static GeglGpuFramebufferHandle gpu_framebuffer     = 0;
static GSList                  *gpu_shader_programs = NULL;

void
gegl_gpu_init (gint    *argc,
               gchar ***argv)
{
  glutInit (argc, *argv);
  gpu_glut_window = glutCreateWindow (NULL);
  glewInit ();

  if (glewIsSupported ("GL_VERSION_1_2")
      && GLEW_ARB_fragment_shader
      && GLEW_EXT_framebuffer_object
      && GLEW_ARB_texture_rectangle
      && GLEW_ARB_texture_float)
    {
      glGenFramebuffersEXT (1, &gpu_framebuffer);
      glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, gpu_framebuffer);

      glutSetWindow (gpu_glut_window);
      glutHideWindow ();

      gpu_is_accelerated = TRUE;
    }
}

GeglGpuFramebufferHandle
gegl_gpu_get_framebuffer (void)
{
  return gpu_framebuffer;
}

gboolean
gegl_gpu_is_accelerated (void)
{
  return gpu_is_accelerated;
}

void
gegl_gpu_register_shader_program (GeglGpuShaderProgramHandle gpu_shader_program)
{
  gpu_shader_programs = g_slist_prepend (gpu_shader_programs,
                                         GUINT_TO_POINTER (gpu_shader_program));
}

void
gegl_gpu_exit (void)
{
  GSList *iter;
  iter = gpu_shader_programs;

  while (iter)
    {
      glDeleteProgram (GPOINTER_TO_UINT (iter->data));
      iter = iter->next;
    }

  g_slist_free (gpu_shader_programs);
  gpu_shader_programs = NULL;

  glDeleteFramebuffersEXT (1, &gpu_framebuffer);
  gpu_framebuffer = 0;

  glutDestroyWindow (gpu_glut_window);
  gpu_glut_window = -1;
}
