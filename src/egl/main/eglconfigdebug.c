/*
 * Copyright 2017 Imagination Technologies.
 * All Rights Reserved.
 *
 * Based on eglinfo, which has copyright:
 * Copyright (C) 2005  Brian Paul   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * BRIAN PAUL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "eglarray.h"
#include "eglconfig.h"
#include "eglconfigdebug.h"
#include "egldisplay.h"
#include "egllog.h"
#include "egltypedefs.h"

/* Max debug message length */
#define CONFIG_DEBUG_MSG_MAX 1000

/*
 * These are X visual types, so if you're running eglinfo under
 * something not X, they probably don't make sense.
 */
static const char *const vnames[] = { "SG", "GS", "SC", "PC", "TC", "DC" };

struct _printAttributes {
   EGLint id, size, level;
   EGLint red, green, blue, alpha;
   EGLint depth, stencil;
   EGLint renderable, surfaces;
   EGLint vid, vtype, caveat, bindRgb, bindRgba;
   EGLint samples, sampleBuffers;
   char surfString[100];
};

static void
_printHeaderFormat(void)
{
   /*
    * EGL configuration output legend:
    *
    * chosen --------------- eglChooseConfig returned config priority,
    *                        only relevant when eglChooseConfig is called.
    * id ------------------- EGL_CONFIG_ID
    * bfsz ----------------- EGL_BUFFER_SIZE
    * lvl ------------------ EGL_LEVEL
    *
    * colourbuffer
    * r -------------------- EGL_RED_SIZE
    * g -------------------- EGL_GREEN_SIZE
    * b -------------------- EGL_BLUE_SIZE
    * a -------------------- EGL_ALPHA_SIZE
    * dpth ----------------- EGL_DEPTH_SIZE
    * stcl ----------------- EGL_STENCIL_SIZE
    *
    * multisample
    * ns ------------------- EGL_SAMPLES
    * b -------------------- EGL_SAMPLE_BUFFERS
    * visid ---------------- EGL_NATIVE_VISUAL_ID/EGL_NATIVE_VISUAL_TYPE
    * caveat --------------- EGL_CONFIG_CAVEAT
    * bind ----------------- EGL_BIND_TO_TEXTURE_RGB/EGL_BIND_TO_TEXTURE_RGBA
    *
    * renderable
    * gl, es, es2, es3, vg - EGL_RENDERABLE_TYPE
    *
    * supported
    * surfaces ------------- EGL_SURFACE_TYPE
    */
   _eglLog(_EGL_DEBUG, "---------------");
   _eglLog(_EGL_DEBUG, "Configurations:");
   _eglLog(_EGL_DEBUG, "cho       bf lv colourbuffer dp st  ms           vis  cav  bi     renderable           supported");
   _eglLog(_EGL_DEBUG, "sen    id sz  l  r  g  b  a  th cl ns b           id  eat  nd  gl es es2 es3 vg         surfaces");
   _eglLog(_EGL_DEBUG, "---------------");
}

static void
_snprintfStrcat(char *const msg, const int maxSize, const char *fmt, ...)
{
   int maxAllowed;
   va_list args;

   maxAllowed = maxSize - strlen(msg);

   va_start(args, fmt);
   (void) vsnprintf(&msg[strlen(msg)], maxAllowed, fmt, args);
   va_end(args);
}

static void
_eglGetConfigAttrs(_EGLDisplay *const dpy, _EGLConfig *const conf,
                   struct _printAttributes *const attr)
{
   EGLBoolean success = EGL_TRUE;

   success &= _eglGetConfigAttrib(dpy, conf, EGL_CONFIG_ID, &attr->id);
   success &= _eglGetConfigAttrib(dpy, conf, EGL_BUFFER_SIZE, &attr->size);
   success &= _eglGetConfigAttrib(dpy, conf, EGL_LEVEL, &attr->level);

   success &= _eglGetConfigAttrib(dpy, conf, EGL_RED_SIZE, &attr->red);
   success &= _eglGetConfigAttrib(dpy, conf, EGL_GREEN_SIZE, &attr->green);
   success &= _eglGetConfigAttrib(dpy, conf, EGL_BLUE_SIZE, &attr->blue);
   success &= _eglGetConfigAttrib(dpy, conf, EGL_ALPHA_SIZE, &attr->alpha);
   success &= _eglGetConfigAttrib(dpy, conf, EGL_DEPTH_SIZE, &attr->depth);
   success &= _eglGetConfigAttrib(dpy, conf, EGL_STENCIL_SIZE, &attr->stencil);
   success &= _eglGetConfigAttrib(dpy, conf, EGL_NATIVE_VISUAL_ID, &attr->vid);
   success &= _eglGetConfigAttrib(dpy, conf, EGL_NATIVE_VISUAL_TYPE, &attr->vtype);

   success &= _eglGetConfigAttrib(dpy, conf, EGL_CONFIG_CAVEAT, &attr->caveat);
   success &= _eglGetConfigAttrib(dpy, conf, EGL_BIND_TO_TEXTURE_RGB, &attr->bindRgb);
   success &= _eglGetConfigAttrib(dpy, conf, EGL_BIND_TO_TEXTURE_RGBA, &attr->bindRgba);
   success &= _eglGetConfigAttrib(dpy, conf, EGL_RENDERABLE_TYPE, &attr->renderable);
   success &= _eglGetConfigAttrib(dpy, conf, EGL_SURFACE_TYPE, &attr->surfaces);

   success &= _eglGetConfigAttrib(dpy, conf, EGL_SAMPLES, &attr->samples);
   success &= _eglGetConfigAttrib(dpy, conf, EGL_SAMPLE_BUFFERS, &attr->sampleBuffers);

