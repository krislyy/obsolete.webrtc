﻿
// Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS.  All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.

#include "webrtc/build/WinRT_gyp/Api/Media.h"
#include <stdio.h>
#include <ppltasks.h>
#include <mfapi.h>
#include <vector>
#include <string>
#include <set>
#include "PeerConnectionInterface.h"
#include "Marshalling.h"
#include "RTMediaStreamSource.h"
#include "WebRtcMediaSource.h"
#include "webrtc/base/logging.h"
#include "talk/app/webrtc/videosourceinterface.h"
#include "talk/session/media/channelmanager.h"
#include "talk/media/base/mediaengine.h"
#include "webrtc/modules/audio_device/audio_device_config.h"
#include "webrtc/modules/audio_device/audio_device_impl.h"
#include "webrtc/modules/audio_device/include/audio_device_defines.h"
#include "webrtc/modules/video_capture/windows/device_info_winrt.h"
#include "webrtc/modules/video_capture/windows/video_capture_winrt.h"
#include "webrtc/system_wrappers/include/critical_section_wrapper.h"
#include "webrtc/voice_engine/include/voe_hardware.h"

using Platform::Collections::Vector;
using webrtc_winrt_api_internal::ToCx;
using webrtc_winrt_api_internal::FromCx;
using Windows::Media::Capture::MediaStreamType;
using Windows::Devices::Enumeration::DeviceClass;
using Windows::Devices::Enumeration::DeviceInformation;
using Windows::Devices::Enumeration::DeviceWatcherStatus;
using Windows::Foundation::TypedEventHandler;

namespace {
  std::vector<cricket::Device> g_videoDevices;
  std::vector<cricket::Device> g_audioCapturerDevices;
  std::vector<cricket::Device> g_audioPlayoutDevices;

  webrtc::CriticalSectionWrapper& g_videoDevicesLock(
    *webrtc::CriticalSectionWrapper::CreateCriticalSection());
  webrtc::CriticalSectionWrapper& g_audioCapturerDevicesLock(
    *webrtc::CriticalSectionWrapper::CreateCriticalSection());
  webrtc::CriticalSectionWrapper& g_audioPlayoutDevicesLock(
    *webrtc::CriticalSectionWrapper::CreateCriticalSection());
}

namespace webrtc_winrt_api {

// = MediaVideoTrack =========================================================

MediaVideoTrack::MediaVideoTrack(
  rtc::scoped_refptr<webrtc::VideoTrackInterface> impl) :
  _impl(impl) {
}

MediaVideoTrack::~MediaVideoTrack() {
}

String^ MediaVideoTrack::Kind::get() {
  return ToCx(_impl->kind());
}

String^ MediaVideoTrack::Id::get() {
  return ToCx(_impl->id());
}

bool MediaVideoTrack::Enabled::get() {
  return _impl->enabled();
}

void MediaVideoTrack::Enabled::set(bool value) {
  _impl->set_enabled(value);
}

bool MediaVideoTrack::Suspended::get() {
  return _impl->GetSource()->IsSuspended();
}

void MediaVideoTrack::Suspended::set(bool value) {
  if (value) {
    _impl->GetSource()->Suspend();
  } else {
    _impl->GetSource()->Resume();
  }
}

void MediaVideoTrack::Stop() {
  _impl->GetSource()->Stop();
}

void MediaVideoTrack::SetRenderer(webrtc::VideoRendererInterface* renderer) {
  _impl->AddRenderer(renderer);
}

void MediaVideoTrack::UnsetRenderer(webrtc::VideoRendererInterface* renderer) {
  _impl->RemoveRenderer(renderer);
}

// = MediaAudioTrack =========================================================

MediaAudioTrack::MediaAudioTrack(
  rtc::scoped_refptr<webrtc::AudioTrackInterface> impl) :
  _impl(impl) {
}

String^ MediaAudioTrack::Kind::get() {
  return ToCx(_impl->kind());
}

String^ MediaAudioTrack::Id::get() {
  return ToCx(_impl->id());
}

bool MediaAudioTrack::Enabled::get() {
  return _impl->enabled();
}

void MediaAudioTrack::Enabled::set(bool value) {
  _impl->set_enabled(value);
}

void MediaAudioTrack::Stop() {
}

// = MediaStream =============================================================

MediaStream::MediaStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> impl)
  : _impl(impl) {
}

