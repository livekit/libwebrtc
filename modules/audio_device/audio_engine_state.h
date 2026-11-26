/*
 * Copyright 2026 LiveKit
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

#ifndef MODULES_AUDIO_DEVICE_AUDIO_ENGINE_STATE_H_
#define MODULES_AUDIO_DEVICE_AUDIO_ENGINE_STATE_H_

#include <cstdint>

// The state of the AVAudioEngine based audio device module (AudioEngineDevice)
// and the plan of steps a state transition takes. Plain C++ with no Apple
// imports, so the transition rules can be unit tested on any host without
// audio hardware.

namespace webrtc {
namespace audio_engine {

enum RenderMode { Device = 0, Manual = 1 };

enum MuteMode {
  // Mute input using voice processing
  VoiceProcessing = 0,
  // Mute by restarting engine
  RestartEngine = 1,
  // Mute input by muting the input mixer node
  InputMixer = 2,
};

// Ducking level for voice processing.
// Maps to AVAudioVoiceProcessingOtherAudioDuckingLevel (iOS 17.0+, macOS 14.0+).
enum AudioDuckingLevel {
  AudioDuckingLevelDefault = 0,
  AudioDuckingLevelMin = 1,
  AudioDuckingLevelMid = 2,
  AudioDuckingLevelMax = 3,
};

// Represents the state of the audio engine, including input/output status,
// rendering mode, and various configuration flags.
struct EngineState {
  bool input_enabled = false;
  bool input_running = false;
  bool output_enabled = false;
  bool output_running = false;

  bool output_available = true;
  bool input_available = true;

  bool input_enabled_persistent_mode = false;

  bool input_muted = true;
  bool is_interrupted = false;

  RenderMode render_mode = RenderMode::Device;
  MuteMode mute_mode = MuteMode::VoiceProcessing;

  // App-level policy for Apple's platform voice processing. When false, the
  // runtime audio-processing resolver treats Apple VPIO as unavailable, so
  // automatic mode falls back to WebRTC software processing and platform mode
  // is rejected.
  bool platform_voice_processing_allowed = true;
  bool voice_processing_enabled = true;
  bool voice_processing_bypassed = false;
  bool voice_processing_agc_enabled = true;
  // Requested built-in AEC/NS state. AVAudioEngine exposes one VPIO bypass knob
  // for AEC and NS. AGC has a separate switch, but only has an effect while
  // VPIO is active.
  bool built_in_aec_enabled = true;
  bool built_in_ns_enabled = true;
  bool advanced_ducking = false;
  AudioDuckingLevel ducking_level = AudioDuckingLevelMin;

  // 0 is kAudioObjectUnknown on macOS, the default device.
  uint32_t output_device_id = 0;
  uint32_t input_device_id = 0;

  uint32_t default_output_device_update_count = 0;  // Track default switch count
  uint32_t default_input_device_update_count = 0;

  bool operator==(const EngineState& rhs) const;
  bool operator!=(const EngineState& rhs) const { return !(*this == rhs); }

  // AUDIO STATE LOGIC
  //
  // Device Mode:
  // - Output follows input to keep AVAudioEngine IO active for capture.
  // - Input respects mute mode restrictions (RestartEngine + input_muted)
  //
  // Manual Mode:
  // - Bidirectional coupling: if ANY component is enabled/running, BOTH are considered
  // enabled/running
  // - No mute mode restrictions (manual control bypasses automatic muting)
  // - Required for AVAudioEngine graph connectivity (input must connect to output)
  //
  // All modes respect availability flags (input_available/output_available)

  bool IsOutputEnabled() const;
  bool IsOutputRunning() const;
  bool IsInputEnabled() const;
  bool IsInputRunning() const;

  bool IsAnyEnabled() const { return IsInputEnabled() || IsOutputEnabled(); }
  bool IsAnyRunning() const { return IsInputRunning() || IsOutputRunning(); }

  bool IsOutputDefaultDevice() const { return output_device_id == 0; }
  bool IsInputDefaultDevice() const { return input_device_id == 0; }
};

// A transition from the committed state `prev` to the requested state `next`.
struct EngineStateUpdate {
  EngineState prev;
  EngineState next;

  bool HasNoChanges() const { return prev == next; }

  bool DidEnableOutput() const { return !prev.IsOutputEnabled() && next.IsOutputEnabled(); }

  bool DidEnableInput() const { return !prev.IsInputEnabled() && next.IsInputEnabled(); }

  bool DidDisableOutput() const { return prev.IsOutputEnabled() && !next.IsOutputEnabled(); }

  bool DidDisableInput() const { return prev.IsInputEnabled() && !next.IsInputEnabled(); }

  bool DidAnyEnable() const { return DidEnableOutput() || DidEnableInput(); }

  bool DidAnyDisable() const { return DidDisableOutput() || DidDisableInput(); }

  bool DidBeginInterruption() const { return !prev.is_interrupted && next.is_interrupted; }

  bool DidEndInterruption() const { return prev.is_interrupted && !next.is_interrupted; }

  bool DidUpdateAudioGraph() const {
    return (prev.IsInputEnabled() != next.IsInputEnabled()) ||
           (prev.IsOutputEnabled() != next.IsOutputEnabled());
  }

  bool DidUpdateVoiceProcessingEnabled() const {
    return prev.voice_processing_enabled != next.voice_processing_enabled;
  }

  bool DidUpdateOutputDevice() const { return prev.output_device_id != next.output_device_id; }

  bool DidUpdateInputDevice() const { return prev.input_device_id != next.input_device_id; }

  bool DidUpdateDefaultOutputDevice() const {
    return prev.default_output_device_update_count != next.default_output_device_update_count;
  }

  bool DidUpdateDefaultInputDevice() const {
    return prev.default_input_device_update_count != next.default_input_device_update_count;
  }

  bool DidUpdateMuteMode() const { return prev.mute_mode != next.mute_mode; }

  bool IsEngineRestartRequired() const { return DidUpdateAudioGraph(); }

  bool IsEngineRecreateRequired() const;

  bool DidEnableManualRenderingMode() const {
    return prev.render_mode != RenderMode::Manual && next.render_mode == RenderMode::Manual;
  }

  bool DidEnableDeviceRenderingMode() const {
    return prev.render_mode != RenderMode::Device && next.render_mode == RenderMode::Device;
  }
};

// The steps a device rendering mode transition performs, in the order
// AudioEngineDevice::ApplyDeviceEngineState runs them. Each flag says whether
// the step's state condition holds. A few steps also depend on runtime facts
// that the state cannot know (whether the engine is still running, whether a
// buffer already plays). The executor checks those, as noted per flag.
struct TransitionPlan {
  // Summary of the transition kind.
  bool restart_engine = false;   // The audio graph changes.
  bool recreate_engine = false;  // The engine is released and a new one created.

  // Whether voice processing was already configured on the current engine's
  // input node before this transition. False after a recreate (a fresh engine
  // starts with voice processing off) or when input was not enabled.
  bool voice_processing_was_configured = false;

  // Stop the running engine and fire OnEngineDidStop.
  bool stop_engine = false;
  // Interruption end while the committed state is running. The executor stops
  // the engine only if it stopped by itself during the interruption, that is
  // a configuration observer is still registered and the engine is not
  // running.
  bool stop_engine_if_stopped_during_interruption = false;

  // The executor stops a buffer only if it is playing or recording.
  bool stop_playout_buffer = false;
  bool stop_record_buffer = false;

  // Release the current engine before creating a new one.
  bool release_engine = false;
  bool create_engine = false;

  // Fire OnEngineWillEnable, then check the session for the enable.
  bool will_enable = false;
  bool check_microphone_permission = false;
  bool check_session_category = false;

  bool configure_voice_processing = false;

  bool enable_output = false;
  bool disable_output = false;
  bool enable_input = false;
  bool disable_input = false;

  bool did_disable = false;

  // Voice processing properties applied after the graph is built.
  bool configure_ducking = false;
  bool configure_voice_processing_bypass = false;
  bool configure_agc = false;

  // Select the requested devices on a new engine (macOS).
  bool configure_devices = false;

  // The executor starts a buffer only if it is not playing or recording yet.
  bool start_playout_buffer = false;
  bool start_record_buffer = false;

  // Fire OnEngineWillStart and start the engine.
  bool start_engine = false;
  // Interruption end with a running next state. The executor starts the
  // engine only if it is not running, since an engine started during the
  // interruption may still run at its end.
  bool start_engine_if_not_running = false;

  // Release the engine because nothing stays enabled.
  bool final_release = false;
};

// Plans a transition in device rendering mode. Pure, the result depends only
// on `update`.
TransitionPlan PlanDeviceTransition(const EngineStateUpdate& update);

}  // namespace audio_engine
}  // namespace webrtc

#endif  // MODULES_AUDIO_DEVICE_AUDIO_ENGINE_STATE_H_
