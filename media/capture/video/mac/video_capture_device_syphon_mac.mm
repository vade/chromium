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
#include "ui/gl/init/gl_initializer.h"

namespace media {
    
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
        // Frames vended from Syphon Sources can typically change sizes at runtime
        // This means we dont have a set format for the life of a server
        // We are kind of fibbing here. Unsure what the appropriate format info is for dynamic types.
        
        const media::VideoCaptureFormat format(
                                               gfx::Size(640, 480),
                                               0.0f,
                                               PIXEL_FORMAT_RGB32);
        VLOG(3) << device.display_name << " "
        << VideoCaptureFormat::ToString(format);
        supported_formats->push_back(format);
    }
    
    VideoCaptureDeviceSyphonMac::VideoCaptureDeviceSyphonMac(
                                                                 const VideoCaptureDeviceDescriptor& device_descriptor)
     {
        NSLog(@"Request to init Syphon Server with device descriptor %s %s %s", device_descriptor.display_name.c_str(), device_descriptor.device_id.c_str(),  device_descriptor.model_id.c_str());

         run = false;
         
         // Because we run OpenGL in our frame callback to read Syphon Frames off of the GPU
         // We need to be sure to initialize a Chrome Mac Desktop Implementation:
         gl::init::InitializeStaticGLBindings(gl::kGLImplementationAppleGL);
         
         @autoreleasepool {
             
             // Initialize our GL Context
             NSOpenGLPixelFormatAttribute attributes[] = {
                 NSOpenGLPFAAllowOfflineRenderers,
                 NSOpenGLPFAAccelerated,
                 NSOpenGLPFAColorSize, 32,
                 NSOpenGLPFADepthSize, 24,
//                 NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersionLegacy
                 NSOpenGLPFANoRecovery,
             };
             
             NSOpenGLPixelFormat* format = [[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];
             
             context = [[NSOpenGLContext alloc] initWithFormat:format shareContext:nil];
             
             if(context == nil)
             {
                 NSLog(@"Unable to create Context - falling back");
                 NSOpenGLPixelFormatAttribute attributes[] = {
                     NSOpenGLPFAColorSize, 32,
                     NSOpenGLPFADepthSize, 24,
//                     NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersionLegacy
                 };
                 
                 NSOpenGLPixelFormat* format = [[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];
                 
                 context = [[NSOpenGLContext alloc] initWithFormat:format shareContext:nil];
             
                 if(context == nil)
                 {
                     NSLog(@"Unable to create Context - falling back x2");
                     NSOpenGLPixelFormatAttribute attributes[] = {
//                         NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersionLegacy
                     };
                     
                     NSOpenGLPixelFormat* format = [[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];
                     
                     context = [[NSOpenGLContext alloc] initWithFormat:format shareContext:nil];
                 }
             }
             if(context == nil)
             {
                 NSLog(@"Unable to create Context Final");
             }
             
             [context retain];

             // Enumerate Syphon Server Directories to match our Device Descriptors device_id, in which we placed the Syphon Servers UUID
             NSDictionary* selectedServerDictionary = nil;
             NSString* uuidToMatch = [NSString stringWithUTF8String:device_descriptor.device_id.c_str()];
             if(uuidToMatch) {
                 for(NSDictionary* serverDescription in [SyphonServerDirectory sharedDirectory].servers) {
                     NSString* deviceID = [serverDescription objectForKey:SyphonServerDescriptionUUIDKey];
                     if( [deviceID isEqualToString:uuidToMatch] ) {
                         selectedServerDictionary = serverDescription;
                         break;
                     }
                 }
             }
             else {
                 NSLog(@"Unable to match UUID, finding 1st server");
                 // grab the first server we find
                 selectedServerDictionary = [SyphonServerDirectory sharedDirectory].servers[0];
             }
             if (selectedServerDictionary) {
                 NSLog(@"Found Server: %@", selectedServerDictionary);

                 syphonClient = [[SyphonClient alloc] initWithServerDescription:selectedServerDictionary options:nil newFrameHandler:^(SyphonClient *client) {
                     @autoreleasepool {
                         
                         if(run && context) {
                             [context makeCurrentContext];
                             
                             SyphonImage* image = [client newFrameImageForContext:context.CGLContextObj];
                             
                             if(image) {
                                 // Todo: We create and destroy our FBO / Texture every frame
                                 // Lets cache them at some point.
                                 GLuint tempFBO;
                                 GLuint tempTexture;
                                 
                                 glGenTextures(1, &tempTexture);
                                 glBindTexture(GL_TEXTURE_RECTANGLE_EXT, tempTexture);
                                 glTexImage2D(GL_TEXTURE_RECTANGLE_EXT,
                                              0,
                                              GL_RGBA8,
                                              image.textureSize.width,
                                              image.textureSize.height,
                                              0,
                                              GL_BGRA,
                                              GL_UNSIGNED_INT_8_8_8_8_REV,
                                              NULL);
                                 
                                 glGenFramebuffers(1, &tempFBO);
                                 glBindFramebuffer(GL_FRAMEBUFFER, tempFBO);
                                 glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE_EXT, tempTexture, 0);
                                 
                                 // things go according to plan?
                                 GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
                                 if(status != GL_FRAMEBUFFER_COMPLETE)
                                 {
                                     NSLog(@"could not create FBO - bailing");
                                 }
                                 
                                 glViewport(0, 0, image.textureSize.width, image.textureSize.height);
                                 glOrtho(0, image.textureSize.width, 0, image.textureSize.height, -1, 1);
                                 
                                 glMatrixMode(GL_PROJECTION);
                                 glLoadIdentity();
                                 
                                 glMatrixMode(GL_MODELVIEW);
                                 glLoadIdentity();
                                 
                                 // render into FBO
                                 glColor4f(1.0, 1.0, 1.0, 1.0);
                                 glClearColor(1.0, 0, 0, 0);
                                 
                                 glClear(GL_COLOR_BUFFER_BIT);
                                 
                                 GLfloat tex_coords[] =
                                 {
                                     image.textureSize.width,image.textureSize.height,
                                     0.0,image.textureSize.height,
                                     0.0,0.0,
                                     image.textureSize.width,0.0
                                 };
                                 
                                 GLfloat vertexCoords[8] =
                                 {
                                     1.0,	-1.0,
                                     -1.0,	-1.0,
                                     -1.0,	 1.0,
                                     1.0,	 1.0
                                 };
                                 
                                 GLuint textureName = image.textureName;
                                 glEnable(GL_TEXTURE_RECTANGLE_EXT);
                                 glBindTexture(GL_TEXTURE_RECTANGLE_EXT, textureName);
                                 glTexParameterf(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
                                 glTexParameterf(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                                 glTexParameterf(GL_TEXTURE_RECTANGLE_EXT, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                                 glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
                                 glDisable(GL_BLEND);
                                 
                                 glEnableClientState( GL_TEXTURE_COORD_ARRAY );
                                 glTexCoordPointer(2, GL_FLOAT, 0, tex_coords );
                                 glEnableClientState(GL_VERTEX_ARRAY);
                                 glVertexPointer(2, GL_FLOAT, 0, vertexCoords );
                                 glDrawArrays( GL_TRIANGLE_FAN, 0, 4 );
                                 glDisableClientState( GL_TEXTURE_COORD_ARRAY );
                                 glDisableClientState(GL_VERTEX_ARRAY);
                                 
                                 size_t textureDataLength = 4 * sizeof(int) * image.textureSize.width * image.textureSize.height;
                                 const uint8_t* textureData = (const uint8_t*)malloc(textureDataLength);
                                 
                                 glEnable(GL_TEXTURE_RECTANGLE_EXT);
                                 glBindTexture(GL_TEXTURE_RECTANGLE_EXT, tempTexture);
                                 glGetTexImage(GL_TEXTURE_RECTANGLE_EXT, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, (void*)textureData);

                                 // GL Syncronize contents of FBO / texture
                                 glFlushRenderAPPLE();

                                 const media::VideoCaptureFormat capture_format(
                                                                                gfx::Size(image.textureSize.width, image.textureSize.height),
                                                                                0.0f,
                                                                                media::PIXEL_FORMAT_ARGB);
                                 // We are not currently tracking frame deltas
                                 // We assume 60Hz here.
                                 // Todo: Track frame delta ms
                                 this->OnIncomingCapturedData(textureData,
                                                              textureDataLength,
                                                              capture_format,
                                                              0,
                                                              base::TimeTicks::Now(),
                                                              base::TimeDelta::FromMilliseconds(16));
                                 
                                 free((void*)textureData);
                                 
                                 glDeleteFramebuffers(1, &tempFBO);
                                 glDeleteTextures(1, &tempTexture);
                             }
                         }
                         else if(!context) {
                             NSLog(@"Syphon Client Recieved Frame - No Context");
                         }
                         else if(!run) {
                             NSLog(@"Syphon Client Recieved Frame - Not Running");
                         }
                         
                     }
                 }];
              
                 [syphonClient retain];
             }
             else {
                 NSLog(@"Unable to find Syphon Server for device_description");
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
        run = true;
    }
    
    void VideoCaptureDeviceSyphonMac::StopAndDeAllocate() {
        run = false;
        [syphonClient stop];
    }
    
}  // namespace media
