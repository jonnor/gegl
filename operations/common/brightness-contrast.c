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

#include <GL/glew.h>

#include "config.h"
#include <glib/gi18n-lib.h>

/* Followed by this #if ... */
#ifdef GEGL_CHANT_PROPERTIES
/* ... are the properties of the filter, these are all scalar values
 * (doubles), the the parameters are:
 *       property name,   min,   max, default, "description of property"
 */

gegl_chant_double (contrast, _("Contrast"), -5.0, 5.0, 1.0,
                   _("Range scale factor"))
gegl_chant_double (brightness, _("Brightness"), -3.0, 3.0, 0.0,
                   _("Amount to increase brightness"))

/* this will create the instance structure for our use, and register the
 * property with the given ranges, default values and a comment for the
 * documentation/tooltip.
 */
#else

/* Specify the base class we're building our operation on, the base
 * class provides a lot of the legwork so we do not have to. For
 * brightness contrast the best base class is the POINT_FILTER base
 * class.
 */
#define GEGL_CHANT_TYPE_POINT_FILTER

/* We specify the file we're in, this is needed to make the code
 * generation for the properties work.
 */
#define GEGL_CHANT_C_FILE       "brightness-contrast.c"

/* Including gegl-chant.h creates most of the GObject boiler plate
 * needed, creating a GeglChant instance structure a GeglChantClass
 * structure for our operation, as well as the needed code to register
 * our new gobject with GEGL.
 */
#include "gegl-chant.h"

#include "gegl-gpu-types.h"
#include "gegl-gpu-init.h"

static const char* shader_program_str = "                       \
uniform sampler2DRect pixels;                                   \
uniform float brightness, contrast;                             \
                                                                \
void main()                                                     \
{                                                               \
  vec4 pixel = texture2DRect(pixels, gl_TexCoord[0].st);        \
  vec3 color = (pixel.rgb - 0.5) * contrast + brightness + 0.5; \
  gl_FragColor = vec4(color, pixel.a);                          \
}                                                               ";

static GLuint shader_program = 0;
static GLuint pixels_param;
static GLuint brightness_param;
static GLuint contrast_param;

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
  brightness_param = glGetUniformLocation (program, "brightness");
  contrast_param = glGetUniformLocation (program, "contrast");

  return program;
}

/* prepare() is called on each operation providing data to a node that
 * is requested to provide a rendered result. When prepare is called
 * all properties are known. For brightness contrast we use this
 * opportunity to dictate the formats of the input and output buffers.
 */
static void prepare (GeglOperation *operation)
{
  gegl_operation_set_format (operation, "input", babl_format ("RGBA float"));
  gegl_operation_set_format (operation, "output", babl_format ("RGBA float"));
}

/* For GeglOperationPointFilter subclasses, we operate on linear
 * buffers with a pixel count.
 */
static gboolean
process (GeglOperation       *op,
         void                *in_buf,
         void                *out_buf,
         glong                n_pixels,
         const GeglRectangle *roi)
{
  /* Retrieve a pointer to GeglChantO structure which contains all the
   * chanted properties
   */
  GeglChantO *o = GEGL_CHANT_PROPERTIES (op);
  gfloat     *in_pixel;
  gfloat     *out_pixel;
  gfloat      brightness, contrast;
  glong       i;

  in_pixel   = in_buf;
  out_pixel  = out_buf;

  brightness = o->brightness;
  contrast   = o->contrast;

  for (i=0; i<n_pixels; i++)
    {
      gint component;
      for (component=0; component <3 ; component++)
        {
          out_pixel[component] =
                (in_pixel[component] - 0.5) * contrast + brightness + 0.5;
        }
      out_pixel[3] = in_pixel[3]; /* copy the alpha */
      in_pixel  += 4;
      out_pixel += 4;
    }
  return TRUE;
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
  gfloat      brightness = o->brightness;
  gfloat      contrast   = o->contrast;

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
  glUniform1f (brightness_param, brightness);
  glUniform1f (contrast_param, contrast);

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

#ifdef HAS_G4FLOAT
/* The compiler supports vector extensions allowing an version of
 * the process code that produces more optimal instructions on the
 * target platform.
 */

static gboolean
process_simd (GeglOperation       *op,
              void                *in_buf,
              void                *out_buf,
              glong                samples,
              const GeglRectangle *roi)
{
  GeglChantO *o   = GEGL_CHANT_PROPERTIES (op);
  g4float    *in  = in_buf;
  g4float    *out = out_buf;

  /* add 0.5 to brightness here to make the logic in the innerloop tighter
   */
  g4float  brightness = g4float_all(o->brightness + 0.5);
  g4float  contrast   = g4float_all(o->contrast);
  g4float  half       = g4float_half;

  while (samples--)
    {
      *out = (*in - half) * contrast + brightness;
      g4floatA(*out)=g4floatA(*in);
      in  ++;
      out ++;
    }
  return TRUE;
}
#endif

/*
 * The class init function sets up information needed for this operations class
 * (template) in the GObject OO framework.
 */
static void
gegl_chant_class_init (GeglChantClass *klass)
{
  GeglOperationClass            *operation_class;
  GeglOperationPointFilterClass *point_filter_class;

  operation_class    = GEGL_OPERATION_CLASS (klass);
  point_filter_class = GEGL_OPERATION_POINT_FILTER_CLASS (klass);

  /* override the prepare methods of the GeglOperation class */
  operation_class->prepare = prepare;
  /* override the process method of the point filter class (the process methods
   * of our superclasses deal with the handling on their level of abstraction)
   */
  point_filter_class->process = process;

  /* specify the name this operation is found under in the GUI/when
   * programming/in XML
   */
  operation_class->name        = "gegl:brightness-contrast";

  /* a colon separated list of categories/tags for this operations */
  operation_class->categories  = "color";

  /* a description of what this operations does */
  operation_class->description = _("Changes the light level and contrast.");


#ifdef HAS_G4FLOAT
  /* add conditionally compiled variation of process(), gegl should be able
   * to determine which is fastest and hopefully if any implementation is
   * broken and not conforming to the reference implementation.
   */
  gegl_operation_class_add_processor (operation_class,
                                      G_CALLBACK (process_simd), "simd");
#endif
  gegl_operation_class_add_processor (operation_class,
                                      G_CALLBACK (process_gpu),
                                      "gpu:reference");
}

#endif /* closing #ifdef GEGL_CHANT_PROPERTIES ... else ... */