MediaStream::~MediaStream() {
  LOG(LS_INFO) << "MediaStream::~MediaStream";
}

rtc::scoped_refptr<webrtc::MediaStreamInterface> MediaStream::GetImpl() {
  return _impl;
}

IVector<MediaAudioTrack^>^ MediaStream::GetAudioTracks() {
  if (_impl == nullptr)
    return nullptr;
  auto ret = ref new Vector<MediaAudioTrack^>();
  for (auto track : _impl->GetAudioTracks()) {
    ret->Append(ref new MediaAudioTrack(track));
  }
  return ret;
}

String^ MediaStream::Id::get() {
  if (_impl == nullptr)
    return nullptr;
  return ToCx(_impl->label());
}

IVector<MediaVideoTrack^>^ MediaStream::GetVideoTracks() {
  if (_impl == nullptr)
    return nullptr;
  auto ret = ref new Vector<MediaVideoTrack^>();
  for (auto track : _impl->GetVideoTracks()) {
    ret->Append(ref new MediaVideoTrack(track));
  }
  return ret;
}

IVector<IMediaStreamTrack^>^ MediaStream::GetTracks() {
  if (_impl == nullptr)
    return nullptr;
  auto ret = ref new Vector<IMediaStreamTrack^>();
  for (auto track : _impl->GetAudioTracks()) {
    ret->Append(ref new MediaAudioTrack(track));
  }
  for (auto track : _impl->GetVideoTracks()) {
    ret->Append(ref new MediaVideoTrack(track));
  }
  return ret;
}

IMediaStreamTrack^ MediaStream::GetTrackById(String^ trackId) {
  if (_impl == nullptr)
    return nullptr;
  IMediaStreamTrack^ ret = nullptr;
  std::string trackIdStr = FromCx(trackId);
  // Search the audio tracks.
  auto audioTrack = _impl->FindAudioTrack(trackIdStr);
  if (audioTrack != nullptr) {
    ret = ref new MediaAudioTrack(audioTrack);
  } else {
    // Search the video tracks.
    auto videoTrack = _impl->FindVideoTrack(trackIdStr);
    if (videoTrack != nullptr) {
      ret = ref new MediaVideoTrack(videoTrack);
    }
  }
  return ret;
}

void MediaStream::AddTrack(IMediaStreamTrack^ track) {
  if (_impl == nullptr)
    return;
  std::string kind = FromCx(track->Kind);
  if (kind == "audio") {
    auto audioTrack = static_cast<MediaAudioTrack^>(track);
    _impl->AddTrack(audioTrack->GetImpl());
  } else if (kind == "video") {
    auto videoTrack = static_cast<MediaVideoTrack^>(track);
    _impl->AddTrack(videoTrack->GetImpl());
  } else {
    throw "Unknown track kind";
  }
}

void MediaStream::RemoveTrack(IMediaStreamTrack^ track) {
  if (_impl == nullptr)
    return;
  std::string kind = FromCx(track->Kind);
  if (kind == "audio") {
    auto audioTrack = static_cast<MediaAudioTrack^>(track);
    _impl->RemoveTrack(audioTrack->GetImpl());
  } else if (kind == "video") {
    auto videoTrack = static_cast<MediaVideoTrack^>(track);
    _impl->RemoveTrack(videoTrack->GetImpl());
  } else {
    throw "Unknown track kind";
  }
}

void MediaStream::Stop() {
  // TODO(winrt): Investigate if this is the proper way
  // to stop the stream. If something else holds
  // a reference, the stream may not stop.
  _impl = nullptr;
}

bool MediaStream::Active::get() {
  if (_impl == nullptr)
    return false;
  bool ret = false;
  for (auto track : _impl->GetAudioTracks()) {
    if (track->state() < webrtc::MediaStreamTrackInterface::kEnded) {
      ret = true;
    }
  }
  for (auto track : _impl->GetVideoTracks()) {
    if (track->state() < webrtc::MediaStreamTrackInterface::kEnded) {
      ret = true;
    }
  }
  return ret;
}

