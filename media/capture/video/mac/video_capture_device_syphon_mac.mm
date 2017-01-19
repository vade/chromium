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
                NSString* displayName = [serverDescription objectForKey:SyphonServerDescriptionAppNameKey];
                if(serverName.length)
                     displayName = [NSString stringWithFormat:@"%@ - %@", displayName, serverName];
                NSString* deviceID = [serverDescription objectForKey:SyphonServerDescriptionUUIDKey];
                
                VideoCaptureDeviceDescriptor descriptor;
                descriptor.capture_api = VideoCaptureApi::MACOSX_SYPHON;
                descriptor.transport_type = VideoCaptureTransportType::OTHER_TRANSPORT;
                descriptor.display_name = std::string([displayName UTF8String]);
                descriptor.device_id = [deviceID UTF8String];
                device_descriptors->push_back(descriptor);
                NSLog(@"Syphon Server enumerated: %@", serverDescription);
            }
        }
    }
     
    // static
    void VideoCaptureDeviceSyphonMac::EnumerateDeviceCapabilities(
                                                                    const VideoCaptureDeviceDescriptor& device,
                                                                    VideoCaptureFormats* supported_formats) {
        // Frames vended from Syphon Sources can typically change at runtime
        // This means we dont have a set format for the life of a server
        // We are
        
        const media::VideoCaptureFormat format(
                                               gfx::Size(640, 480),
                                               60.0,
                                               PIXEL_FORMAT_RGB32);
        VLOG(3) << device.display_name << " "
        << VideoCaptureFormat::ToString(format);
        supported_formats->push_back(format);
    }
    
    VideoCaptureDeviceSyphonMac::VideoCaptureDeviceSyphonMac(
                                                                 const VideoCaptureDeviceDescriptor& device_descriptor)
     {
        NSLog(@"Request to init Syphon Server with device display name:");
         
         @autoreleasepool {
             // Enumerate Syphon Server Directories to match our Device Descriptors device_id, in which we placed the Syphon Servers UUID
             NSDictionary* selectedServerDictionary = nil;
             NSString* uuidToMatch = [NSString stringWithUTF8String:device_descriptor.device_id.c_str()];
             for(NSDictionary* serverDescription in [SyphonServerDirectory sharedDirectory].servers) {
                 NSString* deviceID = [serverDescription objectForKey:SyphonServerDescriptionUUIDKey];
                 if( [deviceID isEqualToString:uuidToMatch] ) {
                     selectedServerDictionary = serverDescription;
                     break;
                 }
             }
             if (selectedServerDictionary) {
                 syphonClient = [[SyphonClient alloc] initWithServerDescription:selectedServerDictionary options:nil newFrameHandler:NULL];
                 [syphonClient retain];
                 
                 NSOpenGLPixelFormatAttribute attributes[] = {
                     NSOpenGLPFAAllowOfflineRenderers,
                     NSOpenGLPFAAccelerated,
                     NSOpenGLPFAColorSize, 32,
                     NSOpenGLPFADepthSize, 24,
                     NSOpenGLPFANoRecovery,
                     NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersionLegacy
                 };
                 
                 NSOpenGLPixelFormat* format = [[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];
                 
                 context = [[NSOpenGLContext alloc] initWithFormat:format shareContext:nil];
                 [context retain];
             }
         }
     }
    
    VideoCaptureDeviceSyphonMac::~VideoCaptureDeviceSyphonMac() {
        NSLog(@"Request to dealloc Syphon Server");
        @autoreleasepool {
            if(syphonClient != nil) {
                [syphonClient release];
                syphonClient = nil;
            }
            if(context != nil) {
                [context release];
                context = nil;
            }
        }
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
        
        NSLog(@"Request to allocate and start Syphon Server");

        
//        if (decklink_capture_delegate_.get())
//            decklink_capture_delegate_->AllocateAndStart(params);
    }
    
    void VideoCaptureDeviceSyphonMac::StopAndDeAllocate() {
//        if (decklink_capture_delegate_.get())
//            decklink_capture_delegate_->StopAndDeAllocate();
    }
    
}  // namespace media
