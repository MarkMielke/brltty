/*
 * BRLTTY - A background process providing access to the console screen (when in
 *          text mode) for a blind person using a refreshable braille display.
 *
 * Copyright (C) 1995-2014 by The BRLTTY Developers.
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

#include "prologue.h"

#include <string.h>

#include "log.h"
#include "async_event.h"
#include "async_signal.h"
#include "async_internal.h"

#ifdef HAVE_POSIX_THREADS
#include <pthread.h>
#endif /* HAVE_POSIX_THREADS */

#ifdef ASYNC_CAN_HANDLE_SIGNALS
struct AsyncSignalDataStruct {
  Queue *signalQueue;

#ifdef ASYNC_CAN_BLOCK_SIGNALS
  sigset_t claimedSignals;
  sigset_t obtainedSignals;
  sigset_t obtainableSignals;
#endif /* ASYNC_CAN_BLOCK_SIGNALS */

#ifdef ASYNC_CAN_OBTAIN_SIGNALS
  int firstObtainableSignal;
  int lastObtainableSignal;
#endif /* ASYNC_CAN_OBTAIN_SIGNALS */
};

void
asyncDeallocateSignalData (AsyncSignalData *sd) {
  if (sd) {
    if (sd->signalQueue) deallocateQueue(sd->signalQueue);
    free(sd);
  }
}

static AsyncSignalData *
getSignalData (void) {
  AsyncThreadSpecificData *tsd = asyncGetThreadSpecificData();
  if (!tsd) return NULL;

  if (!tsd->signalData) {
    AsyncSignalData *sd;

    if (!(sd = malloc(sizeof(*sd)))) {
      logMallocError();
      return NULL;
    }

    memset(sd, 0, sizeof(*sd));
    sd->signalQueue = NULL;

#ifdef ASYNC_CAN_BLOCK_SIGNALS
    sigemptyset(&sd->claimedSignals);
    sigemptyset(&sd->obtainedSignals);
    sigemptyset(&sd->obtainableSignals);
#endif /* ASYNC_CAN_BLOCK_SIGNALS */

#ifdef ASYNC_CAN_OBTAIN_SIGNALS
    sd->firstObtainableSignal = SIGRTMIN;
    sd->lastObtainableSignal = SIGRTMAX;

#ifdef ASYNC_CAN_BLOCK_SIGNALS
    {
      int signalNumber;

      for (signalNumber=sd->firstObtainableSignal; signalNumber<=sd->lastObtainableSignal; signalNumber+=1) {
        sigaddset(&sd->obtainableSignals, signalNumber);
      }
    }
#endif /* ASYNC_CAN_BLOCK_SIGNALS */
#endif /* ASYNC_CAN_OBTAIN_SIGNALS */

    tsd->signalData = sd;
  }

  return tsd->signalData;
}

int
asyncHandleSignal (int signalNumber, AsyncSignalHandler *newHandler, AsyncSignalHandler **oldHandler) {
#ifdef HAVE_SIGACTION
  struct sigaction newAction;
  struct sigaction oldAction;

  memset(&newAction, 0, sizeof(newAction));
  sigemptyset(&newAction.sa_mask);
  newAction.sa_handler = newHandler;

  if (sigaction(signalNumber, &newAction, &oldAction) != -1) {
    if (oldHandler) *oldHandler = oldAction.sa_handler;
    return 1;
  }

  logSystemError("sigaction");
#else /* HAVE_SIGACTION */
  AsyncSignalHandler *result = signal(signalNumber, newHandler);

  if (result != SIG_ERR) {
    if (oldHandler) *oldHandler = result;
    return 1;
  }

  logSystemError("signal");
#endif /* HAVE_SIGACTION */

  return 0;
}

int
asyncIgnoreSignal (int signalNumber, AsyncSignalHandler **oldHandler) {
  return asyncHandleSignal(signalNumber, SIG_IGN, oldHandler);
}

int
asyncRevertSignal (int signalNumber, AsyncSignalHandler **oldHandler) {
  return asyncHandleSignal(signalNumber, SIG_DFL, oldHandler);
}

ASYNC_SIGNAL_HANDLER(asyncEmptySignalHandler) {
}

