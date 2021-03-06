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
#include "parameters.h"
#include "spk_thread.h"
#include "async_wait.h"
#include "async_event.h"
#include "async_thread.h"

#ifdef ENABLE_SPEECH_SUPPORT
typedef enum {
  THD_CONSTRUCTING,
  THD_STARTING,
  THD_READY,
  THD_STOPPING,
  THD_FINISHED
} ThreadState;

typedef struct {
  const char *name;
} ThreadStateEntry;

static const ThreadStateEntry threadStateTable[] = {
  [THD_CONSTRUCTING] = {
    .name = "constructing"
  },

  [THD_STARTING] = {
    .name = "starting"
  },

  [THD_READY] = {
    .name = "ready"
  },

  [THD_STOPPING] = {
    .name = "stopping"
  },

  [THD_FINISHED] {
    .name = "finished"
  },
};

static inline const ThreadStateEntry *
getThreadStateEntry (ThreadState state) {
  if (state >= ARRAY_COUNT(threadStateTable)) return NULL;
  return &threadStateTable[state];
}

typedef enum {
  RSP_PENDING,
  RSP_INTEGER
} SpeechResponseType;

struct SpeechDriverThreadStruct {
  ThreadState threadState;

  volatile SpeechSynthesizer *speechSynthesizer;
  char **driverParameters;

#ifdef ASYNC_CAN_HANDLE_THREADS
  pthread_t threadIdentifier;
  AsyncEvent *requestEvent;
  AsyncEvent *messageEvent;
#endif /* ASYNC_CAN_HANDLE_THREADS */

  struct {
    SpeechResponseType type;

    union {
      int INTEGER;
    } value;
  } response;
};

typedef enum {
  REQ_SAY_TEXT,
  REQ_MUTE_SPEECH,

  REQ_SET_VOLUME,
  REQ_SET_RATE,
  REQ_SET_PITCH,
  REQ_SET_PUNCTUATION
} SpeechRequestType;

typedef struct {
  SpeechRequestType type;

  union {
    struct {
      const unsigned char *text;
      size_t length;
      size_t count;
      const unsigned char *attributes;
    } sayText;

    struct {
      unsigned char setting;
    } setVolume;

    struct {
      unsigned char setting;
    } setRate;

    struct {
      unsigned char setting;
    } setPitch;

    struct {
      SpeechPunctuation setting;
    } setPunctuation;
  } arguments;

  unsigned char data[0];
} SpeechRequest;

typedef struct {
  const void *address;
  size_t size;
  unsigned end:1;
} SpeechDatum;

#define BEGIN_SPEECH_DATA SpeechDatum data[] = {
#define END_SPEECH_DATA {.end=1} };

typedef enum {
  MSG_SPEECH_FINISHED,
  MSG_SPEECH_LOCATION
} SpeechMessageType;

typedef struct {
  SpeechMessageType type;

  union {
    struct {
      int location;
    } speechLocation;
  } arguments;

  unsigned char data[0];
} SpeechMessage;

static void
setThreadState (volatile SpeechDriverThread *sdt, ThreadState state) {
  const ThreadStateEntry *entry = getThreadStateEntry(state);
  const char *name = entry? entry->name: NULL;

  if (!name) name = "?";
  logMessage(LOG_CATEGORY(SPEECH_EVENTS), "driver thread %s", name);
  sdt->threadState = state;
}

static size_t
getSpeechDataSize (const SpeechDatum *data) {
  size_t size = 0;

  if (data) {
    const SpeechDatum *datum = data;

    while (!datum->end) {
      if (datum->address) size += datum->size;
      datum += 1;
    }
  }

  return size;
}

static void
moveSpeechData (unsigned char *target, SpeechDatum *data) {
  if (data) {
    SpeechDatum *datum = data;

    while (!datum->end) {
      if (datum->address) {
        memcpy(target, datum->address, datum->size);
        datum->address = target;
        target += datum->size;
      }

      datum += 1;
    }
  }
}

static void
handleSpeechMessage (volatile SpeechDriverThread *sdt, SpeechMessage *msg) {
  if (msg) {
    switch (msg->type) {
      case MSG_SPEECH_FINISHED:
        setSpeechFinished();
        break;

      case MSG_SPEECH_LOCATION:
        setSpeechLocation(msg->arguments.speechLocation.location);
        break;

      default:
        logMessage(LOG_CATEGORY(SPEECH_EVENTS), "unimplemented driver message type: %u", msg->type);
        break;
    }

    free(msg);
  }
}

