// Copyright (c) 2026 Record Replay Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/replay_ipc_log.h"

#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "base/json/string_escape.h"
#include "v8/include/replayio.h"
#include "v8/include/v8.h"

namespace v8 {
// In V8's API, and only used from here and by V8's own bindings for Node.
std::string RecordReplayGetCurrentExecutionPoint();
}  // namespace v8

namespace electron::replay {

namespace {

int g_next_id = 0;

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
  if (!v8::recordreplay::IsRecordingOrReplaying())
    return 0;
  if (!id)
    id = ++g_next_id;

  // These happen on both sides: they make a bookmark and read the recording ID,
  // which are both in the recording. The rest only happens while recording, and
  // isn't recorded.
  std::string point = v8::RecordReplayGetCurrentExecutionPoint();
  const char* recording = v8::recordreplay::GetRecordingId();
  if (v8::recordreplay::IsReplaying())
    return id;

  v8::replayio::AutoPassThroughEvents pass_through;
  static FILE* log = OpenLog();
  if (log) {
    fprintf(log,
            "{\"event\":\"%s\",\"id\":%d,\"channel\":%s,\"peer\":%d,\"pid\":%d,"
            "\"recording\":\"%s\",\"point\":\"%s\",\"ticks\":%llu}\n",
            event, id, base::GetQuotedJSONString(channel).c_str(), peer,
            getpid(), recording ? recording : "", point.c_str(),
            mach_absolute_time());
  }
  return id;
}

}  // namespace electron::replay