#ifdef ASYNC_CAN_BLOCK_SIGNALS
static int
setSignalMask (int how, const sigset_t *newMask, sigset_t *oldMask) {
#ifdef HAVE_POSIX_THREADS
  int error = pthread_sigmask(how, newMask, oldMask);

  if (!error) return 1;
  logActionError(error, "pthread_setmask");
#else /* HAVE_POSIX_THREADS */
  if (sigprocmask(how, newMask, oldMask) != -1) return 1;
  logSystemError("sigprocmask");
#endif /* HAVE_POSIX_THREADS */

  return 0;
}

static int
makeSignalMask (sigset_t *signalMask, int signalNumber) {
  if (sigemptyset(signalMask) != -1) {
    if (sigaddset(signalMask, signalNumber) != -1) {
      return 1;
    } else {
      logSystemError("sigaddset");
    }
  } else {
    logSystemError("sigemptyset");
  }

  return 0;
}

int
asyncSetSignalBlocked (int signalNumber, int state) {
  sigset_t mask;

  if (makeSignalMask(&mask, signalNumber)) {
    if (setSignalMask((state? SIG_BLOCK: SIG_UNBLOCK), &mask, NULL)) {
      return 1;
    }
  }

  return 0;
}

static int
getSignalMask (sigset_t *mask) {
  return setSignalMask(SIG_SETMASK, NULL, mask);
}

int
asyncIsSignalBlocked (int signalNumber) {
  sigset_t signalMask;

  if (getSignalMask(&signalMask)) {
    int result = sigismember(&signalMask, signalNumber);

    if (result != -1) return result;
    logSystemError("sigismember");
  }

  return 0;
}

int
asyncCallWithSignalsBlocked (
  const sigset_t *mask,
  AsyncWithSignalsBlockedFunction *function,
  void *data
) {
  sigset_t oldMask;

  if (setSignalMask(SIG_BLOCK, mask, &oldMask)) {
    function(data);
    setSignalMask(SIG_SETMASK, &oldMask, NULL);
    return 1;
  }

  return 0;
}

int
asyncCallWithSignalBlocked (
  int number,
  AsyncWithSignalsBlockedFunction *function,
  void *data
) {
  sigset_t mask;

  if (makeSignalMask(&mask, number)) {
    if (asyncCallWithSignalsBlocked(&mask, function, data)) {
      return 1;
    }
  }

  return 0;
}

int
asyncCallWithAllSignalsBlocked (
  AsyncWithSignalsBlockedFunction *function,
  void *data
) {
  sigset_t mask;

  if (sigfillset(&mask) != -1) {
    if (asyncCallWithSignalsBlocked(&mask, function, data)) {
      return 1;
    }
  } else {
    logSystemError("sigfillset");
  }

  return 0;
}

int
asyncCallWithObtainableSignalsBlocked (
  AsyncWithSignalsBlockedFunction *function,
  void *data
) {
  AsyncSignalData *sd = getSignalData();

  if (sd) {
    if (asyncCallWithSignalsBlocked(&sd->obtainableSignals, function, data)) {
      return 1;
    }
  }

  return 0;
}
#endif /* ASYNC_CAN_BLOCK_SIGNALS */

#ifdef ASYNC_CAN_MONITOR_SIGNALS
typedef struct {
  int number;
  AsyncEvent *event;
  Queue *monitors;

  AsyncSignalHandler *oldHandler;
  unsigned wasBlocked:1;
} SignalEntry;

typedef struct {
  SignalEntry *signal;
  AsyncSignalCallback *callback;
  void *data;
  unsigned active:1;
  unsigned delete:1;
} MonitorEntry;

static void
deallocateMonitorEntry (void *item, void *data) {
  MonitorEntry *mon = item;

  free(mon);
}

static void
deallocateSignalEntry (void *item, void *data) {
  SignalEntry *sig = item;

  deallocateQueue(sig->monitors);
  asyncDiscardEvent(sig->event);
  free(sig);
}

static Queue *
getSignalQueue (int create) {
  AsyncSignalData *sd = getSignalData();
  if (!sd) return NULL;

  if (!sd->signalQueue && create) {
    sd->signalQueue = newQueue(deallocateSignalEntry, NULL);
  }

  return sd->signalQueue;
}

typedef struct {
  SignalEntry *const signalEntry;
} DeleteSignalEntryParameters;

ASYNC_WITH_SIGNALS_BLOCKED_FUNCTION(asyncDeleteSignalEntry) {
  DeleteSignalEntryParameters *parameters = data;
  Queue *signals = getSignalQueue(0);
  Element *signalElement = findElementWithItem(signals, parameters->signalEntry);

  deleteElement(signalElement);
}