static int
sendSpeechMessage (volatile SpeechDriverThread *sdt, SpeechMessage *msg) {
#ifdef ASYNC_CAN_HANDLE_THREADS
  return asyncSignalEvent(sdt->messageEvent, msg);
#else /* ASYNC_CAN_HANDLE_THREADS */
  handleSpeechMessage(sdt, msg);
  return 1;
#endif /* ASYNC_CAN_HANDLE_THREADS */
}

static SpeechMessage *
newSpeechMessage (SpeechMessageType type, SpeechDatum *data) {
  SpeechMessage *msg;
  size_t size = sizeof(*msg) + getSpeechDataSize(data);

  if ((msg = malloc(size))) {
    memset(msg, 0, sizeof(*msg));
    msg->type = type;
    moveSpeechData(msg->data, data);
    return msg;
  } else {
    logMallocError();
  }

  return NULL;
}

int
speechMessage_speechFinished (
  volatile SpeechDriverThread *sdt
) {
  SpeechMessage *msg;

  if ((msg = newSpeechMessage(MSG_SPEECH_FINISHED, NULL))) {
    if (sendSpeechMessage(sdt, msg)) return 1;

    free(msg);
  }

  return 0;
}

int
speechMessage_speechLocation (
  volatile SpeechDriverThread *sdt,
  int location
) {
  SpeechMessage *msg;

  if ((msg = newSpeechMessage(MSG_SPEECH_LOCATION, NULL))) {
    msg->arguments.speechLocation.location = location;
    if (sendSpeechMessage(sdt, msg)) return 1;

    free(msg);
  }

  return 0;
}

static inline void
setResponsePending (volatile SpeechDriverThread *sdt) {
  sdt->response.type = RSP_PENDING;
}

static int
sendIntegerResponse (volatile SpeechDriverThread *sdt, int value) {
  sdt->response.type = RSP_INTEGER;
  sdt->response.value.INTEGER = value;
  return sendSpeechMessage(sdt, NULL);
}

ASYNC_CONDITION_TESTER(testSpeechResponseReceived) {
  volatile SpeechDriverThread *sdt = data;

  return sdt->response.type != RSP_PENDING;
}

static int
awaitSpeechResponse (volatile SpeechDriverThread *sdt, int timeout) {
  return asyncAwaitCondition(timeout, testSpeechResponseReceived, (void *)sdt);
}

static inline int
getIntegerResult (volatile SpeechDriverThread *sdt) {
  if (awaitSpeechResponse(sdt, SPEECH_RESPONSE_WAIT_TIMEOUT)) {
    if (sdt->response.type == RSP_INTEGER) {
      return sdt->response.value.INTEGER;
    }
  }

  return 0;
}

static void
handleSpeechRequest (volatile SpeechDriverThread *sdt, SpeechRequest *req) {
  if (req) {
    switch (req->type) {
      case REQ_SAY_TEXT: {
        speech->say(
          sdt->speechSynthesizer,
          req->arguments.sayText.text, req->arguments.sayText.length,
          req->arguments.sayText.count, req->arguments.sayText.attributes
        );

        sendIntegerResponse(sdt, 1);
        break;
      }

      case REQ_MUTE_SPEECH: {
        speech->mute(sdt->speechSynthesizer);

        sendIntegerResponse(sdt, 1);
        break;
      }

      case REQ_SET_VOLUME: {
        sdt->speechSynthesizer->setVolume(
          sdt->speechSynthesizer,
          req->arguments.setVolume.setting
        );

        sendIntegerResponse(sdt, 1);
        break;
      }

      case REQ_SET_RATE: {
        sdt->speechSynthesizer->setRate(
          sdt->speechSynthesizer,
          req->arguments.setRate.setting
        );

        sendIntegerResponse(sdt, 1);
        break;
      }

      case REQ_SET_PITCH: {
        sdt->speechSynthesizer->setPitch(
          sdt->speechSynthesizer,
          req->arguments.setPitch.setting
        );

        sendIntegerResponse(sdt, 1);
        break;
      }

      case REQ_SET_PUNCTUATION: {
        sdt->speechSynthesizer->setPunctuation(
          sdt->speechSynthesizer,
          req->arguments.setPunctuation.setting
        );

        sendIntegerResponse(sdt, 1);
        break;
      }

      default:
        logMessage(LOG_CATEGORY(SPEECH_EVENTS), "unimplemented speech request type: %u", req->type);
        sendIntegerResponse(sdt, 0);
        break;
    }

    free(req);
  } else {
    setThreadState(sdt, THD_STOPPING);
  }
}

static int
sendSpeechRequest (volatile SpeechDriverThread *sdt, SpeechRequest *req) {
  if (!sdt) return 0;
  setResponsePending(sdt);

#ifdef ASYNC_CAN_HANDLE_THREADS
  return asyncSignalEvent(sdt->requestEvent, req);
#else /* ASYNC_CAN_HANDLE_THREADS */
  handleSpeechRequest(sdt, req);
  return 1;
#endif /* ASYNC_CAN_HANDLE_THREADS */
}

