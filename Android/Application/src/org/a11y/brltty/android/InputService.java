/*
 * BRLTTY - A background process providing access to the console screen (when in
 *          text mode) for a blind person using a refreshable braille display.
 *
 * Copyright (C) 1995-2013 by The BRLTTY Developers.
 *
 * BRLTTY comes with ABSOLUTELY NO WARRANTY.
 *
 * This is free software, placed under the terms of the
 * GNU General Public License, as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any
 * later version. Please see the file LICENSE-GPL for details.
 *
 * Web Page: http://mielke.cc/brltty/
 *
 * This software is maintained by Dave Mielke <dave@mielke.cc>.
 */

package org.a11y.brltty.android;

import org.a11y.brltty.core.*;

import android.util.Log;

import android.content.Intent;

import android.inputmethodservice.InputMethodService;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.KeyEvent;

public class InputService extends InputMethodService {
  private static final String LOG_TAG = InputService.class.getName();

  private static volatile InputService inputService = null;
  private static int startIdentifier = 0;

  public static InputService getInputService () {
    return inputService;
  }

  @Override
  public void onCreate () {
    super.onCreate();
    Log.d(LOG_TAG, "input service started");
    inputService = this;
  }

  @Override
  public void onDestroy () {
    super.onDestroy();
    Log.d(LOG_TAG, "input service stopped");
    inputService = null;
    startIdentifier = 0;
  }

  @Override
  public void onBindInput () {
    Log.d(LOG_TAG, "input service bound");
  }

  @Override
  public void onUnbindInput () {
    Log.d(LOG_TAG, "input service unbound");
  }

  @Override
  public int onStartCommand (Intent intent, int flags, int identifier) {
    startIdentifier = identifier;
    Log.d(LOG_TAG, "input service starting");
    return START_STICKY;
  }

  @Override
  public void onStartInput (EditorInfo info, boolean restarting) {
    Log.d(LOG_TAG, "input service " + (restarting? "reconnected": "connected"));
    if (info.actionLabel != null) Log.d(LOG_TAG, "action label: " + info.actionLabel);
    Log.d(LOG_TAG, "action id: " + info.actionId);
  }

  @Override
  public void onFinishInput () {
    Log.d(LOG_TAG, "input service disconnected");
  }

  public void forwardKeyEvent (int code, boolean press) {
    InputConnection connection = getCurrentInputConnection();

    if (connection != null) {
      int action = press? KeyEvent.ACTION_DOWN: KeyEvent.ACTION_UP;
      connection.sendKeyEvent(new KeyEvent(action, code));
    }
  }

  public native boolean handleKeyEvent (int code, boolean press);

  public boolean acceptKeyEvent (final int code, final boolean press) {
    switch (code) {
      case KeyEvent.KEYCODE_POWER:
      case KeyEvent.KEYCODE_HOME:
      case KeyEvent.KEYCODE_BACK:
      case KeyEvent.KEYCODE_MENU:
        return false;

      default:
        break;
    }

    CoreWrapper.runOnCoreThread(new Runnable() {
      @Override
      public void run () {
        if (!handleKeyEvent(code, press)) forwardKeyEvent(code, press);
      }
    });

    return true;
  }

  @Override
  public boolean onKeyDown (int code, KeyEvent event) {
    if (acceptKeyEvent(code, true)) return true;
    return super.onKeyDown(code, event);
  }

  @Override
  public boolean onKeyUp (int code, KeyEvent event) {
    if (acceptKeyEvent(code, false)) return true;
    return super.onKeyUp(code, event);
  }
}