static void
deleteMonitor (Element *monitorElement) {
  MonitorEntry *mon = getElementItem(monitorElement);
  SignalEntry *sig = mon->signal;

  logSymbol(LOG_CATEGORY(ASYNC_EVENTS), mon->callback, "signal %d removed", sig->number);
  deleteElement(monitorElement);

  if (getQueueSize(sig->monitors) == 0) {
    asyncHandleSignal(sig->number, sig->oldHandler, NULL);
    asyncSetSignalBlocked(sig->number, sig->wasBlocked);

    {
      DeleteSignalEntryParameters parameters = {
        .signalEntry = sig
      };

      asyncCallWithAllSignalsBlocked(asyncDeleteSignalEntry, &parameters);
    }
  }
}

static void
cancelMonitor (Element *monitorElement) {
  MonitorEntry *mon = getElementItem(monitorElement);

  if (mon->active) {
    mon->delete = 1;
  } else {
    deleteMonitor(monitorElement);
  }
}

ASYNC_EVENT_CALLBACK(asyncHandlePendingSignal) {
  SignalEntry *sig = parameters->eventData;
  Element *monitorElement = getQueueHead(sig->monitors);

  if (monitorElement) {
    MonitorEntry *mon = getElementItem(monitorElement);
    AsyncSignalCallback *callback = mon->callback;

    const AsyncSignalCallbackParameters parameters = {
      .signal = sig->number,
      .data = mon->data
    };

    logSymbol(LOG_CATEGORY(ASYNC_EVENTS), callback, "signal %d starting", sig->number);
    mon->active = 1;
    if (!callback(&parameters)) mon->delete = 1;
    mon->active = 0;
    if (mon->delete) deleteMonitor(monitorElement);
  }
}

typedef struct {
  Queue *const signalQueue;
  SignalEntry *const signalEntry;

  Element *signalElement;
} AddSignalEntryParameters;

ASYNC_WITH_SIGNALS_BLOCKED_FUNCTION(asyncAddSignalEntry) {
  AddSignalEntryParameters *parameters = data;

  parameters->signalElement = enqueueItem(parameters->signalQueue, parameters->signalEntry);
}

typedef struct {
  int signalNumber;
} TestMonitoredSignalKey;

static int
testMonitoredSignal (const void *item, void *data) {
  const SignalEntry *sig = item;
  const TestMonitoredSignalKey *key = data;

  return sig->number == key->signalNumber;
}

static Element *
getSignalElement (int signalNumber, int create) {
  Queue *signals = getSignalQueue(create);

  if (signals) {
    {
      TestMonitoredSignalKey key = {
        .signalNumber = signalNumber
      };

      {
        Element *element = findElement(signals, testMonitoredSignal, &key);

        if (element) return element;
      }
    }

    if (create) {
      SignalEntry *sig;

      if ((sig = malloc(sizeof(*sig)))) {
        memset(sig, 0, sizeof(*sig));
        sig->number = signalNumber;

        if ((sig->event = asyncNewEvent(asyncHandlePendingSignal, sig))) {
          if ((sig->monitors = newQueue(deallocateMonitorEntry, NULL))) {
            {
              static AsyncQueueMethods methods = {
                .cancelRequest = cancelMonitor
              };

              setQueueData(sig->monitors, &methods);
            }

            {
              AddSignalEntryParameters parameters = {
                .signalQueue = signals,
                .signalEntry = sig,

                .signalElement = NULL
              };

              asyncCallWithAllSignalsBlocked(asyncAddSignalEntry, &parameters);
              if (parameters.signalElement) return parameters.signalElement;
            }

            deallocateQueue(sig->monitors);
          }

          asyncDiscardEvent(sig->event);
        }

        free(sig);
      } else {
        logMallocError();
      }
    }
  }

  return NULL;
}

ASYNC_SIGNAL_HANDLER(asyncHandleMonitoredSignal) {
  Element *signalElement = getSignalElement(signalNumber, 0);

  if (signalElement) {
    SignalEntry *sig = getElementItem(signalElement);

    asyncSignalEvent(sig->event, NULL);
  }
}

typedef struct {
  int signal;
  AsyncSignalCallback *callback;
  void *data;
} MonitorElementParameters;