static SpeechRequest *
newSpeechRequest (SpeechRequestType type, SpeechDatum *data) {
  SpeechRequest *req;
  size_t size = sizeof(*req) + getSpeechDataSize(data);

  if ((req = malloc(size))) {
    memset(req, 0, sizeof(*req));
    req->type = type;
    moveSpeechData(req->data, data);
    return req;
  } else {
    logMallocError();
  }

  return NULL;
}

int
speechRequest_sayText (
  volatile SpeechDriverThread *sdt,
  const char *text, size_t length,
  size_t count, const unsigned char *attributes
) {
  SpeechRequest *req;

  BEGIN_SPEECH_DATA
    {.address=text, .size=length+1},
    {.address=attributes, .size=count},
  END_SPEECH_DATA

  if ((req = newSpeechRequest(REQ_SAY_TEXT, data))) {
    req->arguments.sayText.text = data[0].address;
    req->arguments.sayText.length = length;
    req->arguments.sayText.count = count;
    req->arguments.sayText.attributes = data[1].address;
    if (sendSpeechRequest(sdt, req)) return getIntegerResult(sdt);

    free(req);
  }

  return 0;
}

int
speechRequest_muteSpeech (
  volatile SpeechDriverThread *sdt
) {
  SpeechRequest *req;

  if ((req = newSpeechRequest(REQ_MUTE_SPEECH, NULL))) {
    if (sendSpeechRequest(sdt, req)) return getIntegerResult(sdt);

    free(req);
  }

  return 0;
}

int
speechRequest_setVolume (
  volatile SpeechDriverThread *sdt,
  unsigned char setting
) {
  SpeechRequest *req;

  if ((req = newSpeechRequest(REQ_SET_VOLUME, NULL))) {
    req->arguments.setVolume.setting = setting;
    if (sendSpeechRequest(sdt, req)) return getIntegerResult(sdt);

    free(req);
  }

  return 0;
}

int
speechRequest_setRate (
  volatile SpeechDriverThread *sdt,
  unsigned char setting
) {
  SpeechRequest *req;

  if ((req = newSpeechRequest(REQ_SET_RATE, NULL))) {
    req->arguments.setRate.setting = setting;
    if (sendSpeechRequest(sdt, req)) return getIntegerResult(sdt);

    free(req);
  }

  return 0;
}

int
speechRequest_setPitch (
  volatile SpeechDriverThread *sdt,
  unsigned char setting
) {
  SpeechRequest *req;

  if ((req = newSpeechRequest(REQ_SET_PITCH, NULL))) {
    req->arguments.setPitch.setting = setting;
    if (sendSpeechRequest(sdt, req)) return getIntegerResult(sdt);

    free(req);
  }

  return 0;
}

int
speechRequest_setPunctuation (
  volatile SpeechDriverThread *sdt,
  SpeechPunctuation setting
) {
  SpeechRequest *req;

  if ((req = newSpeechRequest(REQ_SET_PUNCTUATION, NULL))) {
    req->arguments.setPunctuation.setting = setting;
    if (sendSpeechRequest(sdt, req)) return getIntegerResult(sdt);

    free(req);
  }

  return 0;
}

static void
setThreadReady (volatile SpeechDriverThread *sdt) {
  setThreadState(sdt, THD_READY);
  sendIntegerResponse(sdt, 1);
}

static int
startSpeechDriver (volatile SpeechDriverThread *sdt) {
  logMessage(LOG_CATEGORY(SPEECH_EVENTS), "starting driver");
  return speech->construct(sdt->speechSynthesizer, sdt->driverParameters);
}

static void
stopSpeechDriver (volatile SpeechDriverThread *sdt) {
  logMessage(LOG_CATEGORY(SPEECH_EVENTS), "stopping driver");
  speech->destruct(sdt->speechSynthesizer);
}

#ifdef ASYNC_CAN_HANDLE_THREADS
ASYNC_CONDITION_TESTER(testSpeechDriverThreadStopping) {
  volatile SpeechDriverThread *sdt = data;

  return sdt->threadState == THD_STOPPING;
}

ASYNC_CONDITION_TESTER(testSpeechDriverThreadFinished) {
  volatile SpeechDriverThread *sdt = data;

  return sdt->threadState == THD_FINISHED;
}

ASYNC_EVENT_CALLBACK(handleSpeechMessageEvent) {
  volatile SpeechDriverThread *sdt = parameters->eventData;
  SpeechMessage *msg = parameters->signalData;

  handleSpeechMessage(sdt, msg);
}

