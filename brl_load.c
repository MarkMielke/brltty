/*
 * BRLTTY - A background process providing access to the Linux console (when in
 *          text mode) for a blind person using a refreshable braille display.
 *
 * Copyright (C) 1995-2001 by The BRLTTY Team. All rights reserved.
 *
 * BRLTTY comes with ABSOLUTELY NO WARRANTY.
 *
 * This is free software, placed under the terms of the
 * GNU General Public License, as published by the Free Software
 * Foundation.  Please see the file COPYING for details.
 *
 * Web Page: http://mielke.cc/brltty/
 *
 * This software is maintained by Dave Mielke <dave@mielke.cc>.
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <dlfcn.h>

#include "misc.h"
#include "brl.h"

#define BRLSYMBOL noBraille
#define BRLNAME "NoBraille"
#define BRLDRIVER "no"
#define BRLHELP "/dev/null"
#define PREFSTYLE ST_None
#include "brl_driver.h"
static void brl_identify (void) {
  LogPrint(LOG_NOTICE, "No braille support.");
}
static void brl_initialize (char **parameters, brldim *brl, const char *device) { }
static void brl_close (brldim *brl) { }
static void brl_writeWindow (brldim *brl) { }
static void brl_writeStatus (const unsigned char *status) { }
static int brl_read (DriverCommandContext cmds) { return EOF; }

static void *libraryHandle = NULL;	/* handle to driver */
static const char *symbolName = "brl_driver";
braille_driver *braille = NULL;	/* filled by dynamic libs */

/*
 * Output braille translation tables.
 * The files *.auto.h (the default tables) are generated at compile-time.
 */
unsigned char texttrans[0X100] = {
  #include "text.auto.h"
};
unsigned char untexttrans[0X100];

unsigned char attribtrans[0X100] = {
  #include "attrib.auto.h"
};

/*
 * Status cells support 
 * remark: the Papenmeier has a column with 22 cells, 
 * all other terminals use up to 5 bytes
 */
unsigned char statcells[MAXNSTATCELLS];        /* status cell buffer */

/* load driver from library */
/* return true (nonzero) on success */
int loadBrailleDriver (const char **driver) {
  if (*driver != NULL)
    if (strcmp(*driver, noBraille.identifier) == 0) {
      braille = &noBraille;
      *driver = NULL;
      return 1;
    }

  #ifdef BRL_BUILTIN
    {
      extern braille_driver brl_driver;
      if (*driver != NULL)
        if (strcmp(*driver, brl_driver.identifier) == 0)
          *driver = NULL;
      if (*driver == NULL) {
        braille = &brl_driver;
        return 1;
      }
    }
  #else
    if (*driver == NULL) {
      braille = &noBraille;
      return 1;
    }
  #endif

  {
    const char *libraryName = *driver;

    /* allow shortcuts */
    if (strlen(libraryName) == 2) {
      static char buffer[] = "libbrlttyb??.so.1"; /* two ? for shortcut */
      memcpy(strchr(buffer, '?'), libraryName, 2);
      libraryName = buffer;
    }

    if ((libraryHandle = dlopen(libraryName, RTLD_NOW|RTLD_GLOBAL))) {
      const char *error;
      braille = dlsym(libraryHandle, symbolName);
      if (!(error = dlerror())) {
        return 1;
      } else {
        LogPrint(LOG_ERR, "%s", error);
        LogPrint(LOG_ERR, "Braille driver symbol not found: %s", symbolName);
      }
      dlclose(libraryHandle);
      libraryHandle = NULL;
    } else {
      LogPrint(LOG_ERR, "%s", dlerror());
      LogPrint(LOG_ERR, "Cannot open braille driver library: %s", libraryName);
    }
  }

  braille = &noBraille;
  return 0;
}


int listBrailleDrivers (void) {
  char buf[64];
  static const char *list_file = LIB_PATH "/brltty-brl.lst";
  int cnt, fd = open(list_file, O_RDONLY);
  if (fd < 0) {
    fprintf(stderr, "Error: can't access braille driver list file\n");
    perror(list_file);
    return 0;
  }
  fprintf(stderr, "Available Braille Drivers:\n\n");
  fprintf(stderr, "XX\tDescription\n");
  fprintf(stderr, "--\t-----------\n");
  while ((cnt = read(fd, buf, sizeof(buf))))
    fwrite(buf, cnt, 1, stderr);
  close(fd);
  return 1;
}
