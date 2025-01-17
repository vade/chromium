// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/media/media_permission_dispatcher.h"

#include "base/bind.h"
#include "base/logging.h"
#include "base/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "media/base/bind_to_current_loop.h"
#include "third_party/WebKit/public/web/WebUserGestureIndicator.h"
#include "url/gurl.h"

namespace {

using Type = media::MediaPermission::Type;

blink::mojom::PermissionDescriptorPtr MediaPermissionTypeToPermissionDescriptor(
    Type type) {
  auto descriptor = blink::mojom::PermissionDescriptor::New();
  switch (type) {
    case Type::PROTECTED_MEDIA_IDENTIFIER:
      descriptor->name =
          blink::mojom::PermissionName::PROTECTED_MEDIA_IDENTIFIER;
      break;
    case Type::AUDIO_CAPTURE:
      descriptor->name = blink::mojom::PermissionName::AUDIO_CAPTURE;
      break;
    case Type::VIDEO_CAPTURE:
      descriptor->name = blink::mojom::PermissionName::VIDEO_CAPTURE;
      break;
    default:
      NOTREACHED() << type;
      descriptor->name =
          blink::mojom::PermissionName::PROTECTED_MEDIA_IDENTIFIER;
  }
  return descriptor;
}

}  // namespace

namespace content {

MediaPermissionDispatcher::MediaPermissionDispatcher(
    const ConnectToServiceCB& connect_to_service_cb)
    : connect_to_service_cb_(connect_to_service_cb),
      task_runner_(base::ThreadTaskRunnerHandle::Get()),
      next_request_id_(0),
      weak_factory_(this) {
  DCHECK(!connect_to_service_cb_.is_null());
  weak_ptr_ = weak_factory_.GetWeakPtr();
}

MediaPermissionDispatcher::~MediaPermissionDispatcher() {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  // Fire all pending callbacks with |false|.
  for (auto& request : requests_)
    request.second.Run(false);
}

void MediaPermissionDispatcher::HasPermission(
    Type type,
    const GURL& security_origin,
    const PermissionStatusCB& permission_status_cb) {
  if (!task_runner_->RunsTasksInCurrentSequence()) {
    task_runner_->PostTask(
        FROM_HERE, base::Bind(&MediaPermissionDispatcher::HasPermission,
                              weak_ptr_, type, security_origin,
                              media::BindToCurrentLoop(permission_status_cb)));
    return;
  }

  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  if (!permission_service_)
    connect_to_service_cb_.Run(mojo::MakeRequest(&permission_service_));

  int request_id = RegisterCallback(permission_status_cb);
  DVLOG(2) << __func__ << ": request ID " << request_id;

  permission_service_->HasPermission(
      MediaPermissionTypeToPermissionDescriptor(type),
      url::Origin(security_origin),
      base::Bind(&MediaPermissionDispatcher::OnPermissionStatus, weak_ptr_,
                 request_id));
}

void MediaPermissionDispatcher::RequestPermission(
    Type type,
    const GURL& security_origin,
    const PermissionStatusCB& permission_status_cb) {
  if (!task_runner_->RunsTasksInCurrentSequence()) {
    task_runner_->PostTask(
        FROM_HERE, base::Bind(&MediaPermissionDispatcher::RequestPermission,
                              weak_ptr_, type, security_origin,
                              media::BindToCurrentLoop(permission_status_cb)));
    return;
  }

  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  if (!permission_service_)
    connect_to_service_cb_.Run(mojo::MakeRequest(&permission_service_));

  int request_id = RegisterCallback(permission_status_cb);
  DVLOG(2) << __func__ << ": request ID " << request_id;

  permission_service_->RequestPermission(
      MediaPermissionTypeToPermissionDescriptor(type),
      url::Origin(security_origin),
      blink::WebUserGestureIndicator::IsProcessingUserGesture(),
      base::Bind(&MediaPermissionDispatcher::OnPermissionStatus, weak_ptr_,
                 request_id));
}

uint32_t MediaPermissionDispatcher::RegisterCallback(
    const PermissionStatusCB& permission_status_cb) {
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  uint32_t request_id = next_request_id_++;
  DCHECK(!requests_.count(request_id));
  requests_[request_id] = permission_status_cb;

  return request_id;
}

void MediaPermissionDispatcher::OnPermissionStatus(
    uint32_t request_id,
    blink::mojom::PermissionStatus status) {
  DVLOG(2) << __func__ << ": (" << request_id << ", " << status << ")";
  DCHECK(task_runner_->RunsTasksInCurrentSequence());

  RequestMap::iterator iter = requests_.find(request_id);
  DCHECK(iter != requests_.end()) << "Request not found.";

  PermissionStatusCB permission_status_cb = iter->second;
  requests_.erase(iter);

  permission_status_cb.Run(status == blink::mojom::PermissionStatus::GRANTED);
}

}  // namespace content
