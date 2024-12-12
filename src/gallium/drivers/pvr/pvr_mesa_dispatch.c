/*
 * Mesa 3-D graphics library
 * Version:  7.1
 *
 * Copyright (C) 1999-2007  Brian Paul   All Rights Reserved.
 *
 * The contents of this file are subject to the MIT license as set out below.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdlib.h>
#include <assert.h>

#include "glapi/gen/dispatch.h"
#include "glapi/glapi.h"

#include "main/errors.h"
#include "main/version.h"

#include "pvr/ddk/pvr_ddk_private.h"
#include "pvr/ddk/pvr_dri_compat.h"
#include "pvr/ddk/pvr_dri_support.h"
#include "pvr/ddk/pvr_dri_support_ddk.h"

#include "pvr_context.h"
#include "pvr_mesa_dispatch.h"
#include "pvr_screen.h"

/**
 * This is the default function we plug into all dispatch table slots This
 * helps prevents a segfault when someone calls a GL function without first
 * checking if the extension is supported.
 */
static int
generic_nop(void)
{
   _mesa_warning(NULL, "User called no-op dispatch function (an unsupported extension function?)");

   return 0;
}

/**
 * Allocate and initialise a new dispatch table.
 */
static struct _glapi_table *
pvrdri_alloc_dispatch_table(void)
{
   unsigned int numEntries = _mesa_glapi_get_dispatch_table_size();
   _glapi_proc *table;

   table = malloc(numEntries * sizeof(_glapi_proc));
   if (table)
      for (unsigned int i = 0; i < numEntries; i++)
         table[i] = (_glapi_proc) generic_nop;

   return (struct _glapi_table *) table;
}

/**
 * Return a pointer to the pointer to the dispatch table of an API in
 * PVRDRIScreen.
 */
static struct _glapi_table **
pvrdri_get_dispatch_table_ptr(struct pvr_screen *screen, PVRDRIAPIType eAPI)
{
   switch (eAPI) {
   case PVRDRI_API_GLES1:
      return &screen->ogles1_dispatch;
   case PVRDRI_API_GLES2:
      return &screen->ogles2_dispatch;
   case PVRDRI_API_GL_COMPAT:
   case PVRDRI_API_GL_CORE:
      return &screen->ogl_dispatch;
   default:
      return NULL;
   }
}

/**
 * Return a pointer to the dispatch table of an API.
 */
static struct _glapi_table *
pvrdri_get_dispatch_table(struct pvr_screen *screen, PVRDRIAPIType api)
{
   struct _glapi_table **table =
      pvrdri_get_dispatch_table_ptr(screen, api);

   return table ? *table : NULL;
}

/**
 * Free all dispatch tables.
 */
void
pvrdri_free_dispatch_tables(struct pvr_screen *screen)
{
   if (screen->ogles1_dispatch != NULL) {
      free(screen->ogles1_dispatch);
      screen->ogles1_dispatch = NULL;
   }

   if (screen->ogles2_dispatch != NULL) {
      free(screen->ogles2_dispatch);
      screen->ogles2_dispatch = NULL;
   }

   if (screen->ogl_dispatch != NULL) {
      free(screen->ogl_dispatch);
      screen->ogl_dispatch = NULL;
   }
}

static void
pvrdri_add_mesa_dispatch(struct _glapi_table *table, PVRDRIAPIType api,
                         struct DRISUPScreen *drisup_screen,
                         unsigned int index)
{
   int offset;
   const char *func_name;
   _glapi_proc func;

   func = DRISUPGetAPIProcAddress(drisup_screen, api, index);
   if (func == NULL)
      return;

   func_name = DRISUPGetAPIProcName(drisup_screen, api, index);
   assert(func_name != NULL);

   offset = _mesa_glapi_get_proc_offset(func_name);
   if (offset == -1)
      _mesa_warning(NULL, "Couldn't add %s to the Mesa dispatch table",
                    func_name);
   else
      SET_by_offset(table, offset, func);
}

static void
pvrdri_set_mesa_dispatch(struct _glapi_table *table, PVRDRIAPIType api,
                         struct DRISUPScreen *drisup_screen,
                         unsigned int num_funcs)
{
   for (unsigned int i = 0; i < num_funcs; i++)
      pvrdri_add_mesa_dispatch(table, api, drisup_screen, i);
}

bool
pvrdri_create_dispatch_table(struct pvr_screen *screen, PVRDRIAPIType api)
{
   struct DRISUPScreen *drisup_screen = screen->drisup_screen;
   struct _glapi_table **table;
   unsigned int num_funcs;

   table = pvrdri_get_dispatch_table_ptr(screen, api);
   if (table == NULL)
      return false;

   if (*table != NULL)
      return true;

   num_funcs = DRISUPGetNumAPIProcs(drisup_screen, api);
   if (!num_funcs)
      return false;

   *table = pvrdri_alloc_dispatch_table();
   if (*table == NULL)
      return false;

   pvrdri_set_mesa_dispatch(*table, api, drisup_screen, num_funcs);

   return true;
}

void
pvrdri_set_null_dispatch_table(void)
{
   _mesa_glapi_set_dispatch(NULL);
}

void
pvrdri_set_dispatch_table(struct pvr_context *context)
{
   struct _glapi_table *table;

   table = pvrdri_get_dispatch_table(pvr_screen(context->base.screen),
                                     context->api);

   _mesa_glapi_set_dispatch(table);
}

void
pvrdri_set_null_current_context(void)
{
   _mesa_glapi_set_context(NULL);
}

void
pvrdri_set_current_context(struct pvr_context *context)
{
   _mesa_glapi_set_context(context);
}

struct pvr_context *
pvrdri_get_current_context(void)
{
   return _mesa_glapi_get_context();
}
