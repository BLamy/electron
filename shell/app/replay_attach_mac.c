// Copyright (c) 2026 Record Replay Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// Attaches Replay's driver to this process, if it's being recorded or replayed.
//
// The driver has to attach before the framework's static initializers run, and
// the executable's main only runs after them, as the executable links the
// framework. So this is a library of its own which the framework links: its
// constructor runs before the framework's. Replay Chromium does the same thing
// with an executable which attaches and then loads its framework, in
// chrome/app/record_replay_main.cc.
//
// Recording: with RECORD_ALL_CONTENT set, the browser process and renderers
// attach. REPLAY_ELECTRON_ATTACH restricts that to "main" or "renderer".
//
// Replaying: the engine loads this before the framework, and getenv, dlopen and
// dlsym are its own. Arguments aren't available here: _NSGetArgc gives 0, which
// is how this knows, and always attaches. The engine works out which kind of
// process the recording is from its own arguments.

#include <crt_externs.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef REPLAY_BUILD_ID
#error "Define REPLAY_BUILD_ID and REPLAY_RENDERER_BUILD_ID"
#endif

// Recordings hold the build ID of the process which made them, and the engine
// finds the build to re-run by it. The app also finds the IDs here, by scanning
// for their prefixes.
__attribute__((used)) static const char gBuildId[] = REPLAY_BUILD_ID;
__attribute__((used)) static const char gRendererBuildId[] =
    REPLAY_RENDERER_BUILD_ID;

static void* gDriver;

__attribute__((visibility("default"))) void* ReplayDriverHandle(void) {
  return gDriver;
}

static void (*gFinishRecording)(void);

static void FinishRecording(void) {
  if (gFinishRecording) {
    gFinishRecording();
  }
}

// The value of --type= in this process's arguments, or "main".
static const char* ProcessType(void) {
  int argc = *_NSGetArgc();
  char** argv = *_NSGetArgv();
  for (int i = 1; i < argc; i++) {
    if (!strncmp(argv[i], "--type=", 7)) {
      return argv[i] + 7;
    }
  }
  return "main";
}

static int ShouldRecord(const char* type) {
  if (getenv("RECORD_REPLAY_DONT_RECORD") || getenv("ELECTRON_RUN_AS_NODE") ||
      !getenv("RECORD_ALL_CONTENT")) {
    return 0;
  }
  const char* only = getenv("REPLAY_ELECTRON_ATTACH");
  if (only) {
    return !strcmp(type, only);
  }
  return !strcmp(type, "main") || !strcmp(type, "renderer");
}

// The driver sits beside this library, unless the environment says otherwise.
static void* OpenDriver(void) {
  const char* driver = getenv("RECORD_REPLAY_DRIVER");
  if (driver) {
    return dlopen(driver, RTLD_LAZY);
  }
  Dl_info info;
  if (!dladdr((void*)OpenDriver, &info) || !info.dli_fname) {
    return NULL;
  }
  char path[2048];
  snprintf(path, sizeof(path), "%s", info.dli_fname);
  char* slash = strrchr(path, '/');
  if (!slash) {
    return NULL;
  }
  snprintf(slash + 1, sizeof(path) - (slash + 1 - path),
           "macOS-recordreplay-arm.so");
  return dlopen(path, RTLD_LAZY);
}

// Blink reads its scripts for the debugger from the app, and only knows where they
// are in Replay Chromium's layout. This goes in the recording's environment.
static void SetAssetsDirectory(void) {
  if (getenv("RECORD_REPLAY_ASSETS_DIRECTORY")) {
    return;
  }
  Dl_info info;
  if (!dladdr((void*)SetAssetsDirectory, &info) || !info.dli_fname) {
    return;
  }
  char path[2048];
  snprintf(path, sizeof(path), "%s", info.dli_fname);
  char* contents = strstr(path, "/Contents/Frameworks/");
  if (!contents) {
    return;
  }
  snprintf(contents, sizeof(path) - (contents - path), "/replay-assets");
  setenv("RECORD_REPLAY_ASSETS_DIRECTORY", path, 1);
}

__attribute__((constructor)) static void ReplayAttach(void) {
  int replaying = !*_NSGetArgc();
  const char* type = replaying ? "main" : ProcessType();
  if (!replaying) {
    if (!ShouldRecord(type)) {
      return;
    }
    SetAssetsDirectory();
  }

  void* handle = OpenDriver();
  if (!handle) {
    fprintf(stderr, "replay-attach: can't load the driver: %s\n", dlerror());
    return;
  }

  void (*attach)(const char*) = dlsym(handle, "RecordReplayAttach");
  void (*recordArgs)(int*, char***) =
      dlsym(handle, "RecordReplayRecordCommandLineArguments");
  void (*save)(const char*) = dlsym(handle, "RecordReplaySaveRecording");
  void (*remember)(void) = dlsym(handle, "RecordReplayRememberRecording");
  gFinishRecording = dlsym(handle, "RecordReplayFinishRecording");
  if (!attach || !recordArgs || !save || !remember || !gFinishRecording) {
    fprintf(stderr, "replay-attach: missing driver symbols\n");
    return;
  }

  attach(strcmp(type, "renderer") ? gBuildId : gRendererBuildId);
  recordArgs(_NSGetArgc(), _NSGetArgv());
  save(NULL);
  remember();
  gDriver = handle;

  // A no-op when replaying. The engine finishes where main returns.
  atexit(FinishRecording);
}
