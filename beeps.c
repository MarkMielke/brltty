/*
 * BRLTTY - Access software for Unix for a blind person
 *          using a soft Braille terminal
 *
 * Version 1.0, 26 July 1996
 *
 * Copyright (C) 1995, 1996 by Nikhil Nair and others.  All rights reserved.
 * BRLTTY comes with ABSOLUTELY NO WARRANTY.
 *
 * This is free software, placed under the terms of the
 * GNU General Public License, as published by the Free Software
 * Foundation.  Please see the file COPYING for details.
 *
 * This software is maintained by Nikhil Nair <nn201@cus.cam.ac.uk>.
 */

#include <sys/ioctl.h>
#include <linux/kd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#include "misc.h"

#include "beeps-songs.h"

short sound;


void
soundstat (short stat)
{
  sound = stat;
}


void
snd (int freq, int del, int consolefd)
{
  (void) ioctl (consolefd, KIOCSOUND, freq);
  shortdelay (del);
}

void
nosnd (int consolefd)
{
  (void) ioctl (consolefd, KIOCSOUND, 0);
}

void
play (int song[])
{
  if (sound)
    {
      int consolefd;
      consolefd = open ("/dev/console", O_WRONLY);
      if (consolefd < 0)
	return;
      while (*song != 0)
	{
	  int freq, del;
	  freq = *(song++);
	  del = *(song++);
	  snd (freq, del, consolefd);
	}
      nosnd (consolefd);
      close (consolefd);
    }
}