   if (!success)
      _eglLog(_EGL_DEBUG, "%s: config tainted, could not obtain all attributes",
              __func__);
}

static void
_eglPrintConfig(_EGLDisplay *const dpy, _EGLConfig *const conf,
                char *const printMsg, const int maxMsgSize)
{
   struct _printAttributes attr = { 0 };

   _eglGetConfigAttrs(dpy, conf, &attr);

   if (attr.surfaces & EGL_WINDOW_BIT)
      strcat(attr.surfString, "win,");
   if (attr.surfaces & EGL_PBUFFER_BIT)
      strcat(attr.surfString, "pb,");
   if (attr.surfaces & EGL_PIXMAP_BIT)
      strcat(attr.surfString, "pix,");
   if (attr.surfaces & EGL_STREAM_BIT_KHR)
      strcat(attr.surfString, "str,");
   if (attr.surfaces & EGL_SWAP_BEHAVIOR_PRESERVED_BIT)
      strcat(attr.surfString, "prsv,");
   if (strlen(attr.surfString) > 0)
      attr.surfString[strlen(attr.surfString) - 1] = 0;

   _snprintfStrcat(printMsg, maxMsgSize,
                   "0x%03x %2d %2d %2d %2d %2d %2d  %2d %2d %2d%2d 0x%08x%2s     ",
                   attr.id, attr.size, attr.level,
                   attr.red, attr.green, attr.blue, attr.alpha,
                   attr.depth, attr.stencil,
                   attr.samples, attr.sampleBuffers, attr.vid,
                   attr.vtype < 6 ? vnames[attr.vtype] : "--");

   _snprintfStrcat(printMsg, maxMsgSize,
                   "%c  %c   %c  %c   %c   %c   %c %15s",
                   (attr.caveat != EGL_NONE) ? 'y' : ' ',
                   (attr.bindRgba) ? 'a' : (attr.bindRgb) ? 'y' : ' ',
                   (attr.renderable & EGL_OPENGL_BIT) ? 'y' : ' ',
                   (attr.renderable & EGL_OPENGL_ES_BIT) ? 'y' : ' ',
                   (attr.renderable & EGL_OPENGL_ES2_BIT) ? 'y' : ' ',
                   (attr.renderable & EGL_OPENGL_ES3_BIT) ? 'y' : ' ',
                   (attr.renderable & EGL_OPENVG_BIT) ? 'y' : ' ',
                   attr.surfString);

   _eglLog(_EGL_DEBUG, printMsg);
}

static void
_eglMarkChosenConfig(_EGLConfig *const config,
                     _EGLConfig *const *const chosenConfigs,
                     const EGLint numConfigs, char *const printMsg,
                     const int maxMsgSize)
{
   const char padding[] = "   ";

   if (chosenConfigs == NULL) {
      _snprintfStrcat(printMsg, maxMsgSize, "%s ", &padding[0]);
      return;
   }

   /* Find a match, "mark" and return */
   for (EGLint i = 0; i < numConfigs; i++) {
      if (config == chosenConfigs[i]) {
         _snprintfStrcat(printMsg, maxMsgSize, "%*d ", strlen(padding), i);
         return;
      }
   }

   _snprintfStrcat(printMsg, maxMsgSize, "%s ", &padding[0]);
}

static void
_eglPrintConfigs(_EGLDisplay *const dpy,
                 EGLConfig *const configs, const EGLint numConfigs,
                 const enum EGL_CONFIG_DEBUG_OPTION printOption)
{
   const int maxMsgSize = CONFIG_DEBUG_MSG_MAX;
   EGLint numConfigsToPrint;
   _EGLConfig **configsToPrint;
   _EGLConfig **chosenConfigs;
   char *printMsg;

   printMsg = malloc(maxMsgSize);
   if (!printMsg) {
      _eglLog(_EGL_DEBUG, "%s: failed to allocate the print message", __func__);
      return;
   }

   /*
    * If the printout request came from the 'eglChooseConfig', all
    * configs are printed, and the "chosen" configs are marked.
    */
   if (printOption == EGL_CONFIG_DEBUG_CHOOSE) {
      configsToPrint = (_EGLConfig **) dpy->Configs->Elements;
      numConfigsToPrint = dpy->Configs->Size;
      chosenConfigs = (_EGLConfig **) configs;
   } else {
      assert(printOption == EGL_CONFIG_DEBUG_GET);
      configsToPrint = (_EGLConfig **) configs;
      numConfigsToPrint = numConfigs;
      chosenConfigs = NULL;
   }

   _printHeaderFormat();
   for (EGLint i = 0; i < numConfigsToPrint; i++) {
      _EGLConfig *configToPrint = configsToPrint[i];

      /* "clear" message */
      printMsg[0] = '\0';

      _eglMarkChosenConfig(configToPrint, chosenConfigs, numConfigs,
                           printMsg, maxMsgSize);

      _eglPrintConfig(dpy, configToPrint, printMsg, maxMsgSize);
   }

   free(printMsg);
}

void eglPrintConfigDebug(_EGLDisplay *const dpy,
                         EGLConfig *const configs, const EGLint numConfigs,
                         const enum EGL_CONFIG_DEBUG_OPTION printOption)
{
   if (!numConfigs || !configs) {
      _eglLog(_EGL_DEBUG, "%s: nothing to print", __func__);
      return;
   }

   switch (printOption) {
   case EGL_CONFIG_DEBUG_CHOOSE:
   case EGL_CONFIG_DEBUG_GET:
      _eglPrintConfigs(dpy, configs, numConfigs, printOption);
      break;
   default:
      _eglLog(_EGL_DEBUG, "%s: bad debug option", __func__);
      break;
   }
}
