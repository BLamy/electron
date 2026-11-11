// Copyright (c) 2026 Record Replay Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/replay_ipc_log.h"

#include <dlfcn.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "base/json/string_escape.h"

namespace electron::replay {

namespace {

int g_next_id = 0;

// The driver's own functions, which this calls directly: Chromium's record/replay
// API says a browser process isn't recording, as its annotations are off there.
// dlsym is the engine's when replaying, and it answers for the same names.
struct Driver {
  char* (*current_execution_point)() = nullptr;
  void (*free)(void*) = nullptr;
  const char* (*recording_id)() = nullptr;
  bool (*is_replaying)() = nullptr;
  void (*begin_pass_through)() = nullptr;
  void (*end_pass_through)() = nullptr;
  bool attached = false;
};

template <typename T>
void Bind(T& function, const char* name) {
  function = reinterpret_cast<T>(dlsym(RTLD_DEFAULT, name));
}

const Driver& GetDriver() {
  static const Driver driver = [] {
    Driver d;
    if (!dlsym(RTLD_DEFAULT, "RecordReplayAttach"))
      return d;
    Bind(d.current_execution_point, "RecordReplayCurrentExecutionPoint");
    Bind(d.free, "RecordReplayFree");
    Bind(d.recording_id, "RecordReplayGetRecordingId");
    Bind(d.is_replaying, "RecordReplayIsReplaying");
    Bind(d.begin_pass_through, "RecordReplayBeginPassThroughEvents");
    Bind(d.end_pass_through, "RecordReplayEndPassThroughEvents");
    d.attached = d.current_execution_point && d.free && d.recording_id &&
                 d.is_replaying && d.begin_pass_through && d.end_pass_through;
    return d;
  }();
  return driver;
}

FILE* OpenLog() {
  const char* dir = getenv("REPLAY_ELECTRON_IPC_LOG");
  if (!dir)
    return nullptr;
  char path[2048];
  snprintf(path, sizeof(path), "%s/ipc-%d.jsonl", dir, getpid());
  FILE* file = fopen(path, "a");
  if (file) {
    // The process can exit at any point without flushing.
    setvbuf(file, nullptr, _IOLBF, 0);
  }
  return file;
}

}  // namespace

int LogIpc(const char* event,
           const std::string& channel,
           int peer,
           int id) {
  const Driver& driver = GetDriver();
  if (!driver.attached)
    return 0;
  if (!id)
    id = ++g_next_id;

  // These happen on both sides: they make a bookmark and read the recording ID,
  // which are both in the recording. The rest only happens while recording, and
  // isn't recorded.
  char* point_chars = driver.current_execution_point();
  std::string point = point_chars ? point_chars : "";
  driver.free(point_chars);
  const char* recording = driver.recording_id();
  if (driver.is_replaying())
    return id;

  driver.begin_pass_through();
  static FILE* log = OpenLog();
  if (log) {
    fprintf(log,
            "{\"event\":\"%s\",\"id\":%d,\"channel\":%s,\"peer\":%d,\"pid\":%d,"
            "\"recording\":\"%s\",\"point\":\"%s\",\"ticks\":%llu}\n",
            event, id, base::GetQuotedJSONString(channel).c_str(), peer,
            getpid(), recording ? recording : "", point.c_str(),
            mach_absolute_time());
  }
  driver.end_pass_through();
  return id;
}

}  // namespace electron::replay
