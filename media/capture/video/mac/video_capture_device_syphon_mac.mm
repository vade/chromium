// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/capture/video/mac/video_capture_device_syphon_mac.h"

#include <utility>

#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/strings/sys_string_conversions.h"
#include "base/synchronization/lock.h"
#include "third_party/Syphon/mac/Syphon.framework/Headers/Syphon.h"

namespace media {
    
//    static std::string JoinDeviceNameAndFormat(CFStringRef name,
//                                               CFStringRef format) {
//        return base::SysCFStringRefToUTF8(name) + " - " +
//        base::SysCFStringRefToUTF8(format);
//    }
    
    // static
    void VideoCaptureDeviceSyphonMac::EnumerateDevices(
                                                         VideoCaptureDeviceDescriptors* device_descriptors) {
        
        // Get a listing of available Syphon Servers
        @autoreleasepool {
            for(NSDictionary* serverDescription in [SyphonServerDirectory sharedDirectory].servers)
            {
                NSString* serverName = [serverDescription objectForKey:SyphonServerDescriptionNameKey];
                NSString* serverAppName = [serverDescription objectForKey:SyphonServerDescriptionAppNameKey];
                NSString* displayName = [NSString stringWithFormat:@"%@ - %@", serverAppName, serverName];

                VideoCaptureDeviceDescriptor descriptor;
                descriptor.capture_api = VideoCaptureApi::MACOSX_SYPHON;
                descriptor.transport_type = VideoCaptureTransportType::OTHER_TRANSPORT;
                descriptor.display_name = std::string([displayName UTF8String]);
                descriptor.device_id = std::string("Syphon");
                device_descriptors->push_back(descriptor);
                DVLOG(1) << "Syphon Server enumerated: " << descriptor.display_name;
            }
        }
    }
     
    // static
    void VideoCaptureDeviceSyphonMac::EnumerateDeviceCapabilities(
                                                                    const VideoCaptureDeviceDescriptor& device,
                                                                    VideoCaptureFormats* supported_formats) {
    }
    
    VideoCaptureDeviceSyphonMac::VideoCaptureDeviceSyphonMac(
                                                                 const VideoCaptureDeviceDescriptor& device_descriptor)
     {}
    
    VideoCaptureDeviceSyphonMac::~VideoCaptureDeviceSyphonMac() {
    }
    
    void VideoCaptureDeviceSyphonMac::OnIncomingCapturedData(
                                                               const uint8_t* data,
                                                               size_t length,
                                                               const VideoCaptureFormat& frame_format,
                                                               int rotation,  // Clockwise.
                                                               base::TimeTicks reference_time,
                                                               base::TimeDelta timestamp) {
        base::AutoLock lock(lock_);
        if (client_) {
            client_->OnIncomingCapturedData(data, length, frame_format, rotation,
                                            reference_time, timestamp);
        }
    }
    
    void VideoCaptureDeviceSyphonMac::SendErrorString(
                                                        const tracked_objects::Location& from_here,
                                                        const std::string& reason) {
        DCHECK(thread_checker_.CalledOnValidThread());
        base::AutoLock lock(lock_);
        if (client_)
            client_->OnError(from_here, reason);
    }
    
    void VideoCaptureDeviceSyphonMac::SendLogString(const std::string& message) {
        DCHECK(thread_checker_.CalledOnValidThread());
        base::AutoLock lock(lock_);
        if (client_)
            client_->OnLog(message);
    }
    
    void VideoCaptureDeviceSyphonMac::AllocateAndStart(
                                                         const VideoCaptureParams& params,
                                                         std::unique_ptr<VideoCaptureDevice::Client> client) {
        DCHECK(thread_checker_.CalledOnValidThread());
        client_ = std::move(client);
//        if (decklink_capture_delegate_.get())
//            decklink_capture_delegate_->AllocateAndStart(params);
    }
    
    void VideoCaptureDeviceSyphonMac::StopAndDeAllocate() {
//        if (decklink_capture_delegate_.get())
//            decklink_capture_delegate_->StopAndDeAllocate();
    }
    
}  // namespace media
