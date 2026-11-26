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

#include "modules/audio_device/audio_engine_state.h"

namespace webrtc {
namespace audio_engine {

bool EngineState::operator==(const EngineState& rhs) const {
  return input_enabled == rhs.input_enabled && input_running == rhs.input_running &&
         output_enabled == rhs.output_enabled && output_running == rhs.output_running &&
         input_available == rhs.input_available && output_available == rhs.output_available &&
         input_enabled_persistent_mode == rhs.input_enabled_persistent_mode && input_muted == rhs.input_muted &&
         is_interrupted == rhs.is_interrupted && render_mode == rhs.render_mode && mute_mode == rhs.mute_mode &&
         platform_voice_processing_allowed == rhs.platform_voice_processing_allowed &&
         voice_processing_enabled == rhs.voice_processing_enabled &&
         voice_processing_bypassed == rhs.voice_processing_bypassed &&
         voice_processing_agc_enabled == rhs.voice_processing_agc_enabled &&
         built_in_aec_enabled == rhs.built_in_aec_enabled && built_in_ns_enabled == rhs.built_in_ns_enabled &&
         advanced_ducking == rhs.advanced_ducking && ducking_level == rhs.ducking_level &&
         output_device_id == rhs.output_device_id && input_device_id == rhs.input_device_id &&
         default_output_device_update_count == rhs.default_output_device_update_count &&
         default_input_device_update_count == rhs.default_input_device_update_count;
}

bool EngineState::IsOutputEnabled() const {
  if (!output_available) return false;

  switch (render_mode) {
    case RenderMode::Device:
      return IsInputEnabled() || output_enabled;
    case RenderMode::Manual:
      return output_enabled || input_enabled || input_enabled_persistent_mode;
  }
  return false;
}

bool EngineState::IsOutputRunning() const {
  if (!output_available) return false;

  switch (render_mode) {
    case RenderMode::Device:
      return IsInputRunning() || output_running;
    case RenderMode::Manual:
      return output_running || input_running;
  }
  return false;
}

bool EngineState::IsInputEnabled() const {
  if (!input_available) return false;

  switch (render_mode) {
    case RenderMode::Device:
      return !(mute_mode == MuteMode::RestartEngine && input_muted) &&
             (input_enabled || input_enabled_persistent_mode);
    case RenderMode::Manual:
      return input_enabled || input_enabled_persistent_mode || output_enabled;
  }
  return false;
}

bool EngineState::IsInputRunning() const {
  if (!input_available) return false;

  switch (render_mode) {
    case RenderMode::Device:
      return !(mute_mode == MuteMode::RestartEngine && input_muted) && input_running;
    case RenderMode::Manual:
      return input_running || output_running;
  }
  return false;
}

bool EngineStateUpdate::IsEngineRecreateRequired() const {
  // Device id specified
  bool device = DidUpdateOutputDevice() || DidUpdateInputDevice();

  // Default device updated
  bool default_device = (DidUpdateDefaultOutputDevice() && next.IsOutputDefaultDevice()) ||
                        (DidUpdateDefaultInputDevice() && next.IsInputDefaultDevice());

  // Special case to re-create engine when switching from Speaker & Mic ->
  // Speaker only.
  bool special_case = (prev.IsOutputEnabled() && next.IsOutputEnabled()) &&
                      (prev.IsInputEnabled() && !next.IsInputEnabled());

  // Toggling voice processing requires a full engine recreate to ensure
  // a clean audio hardware state.
  bool voice_processing = DidUpdateVoiceProcessingEnabled();

  return device || default_device || special_case || voice_processing;
}

TransitionPlan PlanDeviceTransition(const EngineStateUpdate& update) {
  const EngineState& prev = update.prev;
  const EngineState& next = update.next;

  TransitionPlan plan;
  plan.restart_engine = update.IsEngineRestartRequired();
  plan.recreate_engine = update.IsEngineRecreateRequired();

  plan.voice_processing_was_configured =
      !plan.recreate_engine && prev.IsInputEnabled() && prev.voice_processing_enabled;

  plan.stop_engine = prev.IsAnyRunning() &&
                     (!next.IsAnyRunning() || plan.restart_engine ||
                      update.DidBeginInterruption() || plan.recreate_engine);
  plan.stop_engine_if_stopped_during_interruption =
      prev.IsAnyRunning() && update.DidEndInterruption();

  plan.stop_playout_buffer = !next.IsOutputEnabled() || plan.recreate_engine;
  plan.stop_record_buffer = !next.IsInputEnabled() || plan.recreate_engine;

  plan.release_engine = plan.recreate_engine;
  plan.create_engine =
      next.IsAnyEnabled() && (!prev.IsAnyEnabled() || plan.recreate_engine);

  plan.will_enable = update.DidAnyEnable();
  plan.check_microphone_permission = update.DidEnableInput();
  plan.check_session_category = update.DidAnyEnable();

  plan.configure_voice_processing =
      next.IsInputEnabled() &&
      plan.voice_processing_was_configured != next.voice_processing_enabled;

  plan.enable_output =
      next.IsOutputEnabled() && (!prev.IsOutputEnabled() || plan.recreate_engine);
  plan.disable_output = !plan.enable_output && prev.IsOutputEnabled() &&
                        !next.IsOutputEnabled() && !plan.recreate_engine;
  plan.enable_input =
      next.IsInputEnabled() && (!prev.IsInputEnabled() || plan.recreate_engine);
  plan.disable_input = !plan.enable_input && prev.IsInputEnabled() &&
                       !next.IsInputEnabled() && !plan.recreate_engine;

  plan.did_disable = update.DidAnyDisable();

  const bool voice_processing_active =
      next.IsInputEnabled() && next.voice_processing_enabled;
  plan.configure_ducking =
      voice_processing_active &&
      (!plan.voice_processing_was_configured ||
       prev.advanced_ducking != next.advanced_ducking ||
       prev.ducking_level != next.ducking_level);
  plan.configure_voice_processing_bypass =
      voice_processing_active &&
      (!plan.voice_processing_was_configured ||
       prev.voice_processing_bypassed != next.voice_processing_bypassed);
  plan.configure_agc =
      voice_processing_active &&
      (!plan.voice_processing_was_configured ||
       prev.voice_processing_agc_enabled != next.voice_processing_agc_enabled);

  plan.configure_devices = plan.create_engine;

  plan.start_playout_buffer = next.IsOutputEnabled();
  plan.start_record_buffer = next.IsInputEnabled();

  plan.start_engine = next.IsAnyRunning() &&
                      (!prev.IsAnyRunning() || plan.restart_engine || plan.recreate_engine);
  plan.start_engine_if_not_running = next.IsAnyRunning() && update.DidEndInterruption();

  plan.final_release = prev.IsAnyEnabled() && !next.IsAnyEnabled();

  return plan;
}

EngineState RebuildBaseline(const EngineState& committed) {
  EngineState baseline = committed;
  baseline.input_enabled = false;
  baseline.input_running = false;
  baseline.output_enabled = false;
  baseline.output_running = false;
  baseline.input_enabled_persistent_mode = false;
  return baseline;
}

}  // namespace audio_engine
}  // namespace webrtc
