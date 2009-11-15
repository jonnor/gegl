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

   /* no properties */

#else

#define GEGL_CHANT_TYPE_POINT_FILTER
#define GEGL_CHANT_C_FILE       "invert.c"
#define GEGLV4

#include "gegl-chant.h"

#if 0

#include "gegl-gpu-types.h"
#include "gegl-gpu-init.h"

static const char* shader_program_str = "                       \
uniform sampler2DRect pixels;                                   \
                                                                \
void main()                                                     \
{                                                               \
  vec4 pixel   = texture2DRect (pixels, gl_TexCoord[0].st);     \
  vec3 color   = 1.0 - pixel.rgb;                               \
  gl_FragColor = vec4 (color, pixel.a);                         \
}                                                               ";

static GLuint shader_program = 0;
static GLuint pixels_param;

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

  return program;
}

static gboolean
process_gpu (GeglOperation       *op,
             GeglGpuTexture      *in,
             GeglGpuTexture      *out,
             glong                samples,
             const GeglRectangle *roi)
{
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

static gboolean
process (GeglOperation       *op,
         void                *in_buf,
         void                *out_buf,
         glong                samples,
         const GeglRectangle *roi)
{
  glong   i;
  gfloat *in  = in_buf;
  gfloat *out = out_buf;

  for (i=0; i<samples; i++)
    {
      int  j;
      for (j=0; j<3; j++)
        {
          gfloat c;
          c = in[j];
          c = 1.0 - c;
          out[j] = c;
        }
      out[3]=in[3];
      in += 4;
      out+= 4;
    }
  return TRUE;
}


#ifdef HAS_G4FLOAT
static gboolean
process_simd (GeglOperation       *op,
              void                *in_buf,
              void                *out_buf,
              glong                samples,
              const GeglRectangle *roi)
{
  g4float *in  = in_buf;
  g4float *out = out_buf;
  g4float  one = g4float_one;

  while (samples--)
    {
      gfloat a= g4float_a(*in)[3];
      *out = one - *in;
      g4float_a(*out)[3]=a;
      in  ++;
      out ++;
    }
  return TRUE;
}
#endif

static void
gegl_chant_class_init (GeglChantClass *klass)
{
  GeglOperationClass            *operation_class;
  GeglOperationPointFilterClass *point_filter_class;

  operation_class    = GEGL_OPERATION_CLASS (klass);
  point_filter_class = GEGL_OPERATION_POINT_FILTER_CLASS (klass);

  point_filter_class->process = process;

  operation_class->name        = "gegl:invert";
  operation_class->categories  = "color";
  operation_class->description =
     _("Inverts the components (except alpha), the result is the "
       "corresponding \"negative\" image.");

#ifdef HAS_G4FLOAT
  gegl_operation_class_add_processor (operation_class,
                                      G_CALLBACK (process_simd), "simd");
#endif
#if 0
  gegl_operation_class_add_processor (operation_class,
                                      G_CALLBACK (process_gpu),
                                      "gpu:reference");
#endif
}

#endif