ASYNC_EVENT_CALLBACK(handleSpeechRequestEvent) {
  volatile SpeechDriverThread *sdt = parameters->eventData;
  SpeechRequest *req = parameters->signalData;

  handleSpeechRequest(sdt, req);
}

static void
awaitSpeechDriverThreadTermination (volatile SpeechDriverThread *sdt) {
  void *result;

  pthread_join(sdt->threadIdentifier, &result);
}

ASYNC_THREAD_FUNCTION(runSpeechDriverThread) {
  volatile SpeechDriverThread *sdt = argument;

  setThreadState(sdt, THD_STARTING);

  if ((sdt->requestEvent = asyncNewEvent(handleSpeechRequestEvent, (void *)sdt))) {
    if (startSpeechDriver(sdt)) {
      setThreadReady(sdt);

      while (!asyncAwaitCondition(SPEECH_REQUEST_WAIT_DURATION,
                                  testSpeechDriverThreadStopping, (void *)sdt)) {
      }

      stopSpeechDriver(sdt);
    } else {
      logMessage(LOG_CATEGORY(SPEECH_EVENTS), "driver construction failure");
    }

    asyncDiscardEvent(sdt->requestEvent);
    sdt->requestEvent = NULL;
  } else {
    logMessage(LOG_CATEGORY(SPEECH_EVENTS), "request event construction failure");
  }

  {
    int ok = sdt->threadState == THD_STOPPING;

    setThreadState(sdt, THD_FINISHED);
    sendIntegerResponse(sdt, ok);
  }

  return NULL;
}
#endif /* ASYNC_CAN_HANDLE_THREADS */

volatile SpeechDriverThread *
newSpeechDriverThread (
  volatile SpeechSynthesizer *spk,
  char **parameters
) {
  volatile SpeechDriverThread *sdt;

  if ((sdt = malloc(sizeof(*sdt)))) {
    memset((void *)sdt, 0, sizeof(*sdt));
    setThreadState(sdt, THD_CONSTRUCTING);
    setResponsePending(sdt);

    sdt->speechSynthesizer = spk;
    sdt->driverParameters = parameters;

#ifdef ASYNC_CAN_HANDLE_THREADS
    if ((sdt->messageEvent = asyncNewEvent(handleSpeechMessageEvent, (void *)sdt))) {
      pthread_t threadIdentifier;
      int createError = asyncCreateThread("speech-driver",
                                          &threadIdentifier, NULL,
                                          runSpeechDriverThread, (void *)sdt);

      if (!createError) {
        sdt->threadIdentifier = threadIdentifier;

        if (awaitSpeechResponse(sdt, SPEECH_DRIVER_THREAD_START_TIMEOUT)) {
          if (sdt->response.type == RSP_INTEGER) {
            if (sdt->response.value.INTEGER) {
              return sdt;
            }
          }

          logMessage(LOG_CATEGORY(SPEECH_EVENTS), "driver thread initialization failure");
          awaitSpeechDriverThreadTermination(sdt);
        } else {
          logMessage(LOG_CATEGORY(SPEECH_EVENTS), "driver thread initialization timeout");
        }
      } else {
        logMessage(LOG_CATEGORY(SPEECH_EVENTS), "driver thread creation failure: %s", strerror(createError));
      }

      asyncDiscardEvent(sdt->messageEvent);
      sdt->messageEvent = NULL;
    } else {
      logMessage(LOG_CATEGORY(SPEECH_EVENTS), "response event construction failure");
    }
#else /* ASYNC_CAN_HANDLE_THREADS */
    if (startSpeechDriver(sdt)) {
      setThreadReady(sdt);
      return sdt;
    }
#endif /* ASYNC_CAN_HANDLE_THREADS */

    free((void *)sdt);
  } else {
    logMallocError();
  }

  return NULL;
}

void
destroySpeechDriverThread (
  volatile SpeechDriverThread *sdt
) {
#ifdef ASYNC_CAN_HANDLE_THREADS
  if (sendSpeechRequest(sdt, NULL)) {
    asyncAwaitCondition(SPEECH_DRIVER_THREAD_STOP_TIMEOUT, testSpeechDriverThreadFinished, (void *)sdt);
    awaitSpeechDriverThreadTermination(sdt);
  }

  if (sdt->messageEvent) asyncDiscardEvent(sdt->messageEvent);
#else /* ASYNC_CAN_HANDLE_THREADS */
  stopSpeechDriver(sdt);
  setThreadState(sdt, THD_FINISHED);
#endif /* ASYNC_CAN_HANDLE_THREADS */

  free((void *)sdt);
}
#endif /* ENABLE_SPEECH_SUPPORT */