// = Media ===================================================================

const char kAudioLabel[] = "audio_label_%x";
const char kVideoLabel[] = "video_label_%x";
const char kStreamLabel[] = "stream_label_%x";
// we will append current time (uint32 in Hex, e.g.:
// 8chars to the end to generate a unique string)

Media::Media() :
  _selectedAudioCapturerDevice(
    cricket::DeviceManagerInterface::kDefaultDeviceName, 0),
  _selectedAudioPlayoutDevice(
    cricket::DeviceManagerInterface::kDefaultDeviceName, 0),
  _videoCaptureDeviceChanged(true),
  _audioCaptureDeviceChanged(true),
  _audioPlayoutDeviceChanged(true) {
  _dev_manager = rtc::scoped_ptr<cricket::DeviceManagerInterface>
    (cricket::DeviceManagerFactory::Create());

  if (!_dev_manager->Init()) {
    LOG(LS_ERROR) << "Can't create device manager";
    return;
  }
  SubscribeToMediaDeviceChanges();
}

Media::~Media() {
  UnsubscribeFromMediaDeviceChanges();
}

// TODO(winrt): Remove this function and always use the async one.
Media^ Media::CreateMedia() {
  return ref new Media();
}

IAsyncOperation<Media^>^ Media::CreateMediaAsync() {
  IAsyncOperation<Media^>^ asyncOp = Concurrency::create_async([]()->Media^ {
    return CreateMedia();
  });
  return asyncOp;
}

