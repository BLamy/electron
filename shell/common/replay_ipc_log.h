// Copyright (c) 2026 Record Replay Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_COMMON_REPLAY_IPC_LOG_H_
#define ELECTRON_SHELL_COMMON_REPLAY_IPC_LOG_H_

#include <string>
#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "third_party/blink/public/common/messaging/cloneable_message.h"

namespace electron::replay {

// When Replay is recording this process, each IPC message it sends or receives
// outside of Electron's own is written to a log, with where it happened in the
// recording. Between them a main process's log and a renderer's say which call
// in one is which in the other, which puts them together as a session: messages
// arrive in the order they're sent. The log's a file written while recording,
// with $REPLAY_ELECTRON_IPC_LOG naming its directory, so nothing about this
// changes what's recorded apart from an execution point being taken.
//
// Returns an ID for what a reply should log, or 0 if nothing's being recorded.
// `peer` is the other process's ID, where this process knows it.
int LogIpc(const char* event,
           const std::string& channel,
           int peer = 0,
           int id = 0);

// Logs a call, and logs its reply when the callback's run.
template <typename Callback>
Callback LogIpcCall(const char* event,
                    const std::string& channel,
                    int peer,
                    Callback callback) {
  int id = LogIpc(event, channel, peer);
  if (!id)
    return callback;
  return base::BindOnce(
      [](const std::string& channel, int peer, int id, Callback callback,
         blink::CloneableMessage message) {
        LogIpc("reply", channel, peer, id);
        std::move(callback).Run(std::move(message));
      },
      channel, peer, id, std::move(callback));
}

}  // namespace electron::replay

#endif  // ELECTRON_SHELL_COMMON_REPLAY_IPC_LOG_H_
