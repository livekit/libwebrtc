/*
 * Copyright 2024 LiveKit
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SDK_OBJC_NATIVE_SRC_AUDIO_AUDIO_DEVICE_AUDIOENGINE_H_
#define SDK_OBJC_NATIVE_SRC_AUDIO_AUDIO_DEVICE_AUDIOENGINE_H_

#include <atomic>
#include <functional>
#include <memory>

#include "api/audio_options.h"
#include "api/environment/environment.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "modules/audio_device/audio_device_generic.h"
#include "modules/audio_device/audio_engine_state.h"
#include "rtc_base/buffer.h"
#include "rtc_base/thread.h"
#include "rtc_base/thread_annotations.h"
#include "sdk/objc/base/RTCMacros.h"
#include "sdk/objc/native/src/audio/audio_session_observer.h"

#if TARGET_OS_OSX
#import <CoreAudio/CoreAudio.h>
#endif
#import <AVFAudio/AVFAudio.h>
#import <AudioToolbox/AudioToolbox.h>

RTC_FWD_DECL_OBJC_CLASS(RTC_OBJC_TYPE(RTCNativeAudioSessionDelegateAdapter));

namespace webrtc {

// Error codes for AudioEngineDevice.
// Apple recommends that you use values in the range -1000 through -9999 inclusive. Values outside
// of this range are reserved by Apple for internal use.
enum AudioEngineErrorCode {
  // Success (no error)
  kAudioEngineNoError = 0,

  // General errors
  kAudioEngineUnknownError = -1000,
  kAudioEngineInitError = -1001,
  kAudioEngineTerminateError = -1002,
  kAudioEngineNotInitializedError = -1003,
  kAudioEngineAlreadyInitializedError = -1004,

  // Device errors
  kAudioEngineDeviceNotFoundError = -2000,
  kAudioEngineDeviceUnavailableError = -2001,
  kAudioEngineDeviceDisconnectedError = -2002,
  kAudioEngineDeviceUnauthorizedError = -2003,
  kAudioEngineDeviceInUseError = -2004,
  kAudioEngineDeviceFormatError = -2005,

  // Playback errors
  kAudioEnginePlayoutInitError = -3000,
  kAudioEnginePlayoutStartError = -3001,
  kAudioEnginePlayoutStopError = -3002,
  kAudioEnginePlayoutAlreadyInitializedError = -3003,
  kAudioEnginePlayoutNotInitializedError = -3004,
  kAudioEnginePlayoutDeviceNotAvailableError = -3010,

  // Recording errors
  kAudioEngineRecordingInitError = -4000,
  kAudioEngineRecordingStartError = -4001,
  kAudioEngineRecordingStopError = -4002,
  kAudioEngineRecordingAlreadyInitializedError = -4003,
  kAudioEngineRecordingNotInitializedError = -4004,
  kAudioEngineRecordingPermissionDeniedError = -4005,
  kAudioEngineRecordingDeviceNotAvailableError = -4010,

  // Engine state errors
  kAudioEngineInvalidStateError = -5000,
  kAudioEngineStateTransitionError = -5001,
  kAudioEngineInterruptionError = -5002,

  // Resource errors
  kAudioEngineOutOfMemoryError = -6000,
  kAudioEngineResourceLimitError = -6001,

  // Render mode errors
  kAudioEngineRenderModeError = -7000,
  kAudioEngineManualRenderingError = -7001,

  // Voice processing errors
  kAudioEngineVoiceProcessingError = -8000,
  kAudioEngineAGCError = -8001,

  // Permission and session errors
  kAudioEngineErrorInsufficientDevicePermission = -9000,
  kAudioEngineErrorAudioSessionInvalidCategory = -9001
};

class FineAudioBuffer;

extern NSString* const kAudioEngineInputMixerNodeKey;

class AudioEngineDevice : public AudioDeviceModule, public AudioSessionObserver {
 public:
  using RenderMode = audio_engine::RenderMode;
  using MuteMode = audio_engine::MuteMode;
  using AudioDuckingLevel = audio_engine::AudioDuckingLevel;
  // Callers name these enumerators through the class.
  static constexpr AudioDuckingLevel AudioDuckingLevelDefault = audio_engine::AudioDuckingLevelDefault;
  static constexpr AudioDuckingLevel AudioDuckingLevelMin = audio_engine::AudioDuckingLevelMin;
  static constexpr AudioDuckingLevel AudioDuckingLevelMid = audio_engine::AudioDuckingLevelMid;
  static constexpr AudioDuckingLevel AudioDuckingLevelMax = audio_engine::AudioDuckingLevelMax;

  // The state of the engine, see audio_engine_state.h.
  using EngineState = audio_engine::EngineState;

  explicit AudioEngineDevice(const Environment& env, bool voice_processing_bypassed);
  ~AudioEngineDevice() override;

  int32_t Init() override;
  int32_t Terminate() override;
  bool Initialized() const override;

  int32_t InitPlayout() override;
  bool PlayoutIsInitialized() const override;

  int32_t InitRecording() override;
  bool RecordingIsInitialized() const override;

  int32_t StartPlayout() override;
  int32_t StopPlayout() override;
  bool Playing() const override;

  int32_t StartRecording() override;
  int32_t StopRecording() override;
  bool Recording() const override;

  int32_t PlayoutDelay(uint16_t* delayMS) const override;
  int32_t GetPlayoutUnderrunCount() const override { return -1; }

#if defined(WEBRTC_IOS)
  int GetPlayoutAudioParameters(AudioParameters* params) const override;
  int GetRecordAudioParameters(AudioParameters* params) const override;
#endif

  int32_t ActiveAudioLayer(AudioDeviceModule::AudioLayer* audioLayer) const override;
  int32_t PlayoutIsAvailable(bool* available) override;
  int32_t RecordingIsAvailable(bool* available) override;
  int16_t PlayoutDevices() override;
  int16_t RecordingDevices() override;
  int32_t PlayoutDeviceName(uint16_t index, char name[kAdmMaxDeviceNameSize],
                            char guid[kAdmMaxGuidSize]) override;
  int32_t RecordingDeviceName(uint16_t index, char name[kAdmMaxDeviceNameSize],
                              char guid[kAdmMaxGuidSize]) override;
  int32_t SetPlayoutDevice(uint16_t index) override;
  int32_t SetPlayoutDevice(AudioDeviceModule::WindowsDeviceType device) override;
  int32_t SetRecordingDevice(uint16_t index) override;
  int32_t SetRecordingDevice(AudioDeviceModule::WindowsDeviceType device) override;
  int32_t InitSpeaker() override;
  bool SpeakerIsInitialized() const override;
  int32_t InitMicrophone() override;
  bool MicrophoneIsInitialized() const override;
  int32_t SpeakerVolumeIsAvailable(bool* available) override;
  int32_t SetSpeakerVolume(uint32_t volume) override;
  int32_t SpeakerVolume(uint32_t* volume) const override;
  int32_t MaxSpeakerVolume(uint32_t* maxVolume) const override;
  int32_t MinSpeakerVolume(uint32_t* minVolume) const override;
  int32_t MicrophoneVolumeIsAvailable(bool* available) override;
  int32_t SetMicrophoneVolume(uint32_t volume) override;
  int32_t MicrophoneVolume(uint32_t* volume) const override;
  int32_t MaxMicrophoneVolume(uint32_t* maxVolume) const override;
  int32_t MinMicrophoneVolume(uint32_t* minVolume) const override;
  int32_t MicrophoneMuteIsAvailable(bool* available) override;
  int32_t SetMicrophoneMute(bool enable) override;
  int32_t MicrophoneMute(bool* enabled) const override;
  int32_t SpeakerMuteIsAvailable(bool* available) override;
  int32_t SetSpeakerMute(bool enable) override;
  int32_t SpeakerMute(bool* enabled) const override;
  int32_t StereoPlayoutIsAvailable(bool* available) const override;
  int32_t SetStereoPlayout(bool enable) override;
  int32_t StereoPlayout(bool* enabled) const override;
  int32_t StereoRecordingIsAvailable(bool* available) const override;
  int32_t SetStereoRecording(bool enable) override;
  int32_t StereoRecording(bool* enabled) const override;

  int32_t RegisterAudioCallback(AudioTransport* audioCallback) override;

  // Built-in AEC/AGC/NS via VPIO. Available on device, not on simulator.
  bool BuiltInAECIsAvailable() const override;
  bool BuiltInAGCIsAvailable() const override;
  bool BuiltInNSIsAvailable() const override;
  PlatformAudioProcessingTopology GetPlatformAudioProcessingTopology() const override;
  bool PlatformVoiceProcessingPathIsAvailable() const override;
  int32_t EnablePlatformVoiceProcessingPath(bool enable) override;
  PlatformAudioProcessingState GetPlatformAudioProcessingState() const override;
  int32_t EnableBuiltInAEC(bool enable) override;
  int32_t EnableBuiltInAGC(bool enable) override;
  int32_t EnableBuiltInNS(bool enable) override;

  // AudioSessionObserver methods. May be called from any thread.
  void OnInterruptionBegin(uint32_t reason) override;
  void OnInterruptionEnd(bool should_resume) override;
  void OnValidRouteChange() override;
  void OnCanPlayOrRecordChange(bool can_play_or_record) override;
  void OnChangedOutputVolume() override;
  void OnMediaServicesReset() override;

  bool IsInterrupted();

  bool IsEngineRunning();

  int32_t SetEngineState(EngineState enable);
  int32_t GetEngineState(EngineState* enabled);

  int32_t SetEngineAvailability(bool input_available, bool output_available);
  int32_t EngineAvailability(bool* input_available, bool* output_available);

  int32_t SetObserver(AudioDeviceObserver* observer) override;

  int32_t SetManualRenderingMode(bool enable);
  int32_t ManualRenderingMode(bool* enabled);

  int32_t SetMuteMode(MuteMode mode);
  int32_t GetMuteMode(MuteMode* mode);

  int32_t SetAdvancedDucking(bool enable);
  int32_t AdvancedDucking(bool* enabled);

  int32_t SetDuckingLevel(AudioDuckingLevel level);
  int32_t DuckingLevel(AudioDuckingLevel* level);

  int32_t SetInitRecordingPersistentMode(bool enable, const AudioOptions *options = nullptr);
  int32_t InitRecordingPersistentMode(bool* enabled);

  int32_t SetPlatformVoiceProcessingAllowed(bool allowed);
  int32_t PlatformVoiceProcessingAllowed(bool *allowed);

  // Deprecated: use SetPlatformVoiceProcessingAllowed / PlatformVoiceProcessingAllowed.
  // Retained as compatibility aliases that forward to those and log a warning.
  int32_t SetVoiceProcessingEnabled(bool enable);
  int32_t VoiceProcessingEnabled(bool* enabled);

  int32_t SetVoiceProcessingBypassed(bool enable);
  int32_t VoiceProcessingBypassed(bool* enabled);

  int32_t SetVoiceProcessingAGCEnabled(bool enable);
  int32_t VoiceProcessingAGCEnabled(bool* enabled);

  int32_t InitAndStartRecording(const AudioOptions *options = nullptr);

  bool IsStopOnMuteModeEnabled() const override;

  // Test only. Steps of a device transition where a test can inject a failure
  // that an observer hook cannot cause.
  enum class FaultPoint {
    // The engine start fails, as if -[AVAudioEngine startAndReturnError:]
    // failed on every attempt.
    kStartEngine,
  };
  // Test only. `injector` runs on the ADM thread at each fault point. A
  // non-zero result fails that step with the returned error. Pass nullptr to
  // remove it.
  void SetFaultInjectorForTesting(std::function<int32_t(FaultPoint)> injector);

 private:
  using EngineStateUpdate = audio_engine::EngineStateUpdate;
  using TransitionPlan = audio_engine::TransitionPlan;

  EngineState engine_state_ RTC_GUARDED_BY(thread_);

  int32_t ModifyEngineState(std::function<EngineState(EngineState)> state_transform);

  int32_t ApplyDeviceEngineState(EngineStateUpdate state);
  int32_t ApplyManualEngineState(EngineStateUpdate state);

  // Recreates `fine_audio_buffer_` if null: the enable steps that create it and
  // the buffer-start steps that use it have different conditions.
  void EnsureFineAudioBuffer();

  // AudioEngine observer methods. May be called from any thread.
  void ReconfigureEngine();

  // Device engine I/O node accessors. Must be called on `thread_`. The ones
  // taking the in-flight transition assert that the matching side is, or is
  // being, enabled. -[AVAudioEngine inputNode] instantiates the input audio
  // unit on first access, which on iOS triggers the microphone permission
  // prompt, so these are the only two ways to reach it: InputNode()
  // instantiates it (input is being enabled), InputNodeOrNil() never does and
  // returns nil unless this engine instance already instantiated it.
  AVAudioInputNode* InputNode(const EngineStateUpdate& state);
  AVAudioInputNode* InputNodeOrNil() const;
  AVAudioOutputNode* OutputNode(const EngineStateUpdate& state);

  // Stops the device engine's I/O audio units explicitly before the engine is
  // released. Required for VPIO, which creates an aggregate device and IO thread
  // that -[AVAudioEngine stop] alone may not fully tear down, and harmless for
  // standard I/O nodes. Must be called on `thread_` with engine_device_ set.
  void StopDeviceEngineAudioUnits();

// Device related
#if TARGET_OS_OSX
  static OSStatus objectListenerProc(AudioObjectID objectId, UInt32 numberAddresses,
                                     const AudioObjectPropertyAddress addresses[],
                                     void* clientData);
  void HandleDeviceListenerEvent(AudioObjectPropertySelector selector);
  void UpdateAllDeviceIDs();

  // Debounce flags for device updates
  webrtc::scoped_refptr<PendingTaskSafetyFlag> default_device_update_safety_ =
      PendingTaskSafetyFlag::Create();
  const int kDefaultDeviceUpdateDebounceMs = 500;  // Debounce delay in milliseconds

  std::vector<AudioObjectID> input_device_ids_;
  std::vector<AudioObjectID> output_device_ids_;
  std::vector<std::string> output_device_labels_;
  std::vector<std::string> input_device_labels_;
#endif

  bool IsMicrophonePermissionAuthorized();

#if !TARGET_OS_OSX
  bool IsAudioSessionCategoryValid(NSString* category, bool is_input_enabled,
                                   bool is_output_enabled);
#endif
  void DebugAudioEngine();

  void StartRenderLoop();
  AVAudioEngineManualRenderingBlock render_block_;

  // Thread that this object is created on.
  webrtc::Thread* thread_;
  std::unique_ptr<webrtc::Thread> render_thread_;
  AVAudioPCMBuffer* render_buffer_;
  AVAudioPCMBuffer* read_buffer_;

  const std::unique_ptr<TaskQueueFactory> task_queue_factory_;
  std::unique_ptr<AudioDeviceBuffer> audio_device_buffer_;
  std::unique_ptr<FineAudioBuffer> fine_audio_buffer_;

  AudioParameters playout_parameters_;
  AudioParameters record_parameters_;

  // Set to true after successful call to Init(), false otherwise.
  bool initialized_ RTC_GUARDED_BY(thread_);

  AudioDeviceObserver* observer_ RTC_GUARDED_BY(thread_) = nullptr;

#if defined(WEBRTC_IOS)
  // Audio interruption observer instance.
  RTC_OBJC_TYPE(RTCNativeAudioSessionDelegateAdapter) * audio_session_observer_
      RTC_GUARDED_BY(thread_);
#endif

  // Avoids running pending task after `this` is Terminated.
  webrtc::scoped_refptr<PendingTaskSafetyFlag> safety_ = PendingTaskSafetyFlag::Create();

  // Ratio between mach tick units and nanosecond. Used to change mach tick
  // units to nanoseconds.
  double machTickUnitsToNanoseconds_;

  // AVAudioEngine objects
  AVAudioEngine* engine_device_ RTC_GUARDED_BY(thread_);
  // True once the current `engine_device_` instance has instantiated its
  // inputNode. Reset whenever a new engine is allocated, meaningless while
  // engine_device_ is nil. Never read engine_device_.inputNode directly, go
  // through InputNode() or InputNodeOrNil() so this flag stays accurate.
  bool input_node_instantiated_ RTC_GUARDED_BY(thread_) = false;
  AVAudioEngine* engine_manual_input_ RTC_GUARDED_BY(thread_);

  // Used for manual rendering mode
  AVAudioFormat* manual_render_rtc_format_;  // Int16

  // Output related
  AVAudioSourceNode* source_node_ RTC_GUARDED_BY(thread_);

  // Input related nodes
  AVAudioSinkNode* sink_node_ RTC_GUARDED_BY(thread_);
  AVAudioMixerNode* input_mixer_node_ RTC_GUARDED_BY(thread_);

  // Float32 -> Int16 converter.
  AudioConverterRef converter_ref_ = nullptr;
  AVAudioPCMBuffer* converter_buffer_;

  void* configuration_observer_ RTC_GUARDED_BY(thread_) = nullptr;

  std::function<int32_t(FaultPoint)> fault_injector_ RTC_GUARDED_BY(thread_);
};
}  // namespace webrtc

#endif  // SDK_OBJC_NATIVE_SRC_AUDIO_AUDIO_DEVICE_AUDIOENGINE_H_