IAsyncOperation<MediaStream^>^ Media::GetUserMedia(
  RTCMediaStreamConstraints^ mediaStreamConstraints) {
  // TODO(WINRT): error handling - no permissions, no device for media type...
  // add to separate sets of constraints
  IAsyncOperation<MediaStream^>^ asyncOp = Concurrency::create_async(
    [this, mediaStreamConstraints]() -> MediaStream^ {
    return globals::RunOnGlobalThread<MediaStream^>([this,
                                      mediaStreamConstraints]()->MediaStream^ {
      // This is the stream returned.
      char streamLabel[32];
      _snprintf(streamLabel, sizeof(streamLabel), kStreamLabel,
        rtc::CreateRandomId64());
      rtc::scoped_refptr<webrtc::MediaStreamInterface> stream =
        globals::gPeerConnectionFactory->CreateLocalMediaStream(streamLabel);

      if (mediaStreamConstraints->audioEnabled) {
        // Check if audio devices candidates are still available.
        // Application may request to use audio devices that are not
        // connected anymore. In this case, fallback to default device.
        webrtc::VoEHardware* voiceEngineHardware =
          globals::gPeerConnectionFactory->channel_manager()->media_engine()->
          GetVoEHardware();
        bool useDefaultAudioPlayoutDevice = true;
        bool useDefaultAudioRecordingDevice = true;
        if (voiceEngineHardware == nullptr) {
          LOG(LS_ERROR) << "Can't validate audio devices: "
            << "VoEHardware API not available.";
        } else {
          int deviceCount(0);
          char audioDeviceName[128];
          char audioDeviceGuid[128];

          if (voiceEngineHardware->GetNumOfRecordingDevices(deviceCount) == 0) {
            for (int i = 0; i < deviceCount; ++i) {
              voiceEngineHardware->GetRecordingDeviceName(i, audioDeviceName,
                audioDeviceGuid);
              std::string webrtc_name(audioDeviceName);
              if (_selectedAudioCapturerDevice.name.compare(0,
                strlen(audioDeviceName), audioDeviceName) == 0) {
                useDefaultAudioRecordingDevice = false;
                break;
              }
            }
            if (useDefaultAudioRecordingDevice) {
              LOG(LS_WARNING) << "Audio capture device "
                << _selectedAudioCapturerDevice.name
                << " not found, using default device";
            }
          } else {
            LOG(LS_ERROR) << "Can't obtain audio recording audio devices.";
          }

          if (voiceEngineHardware->GetNumOfPlayoutDevices(deviceCount) == 0) {
            for (int i = 0; i < deviceCount; ++i) {
              voiceEngineHardware->GetPlayoutDeviceName(i, audioDeviceName,
                audioDeviceGuid);
              if (_selectedAudioPlayoutDevice.name.compare(0,
                strlen(audioDeviceName), audioDeviceName) == 0) {
                useDefaultAudioPlayoutDevice = false;
                break;
              }
            }
            if (useDefaultAudioPlayoutDevice) {
              LOG(LS_WARNING) << "Audio playout device "
                << _selectedAudioPlayoutDevice.name
                << " not found, using default device";
            }
          } else {
            LOG(LS_ERROR) << "Can't obtain audio playout devices.";
          }
        }

        cricket::Device* audioCaptureDevice = useDefaultAudioRecordingDevice ?
          nullptr : &_selectedAudioCapturerDevice;
        cricket::Device* audioPlayoutDevice = useDefaultAudioPlayoutDevice ?
          nullptr : &_selectedAudioPlayoutDevice;

        if (!globals::gPeerConnectionFactory->channel_manager()
          ->media_engine()->SetSoundDevices(
          audioCaptureDevice,
          audioPlayoutDevice)) {
          LOG(LS_ERROR) << "Failed to set audio devices.";
        }

        LOG(LS_INFO) << "Creating audio track.";
        char audioLabel[32];
        _snprintf(audioLabel, sizeof(audioLabel), kAudioLabel,
          rtc::CreateRandomId64());
        rtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track(
          globals::gPeerConnectionFactory->CreateAudioTrack(
            audioLabel,
            globals::gPeerConnectionFactory->CreateAudioSource(NULL)));
        LOG(LS_INFO) << "Adding audio track to stream.";
        stream->AddTrack(audio_track);
      }

      if (mediaStreamConstraints->videoEnabled) {
        cricket::VideoCapturer* videoCapturer = NULL;
        if (_selectedVideoDevice.id == "") {
          // Select the first video device as the capturer.
          webrtc::CriticalSectionScoped cs(&g_videoDevicesLock);
          for (auto videoDev : g_videoDevices) {
            videoCapturer = _dev_manager->CreateVideoCapturer(videoDev);
            if (videoCapturer != NULL)
              break;
          }
        } else {
          videoCapturer = _dev_manager->CreateVideoCapturer(
                                                        _selectedVideoDevice);
        }
        char videoLabel[32];
        _snprintf(videoLabel, sizeof(videoLabel), kVideoLabel,
          rtc::CreateRandomId64());

        // Add a video track
        if (videoCapturer != nullptr) {
          LOG(LS_INFO) << "Creating video track.";
          rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track(
            globals::gPeerConnectionFactory->CreateVideoTrack(
            videoLabel,
            globals::gPeerConnectionFactory->CreateVideoSource(
            videoCapturer, NULL)));
          LOG(LS_INFO) << "Adding video track to stream.";
          stream->AddTrack(video_track);
        }
      }

      auto ret = ref new MediaStream(stream);
      return ret;
    });
  });

  return asyncOp;
}

IMediaSource^ Media::CreateMediaStreamSource(
  MediaVideoTrack^ track, uint32 framerate, String^ id) {
  return globals::RunOnGlobalThread<MediaStreamSource^>([track, framerate,
    id]()->MediaStreamSource^ {
    return webrtc_winrt_api_internal::RTMediaStreamSource::
      CreateMediaSource(track, framerate, id);
  });
}

IMediaSource^ Media::CreateMediaSource(
  MediaVideoTrack^ track, String^ id) {
  return globals::RunOnGlobalThread<IMediaSource^>([track, id]() -> IMediaSource^ {
    ComPtr<ABI::Windows::Media::Core::IMediaSource> comSource;
    webrtc_winrt_api_internal::WebRtcMediaSource::CreateMediaSource(&comSource, track, id);
    IMediaSource^ source = reinterpret_cast<IMediaSource^>(comSource.Get());
    return source;
  });
}

