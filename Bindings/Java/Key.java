/*
 * libbrlapi - A library providing access to braille terminals for applications.
 *
 * Copyright (C) 2006-2014 by
 *   Samuel Thibault <Samuel.Thibault@ens-lyon.org>
 *   Sébastien Hinderer <Sebastien.Hinderer@ens-lyon.org>
 *
 * libbrlapi comes with ABSOLUTELY NO WARRANTY.
 *
 * This is free software, placed under the terms of the
 * GNU Lesser General Public License, as published by the Free Software
 * Foundation; either version 2.1 of the License, or (at your option) any
 * later version. Please see the file LICENSE-LGPL for details.
 *
 * Web Page: http://mielke.cc/brltty/
 *
 * This software is maintained by Dave Mielke <dave@mielke.cc>.
 */

package org.a11y.BrlAPI;

public class Key {
  protected final long code;
  protected int type;
  protected int command;
  protected int argument;
  protected int flags;

  public final native void expandKeyCode (long code);

  public Key (long code) {
    this.code = code;
    expandKeyCode(code);
  }

  public long getCode () {
    return code;
  }

  public int getType () {
    return type;
  }

  public int getCommand () {
    return command;
  }

  public int getArgument () {
    return argument;
  }

  public int getFlags () {
    return flags;
  }
}
