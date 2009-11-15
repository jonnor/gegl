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
 * Copyright 2006 Øyvind Kolås <pippin@gimp.org>
 */

#if 0
#include <GL/glew.h>
#endif

#include "config.h"
#include <glib/gi18n-lib.h>


#ifdef GEGL_CHANT_PROPERTIES

gegl_chant_double (in_low, _("Low input"), -1.0, 4.0, 0.0,
                   _("Input luminance level to become lowest output"))
gegl_chant_double (in_high, _("High input"), -1.0, 4.0, 1.0,
                   _("Input luminance level to become white."))
gegl_chant_double (out_low, _("Low output"), -1.0, 4.0, 0.0,
                   _("Lowest luminance level in output"))
gegl_chant_double (out_high, _("High output"),
                   -1.0, 4.0, 1.0, _("Highest luminance level in output"))

#else

#define GEGL_CHANT_TYPE_POINT_FILTER
#define GEGL_CHANT_C_FILE         "levels.c"

#include "gegl-chant.h"

#if 0

#include "gegl-gpu-types.h"
#include "gegl-gpu-init.h"

static const char* shader_program_str = "                       \
uniform sampler2DRect pixels;                                   \
uniform float in_offset, out_offset, scale;                     \
                                                                \
void main()                                                     \
{                                                               \
  vec4 pixel   = texture2DRect(pixels, gl_TexCoord[0].st);      \
  vec3 color   = (pixel.rgb - in_offset) * scale + out_offset;  \
  gl_FragColor = vec4 (color, pixel.a);                         \
}                                                               ";

static GLuint shader_program = 0;
static GLuint pixels_param;
static GLuint in_offset_param;
static GLuint out_offset_param;
static GLuint scale_param;

static GLuint
create_shader_program (void)
{
  GLuint shader = glCreateShader (GL_FRAGMENT_SHADER);
  GLuint program;

  glShaderSource (shader, 1, &shader_program_str, NULL);
  glCompileShader (shader);

  program = glCreateProgram ();
  glAttachShader (program, shader);
  glLinkProgram (program);

  pixels_param = glGetUniformLocation (program, "pixels");
  in_offset_param = glGetUniformLocation (program, "in_offset");
  out_offset_param = glGetUniformLocation (program, "out_offset");
  scale_param = glGetUniformLocation (program, "scale");

  return program;
}


static gboolean
process_gpu (GeglOperation       *op,
             GeglGpuTexture      *in,
             GeglGpuTexture      *out,
             glong                samples,
             const GeglRectangle *roi)
{
  /* Retrieve a pointer to GeglChantO structure which contains all the
   * chanted properties
   */
  GeglChantO *o          = GEGL_CHANT_PROPERTIES (op);
  gfloat      in_range   = o->in_high - o->in_low;
  gfloat      out_range  = o->out_high - o->out_low;
  gfloat      in_offset  = o->in_low * 1.0;
  gfloat      out_offset = o->out_low * 1.0;
  gfloat      scale;

  if (in_range == 0.0)
    in_range = 0.00000001;

  scale = out_range / in_range;

  /* attach *out* texture to offscreen framebuffer */
  glFramebufferTexture2DEXT (GL_FRAMEBUFFER_EXT, 
                             GL_COLOR_ATTACHMENT0_EXT,
                             GL_TEXTURE_RECTANGLE_ARB,
                             out->handle,
                             0);

  /* set *out* texture as render target */
  glDrawBuffer (GL_COLOR_ATTACHMENT0_EXT);

  /* create and register shader program, all shader programs will be deleted
   * after GEGL terminates
   */
  if (shader_program == 0)
    {
      shader_program = create_shader_program ();
      gegl_gpu_register_shader_program (shader_program);
    }
  glUseProgram (shader_program);

  /* setup shader parameters */
  glActiveTexture (GL_TEXTURE0);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, in->handle);
  glUniform1i (pixels_param, 0);
  glUniform1f (in_offset_param, in_offset);
  glUniform1f (out_offset_param, out_offset);
  glUniform1f (scale_param, scale);

  /* viewport transform for 1:1 pixel=texel=data mapping */
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();
  gluOrtho2D (0.0, roi->width, 0.0, roi->height);
  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();
  glViewport (0, 0, roi->width, roi->height);

  /* make quad filled to hit every pixel/texel */
  glPolygonMode (GL_FRONT, GL_FILL);

  /* and render quad */
  glBegin (GL_QUADS);
    glTexCoord2f (0.0, 0.0);
    glVertex2f (0.0, 0.0);
    glTexCoord2f (roi->width, 0.0);
    glVertex2f (roi->width, 0.0);
    glTexCoord2f (roi->width, roi->height);
    glVertex2f (roi->width, roi->height);
    glTexCoord2f (0.0, roi->height);
    glVertex2f (0.0, roi->height);
  glEnd ();

  return TRUE;
}

#endif


/* GeglOperationPointFilter gives us a linear buffer to operate on
 * in our requested pixel format
 */
static gboolean
process (GeglOperation       *op,
         void                *in_buf,
         void                *out_buf,
         glong                n_pixels,
         const GeglRectangle *roi)
{
  GeglChantO *o = GEGL_CHANT_PROPERTIES (op);
  gfloat     *in_pixel;
  gfloat     *out_pixel;
  gfloat      in_range;
  gfloat      out_range;
  gfloat      in_offset;
  gfloat      out_offset;
  gfloat      scale;
  glong       i;

  in_pixel = in_buf;
  out_pixel = out_buf;

  in_offset = o->in_low * 1.0;
  out_offset = o->out_low * 1.0;
  in_range = o->in_high-o->in_low;
  out_range = o->out_high-o->out_low;

  if (in_range == 0.0)
    in_range = 0.00000001;

  scale = out_range/in_range;

  for (i=0; i<n_pixels; i++)
    {
      int c;
      for (c=0;c<3;c++)
        out_pixel[c] = (in_pixel[c]- in_offset) * scale + out_offset;
      out_pixel[3] = in_pixel[3];
      out_pixel += 4;
      in_pixel += 4;
    }
  return TRUE;
}

static void
gegl_chant_class_init (GeglChantClass *klass)
{
  GeglOperationClass            *operation_class;
  GeglOperationPointFilterClass *point_filter_class;

  operation_class    = GEGL_OPERATION_CLASS (klass);
  point_filter_class = GEGL_OPERATION_POINT_FILTER_CLASS (klass);

  point_filter_class->process = process;

  operation_class->name        = "gegl:levels";
  operation_class->categories  = "color";
  operation_class->description =
        _("Remaps the intensity range of the image");

#if 0
  gegl_operation_class_add_processor (operation_class,
                                      G_CALLBACK (process_gpu),
                                      "gpu:reference");
#endif
}

#endif