IVector<MediaDevice^>^ Media::GetVideoCaptureDevices() {
  webrtc::CriticalSectionScoped cs(&g_videoDevicesLock);
  auto ret = ref new Vector<MediaDevice^>();
  if (_videoCaptureDeviceChanged) {
    globals::RunOnGlobalThread<void>([this] {
      g_videoDevices.clear();
      if (!_dev_manager->GetVideoCaptureDevices(&g_videoDevices)) {
        LOG(LS_ERROR) << "Can't enumerate video capture devices";
      }
    });
    _videoCaptureDeviceChanged = false;
  }
  for (auto videoDev : g_videoDevices) {
    ret->Append(ref new MediaDevice(ToCx(videoDev.id), ToCx(videoDev.name)));
  }
  return ret;
}

IVector<MediaDevice^>^ Media::GetAudioCaptureDevices() {
  webrtc::CriticalSectionScoped cs(&g_audioCapturerDevicesLock);
  auto ret = ref new Vector<MediaDevice^>();
  if (_audioCaptureDeviceChanged) {
    g_audioCapturerDevices.clear();
    globals::RunOnGlobalThread<void>([this] {
      webrtc::VoEHardware* voiceEngineHardware =
        globals::gPeerConnectionFactory->channel_manager()->media_engine()->
        GetVoEHardware();
      if (voiceEngineHardware == nullptr) {
        LOG(LS_ERROR) << "Can't enumerate audio capture devices: "
          << "VoEHardware API not available.";
        return;
      }
      int recordingDeviceCount(0);
      char audioDeviceName[128];
      char audioDeviceGuid[128];
      if (voiceEngineHardware->GetNumOfRecordingDevices(recordingDeviceCount) == 0) {
        for (int i = 0; i < recordingDeviceCount; ++i) {
          voiceEngineHardware->GetRecordingDeviceName(i, audioDeviceName,
            audioDeviceGuid);
          g_audioCapturerDevices.push_back(cricket::Device(
            std::string(audioDeviceName),
            std::string(audioDeviceGuid)));
        }
      } else {
        LOG(LS_ERROR) << "Can't enumerate audio capture devices";
      }
    });
    _audioCaptureDeviceChanged = false;
  }
  for (auto videoDev : g_audioCapturerDevices) {
    ret->Append(ref new MediaDevice(ToCx(videoDev.id), ToCx(videoDev.name)));
  }
  return ret;
}

IVector<MediaDevice^>^ Media::GetAudioPlayoutDevices() {
  webrtc::CriticalSectionScoped cs(&g_audioPlayoutDevicesLock);
  auto ret = ref new Vector<MediaDevice^>();
  if (_audioPlayoutDeviceChanged) {
    g_audioPlayoutDevices.clear();
    globals::RunOnGlobalThread<void>([this] {
      webrtc::VoEHardware* voiceEngineHardware =
        globals::gPeerConnectionFactory->channel_manager()->media_engine()->
        GetVoEHardware();
      if (voiceEngineHardware == nullptr) {
        LOG(LS_ERROR) << "Can't enumerate audio playout devices: "
          << "VoEHardware API not available.";
        return;
      }
      int playoutDeviceCount(0);
      char audioDeviceName[128];
      char audioDeviceGuid[128];
      if (voiceEngineHardware->GetNumOfPlayoutDevices(playoutDeviceCount) == 0) {
        for (int i = 0; i < playoutDeviceCount; ++i) {
          voiceEngineHardware->GetPlayoutDeviceName(i, audioDeviceName,
            audioDeviceGuid);
          g_audioPlayoutDevices.push_back(cricket::Device(
            std::string(audioDeviceName),
            std::string(audioDeviceGuid)));
        }
      } else {
        LOG(LS_ERROR) << "Can't enumerate audio playout devices";
      }
    });
    _audioPlayoutDeviceChanged = false;
  }
  for (auto videoDev : g_audioPlayoutDevices) {
    ret->Append(ref new MediaDevice(ToCx(videoDev.id), ToCx(videoDev.name)));
  }
  return ret;
}

void Media::SelectVideoDevice(MediaDevice^ device) {
  webrtc::CriticalSectionScoped cs(&g_videoDevicesLock);
  std::string id = FromCx(device->Id);
  _selectedVideoDevice.id = "";
  _selectedVideoDevice.name = "";
  for (auto videoDev : g_videoDevices) {
    if (videoDev.id == id) {
      _selectedVideoDevice = videoDev;
      break;
    }
  }
}