static Element *
newMonitorElement (const void *parameters) {
  const MonitorElementParameters *mep = parameters;
  Element *signalElement = getSignalElement(mep->signal, 1);

  if (signalElement) {
    SignalEntry *sig = getElementItem(signalElement);
    int newSignal = getQueueSize(sig->monitors) == 0;
    MonitorEntry *mon;

    if ((mon = malloc(sizeof(*mon)))) {
      memset(mon, 0, sizeof(*mon));
      mon->signal = sig;
      mon->callback = mep->callback;
      mon->data = mep->data;
      mon->active = 0;
      mon->delete = 0;

      {
        Element *monitorElement = enqueueItem(sig->monitors, mon);

        if (monitorElement) {
          logSymbol(LOG_CATEGORY(ASYNC_EVENTS), mon->callback, "signal %d added", sig->number);
          if (!newSignal) return monitorElement;

          if (asyncHandleSignal(mep->signal, asyncHandleMonitoredSignal, &sig->oldHandler)) {
            sig->wasBlocked = asyncIsSignalBlocked(mep->signal);

            if (!sig->wasBlocked || asyncSetSignalBlocked(sig->number, 0)) {
              return monitorElement;
            }
          }

          deleteElement(monitorElement);
        }
      }

      free(mon);
    } else {
      logMallocError();
    }

    if (newSignal) deleteElement(signalElement);
  }

  return NULL;
}

int
asyncMonitorSignal (
  AsyncHandle *handle, int signal,
  AsyncSignalCallback *callback, void *data
) {
  const MonitorElementParameters mep = {
    .signal = signal,
    .callback = callback,
    .data = data
  };

  return asyncMakeHandle(handle, newMonitorElement, &mep);
}
#endif /* ASYNC_CAN_MONITOR_SIGNALS */

#ifdef ASYNC_CAN_OBTAIN_SIGNALS
int
asyncClaimSignalNumber (int signal) {
  AsyncSignalData *sd = getSignalData();

  if (sd) {
    const char *reason = "signal number not claimable";

    if (sigismember(&sd->obtainableSignals, signal)) {
      if (sigismember(&sd->claimedSignals, signal)) {
        reason = "signal number already claimed";
      } else if (sigismember(&sd->obtainedSignals, signal)) {
        reason = "signal number in use";
      } else {
        sigaddset(&sd->claimedSignals, signal);
        return 1;
      }
    }

    logMessage(LOG_ERR, "%s: %d", reason, signal);
  }

  return 0;
}

int
asyncReleaseSignalNumber (int signal) {
  AsyncSignalData *sd = getSignalData();

  if (sd) {
    if (sigismember(&sd->claimedSignals, signal)) {
      sigdelset(&sd->claimedSignals, signal);
      return 1;
    }
  }

  logMessage(LOG_ERR, "signal number not claimed: %d", signal);
  return 0;
}

int
asyncObtainSignalNumber (void) {
  AsyncSignalData *sd = getSignalData();

  if (sd) {
    int signal;

    for (signal=sd->firstObtainableSignal; signal<=sd->lastObtainableSignal; signal+=1) {
      if (sigismember(&sd->obtainableSignals, signal)) {
        if (!sigismember(&sd->claimedSignals, signal)) {
          if (!sigismember(&sd->obtainedSignals, signal)) {
            sigaddset(&sd->obtainedSignals, signal);
            return signal;
          }
        }
      }
    }
  }

  logMessage(LOG_ERR, "no obtainable signal number");
  return 0;
}

int
asyncRelinquishSignalNumber (int signal) {
  AsyncSignalData *sd = getSignalData();

  if (sd) {
    if (sigismember(&sd->obtainedSignals, signal)) {
      sigdelset(&sd->obtainedSignals, signal);
      return 1;
    }
  }

  logMessage(LOG_ERR, "signal number not obtained: %d", signal);
  return 0;
}

#ifdef ASYNC_CAN_BLOCK_SIGNALS
int
asyncBlockObtainableSignals (void) {
  AsyncSignalData *sd = getSignalData();

  if (sd) {
    if (setSignalMask(SIG_BLOCK, &sd->obtainableSignals, NULL)) {
      return 1;
    }
  }

  return 0;
}
#endif /* ASYNC_CAN_BLOCK_SIGNALS */
#endif /* ASYNC_CAN_OBTAIN_SIGNALS */
#endif /* ASYNC_CAN_HANDLE_SIGNALS */
