// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/client/native_device_keymap.h"

namespace remoting {

uint32_t NativeDeviceKeycodeToUsbKeycode(size_t device_keycode) {
  // For iOS we don't have access to device keycode. The keycode should already
  // be in the usb keycode format, so we can just return the same code.
  return device_keycode;
}

}  // namespace remoting