// TODO(winrt): Consider renaming this method to SelectAudioCaptureDevice.
bool Media::SelectAudioDevice(MediaDevice^ device) {
  webrtc::CriticalSectionScoped cs(&g_audioCapturerDevicesLock);
  std::string id = FromCx(device->Id);
  _selectedAudioCapturerDevice.id = "";
  _selectedAudioCapturerDevice.name =
    cricket::DeviceManagerInterface::kDefaultDeviceName;
  for (auto audioCapturer : g_audioCapturerDevices) {
    if (audioCapturer.id == id) {
      _selectedAudioCapturerDevice = audioCapturer;
      return true;
    }
  }
  return false;
}

bool Media::SelectAudioPlayoutDevice(MediaDevice^ device) {
  webrtc::CriticalSectionScoped cs(&g_audioPlayoutDevicesLock);
  std::string id = FromCx(device->Id);
  _selectedAudioPlayoutDevice.id = "";
  _selectedAudioPlayoutDevice.name =
    cricket::DeviceManagerInterface::kDefaultDeviceName;
  for (auto audioPlayoutDevice : g_audioPlayoutDevices) {
    if (audioPlayoutDevice.id == id) {
      _selectedAudioPlayoutDevice = audioPlayoutDevice;
      return true;
    }
  }
  return false;
}

void Media::OnAppSuspending() {
  // https://msdn.microsoft.com/library/windows/apps/br241124
  // Note  For Windows Phone Store apps, music and media apps should clean up
  // the MediaCapture object and associated resources in the Suspending event
  // handler and recreate them in the Resuming event handler.
  webrtc::videocapturemodule::MediaCaptureDevicesWinRT::Instance()->
    ClearCaptureDevicesCache();
}

void Media::SetDisplayOrientation(
  Windows::Graphics::Display::DisplayOrientations display_orientation) {
  webrtc::videocapturemodule::AppStateDispatcher::Instance()->
    DisplayOrientationChanged(display_orientation);
}

IAsyncOperation<IVector<CaptureCapability^>^>^
  MediaDevice::GetVideoCaptureCapabilities() {
  auto op = concurrency::create_async([this]() -> IVector<CaptureCapability^>^ {
  auto mediaCapture =
      webrtc::videocapturemodule::MediaCaptureDevicesWinRT::Instance()->
      GetMediaCapture(_id);
    if (mediaCapture == nullptr) {
      return nullptr;
    }
    auto streamProperties =
      mediaCapture->VideoDeviceController->GetAvailableMediaStreamProperties(
      MediaStreamType::VideoRecord);
    if (streamProperties == nullptr) {
      return nullptr;
    }
    auto ret = ref new Vector<CaptureCapability^>();
    std::set<std::wstring> descSet;
    for (auto prop : streamProperties) {
      if (prop->Type != L"Video") {
        continue;
      }
      auto videoProp =
        static_cast<Windows::Media::MediaProperties::
          IVideoEncodingProperties^>(prop);
      if ((videoProp->FrameRate == nullptr) ||
        (videoProp->FrameRate->Numerator == 0) ||
        (videoProp->FrameRate->Denominator == 0) ||
        (videoProp->Width == 0) || (videoProp->Height == 0)) {
        continue;
      }
      auto cap = ref new CaptureCapability(videoProp->Width, videoProp->Height,
        videoProp->FrameRate->Numerator / videoProp->FrameRate->Denominator,
        videoProp->PixelAspectRatio);
      if (descSet.find(cap->FullDescription->Data()) == descSet.end()) {
        ret->Append(cap);
        descSet.insert(cap->FullDescription->Data());
      }
    }
    return ret;
  });
  return op;
}
void Media::SubscribeToMediaDeviceChanges() {
  _videoCaptureWatcher = DeviceInformation::CreateWatcher(
    DeviceClass::VideoCapture);
  _audioCaptureWatcher = DeviceInformation::CreateWatcher(
    DeviceClass::AudioCapture);
  _audioPlayoutWatcher = DeviceInformation::CreateWatcher(
    DeviceClass::AudioRender);

  _videoCaptureWatcher->Added += ref new TypedEventHandler<DeviceWatcher^,
     DeviceInformation^>(this, &Media::OnMediaDeviceAdded);
  _videoCaptureWatcher->Removed += ref new TypedEventHandler<DeviceWatcher^,
     DeviceInformationUpdate^>(this, &Media::OnMediaDeviceRemoved);

  _audioCaptureWatcher->Added += ref new TypedEventHandler<DeviceWatcher^,
    DeviceInformation^>(this, &Media::OnMediaDeviceAdded);
  _audioCaptureWatcher->Removed += ref new TypedEventHandler<DeviceWatcher^,
    DeviceInformationUpdate^>(this, &Media::OnMediaDeviceRemoved);

  _audioPlayoutWatcher->Added += ref new TypedEventHandler<DeviceWatcher^,
    DeviceInformation^>(this, &Media::OnMediaDeviceAdded);
  _audioPlayoutWatcher->Removed += ref new TypedEventHandler<DeviceWatcher^,
    DeviceInformationUpdate^>(this, &Media::OnMediaDeviceRemoved);

  _videoCaptureWatcher->Start();
  _audioCaptureWatcher->Start();
  _audioPlayoutWatcher->Start();
}

