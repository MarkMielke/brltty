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

#ifndef _SPK_DRIVER_H
#define _SPK_DRIVER_H

/* this header file is used to create the driver structure
 * for a dynamically loadable speech driver.
 * SPKNAME must be defined - see spkconf.h
 */

#include "spk.h"

/* Routines provided by this speech driver. */
static void spk_identify (void); /* print start-up messages */
static void spk_initialize (char **parameters); /* initialize speech device */
static void spk_say (const unsigned char *buffer, int len); /* speak text */
static void spk_mute (void); /* mute speech */
static void spk_close (void); /* close speech device */

#ifdef SPK_HAVE_EXPRESS
  static void spk_express (const unsigned char *buffer, int len); /* speak text */
#endif

#ifdef SPK_HAVE_TRACK
  static void spk_doTrack (void); /* Get current speaking position */
  static int spk_getTrack (void); /* Get current speaking position */
  static int spk_isSpeaking (void);
#else
  static void spk_doTrack (void) { }
  static int spk_getTrack () { return 0; }
  static int spk_isSpeaking () { return 0; }
#endif

#ifdef SPKPARMS
  static const char *const spk_parameters[] = {SPKPARMS, NULL};
#endif

#ifndef SPKSYMBOL
  #define SPKSYMBOL spk_driver
#endif
speech_driver SPKSYMBOL = {
  SPKNAME,
  SPKDRIVER,

  #ifdef SPKPARMS
    spk_parameters,
  #else
    NULL,
  #endif

  spk_identify,
  spk_initialize,
  spk_say,
  spk_mute,
  spk_close,

  #ifdef SPK_HAVE_EXPRESS
    spk_express,
  #else
    NULL,
  #endif

  spk_doTrack,
  spk_getTrack,
  spk_isSpeaking
};

#endif /* !defined(_SPK_DRIVER_H) */