void Media::UnsubscribeFromMediaDeviceChanges() {
  if (_videoCaptureWatcher != nullptr) {
    _videoCaptureWatcher->Stop();
  }
  if (_audioCaptureWatcher != nullptr) {
    _audioCaptureWatcher->Stop();
  }
  if (_audioPlayoutWatcher != nullptr) {
    _audioPlayoutWatcher->Stop();
  }
}

void Media::OnMediaDeviceAdded(DeviceWatcher^ sender,
                               DeviceInformation^ args) {
  // Do not send notifications while DeviceWatcher automatically
  // enumerates devices.
  if (sender->Status != DeviceWatcherStatus::EnumerationCompleted)
    return;
  if (sender == _videoCaptureWatcher) {
    LOG(LS_INFO) << "OnVideoCaptureAdded";
    _videoCaptureDeviceChanged = true;
    OnMediaDevicesChanged(MediaDeviceType::MediaDeviceType_VideoCapture);
    LOG(LS_INFO) << "OnVideoCaptureAdded END";
  } else if (sender == _audioCaptureWatcher) {
    LOG(LS_INFO) << "OnAudioCaptureAdded";
    _audioCaptureDeviceChanged = true;
    OnMediaDevicesChanged(MediaDeviceType::MediaDeviceType_AudioCapture);
    LOG(LS_INFO) << "OnAudioCaptureAdded END";
  } else if (sender == _audioPlayoutWatcher) {
    LOG(LS_INFO) << "OnAudioPlayoutAdded";
    _audioPlayoutDeviceChanged = true;
    OnMediaDevicesChanged(MediaDeviceType::MediaDeviceType_AudioPlayout);
    LOG(LS_INFO) << "OnAudioPlayoutAdded END";
  }
}

void Media::OnMediaDeviceRemoved(DeviceWatcher^ sender,
                                 DeviceInformationUpdate^ updateInfo) {
  // Do not send notifs while DeviceWatcher automaticall enumerates devices
  if (sender->Status != DeviceWatcherStatus::EnumerationCompleted)
    return;
  if (sender == _videoCaptureWatcher) {
    // Need to remove the cached MediaCapture intance if device removed,
    // otherwise, DeviceWatchers stops working properly
    // (event handlers are not called each time)
    webrtc::videocapturemodule::MediaCaptureDevicesWinRT::Instance()->
      RemoveMediaCapture(updateInfo->Id);
    _videoCaptureDeviceChanged = true;
    OnMediaDevicesChanged(MediaDeviceType::MediaDeviceType_VideoCapture);
  } else if (sender == _audioCaptureWatcher) {
    _audioCaptureDeviceChanged = true;
    OnMediaDevicesChanged(MediaDeviceType::MediaDeviceType_AudioCapture);
  } else if (sender == _audioPlayoutWatcher) {
    _audioPlayoutDeviceChanged = true;
    OnMediaDevicesChanged(MediaDeviceType::MediaDeviceType_AudioPlayout);
  }
}

}  // namespace webrtc_winrt_api
