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

#include <functional>
#include <string>
#include <vector>

#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {
namespace audio_engine {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

// The steps a plan takes, in the order ApplyDeviceEngineState runs them.
std::vector<std::string> Steps(const TransitionPlan& plan) {
  std::vector<std::string> steps;
  auto add = [&](bool on, const char* name) {
    if (on) steps.push_back(name);
  };
  add(plan.stop_engine, "stop");
  add(plan.stop_engine_if_stopped_during_interruption, "stop_if_stopped");
  add(plan.stop_playout_buffer, "stop_playout_buffer");
  add(plan.stop_record_buffer, "stop_record_buffer");
  add(plan.release_engine, "release");
  add(plan.create_engine, "create");
  add(plan.will_enable, "will_enable");
  add(plan.check_microphone_permission, "check_permission");
  add(plan.check_session_category, "check_category");
  add(plan.configure_voice_processing, "configure_vp");
  add(plan.enable_output, "enable_output");
  add(plan.disable_output, "disable_output");
  add(plan.enable_input, "enable_input");
  add(plan.disable_input, "disable_input");
  add(plan.did_disable, "did_disable");
  add(plan.configure_ducking, "ducking");
  add(plan.configure_voice_processing_bypass, "vp_bypass");
  add(plan.configure_agc, "agc");
  add(plan.configure_devices, "devices");
  add(plan.start_playout_buffer, "start_playout_buffer");
  add(plan.start_record_buffer, "start_record_buffer");
  add(plan.start_engine, "start");
  add(plan.start_engine_if_not_running, "start_if_not_running");
  add(plan.final_release, "final_release");
  return steps;
}

std::vector<std::string> Plan(const EngineState& prev, const EngineState& next) {
  return Steps(PlanDeviceTransition({prev, next}));
}

EngineState Idle() {
  return EngineState();
}

EngineState PlayoutEnabled() {
  EngineState s;
  s.output_enabled = true;
  return s;
}

EngineState Playing() {
  EngineState s = PlayoutEnabled();
  s.output_running = true;
  return s;
}

// What InitAndStartRecording produces from idle.
EngineState Recording() {
  EngineState s;
  s.input_enabled = true;
  s.input_running = true;
  s.input_muted = false;
  return s;
}

EngineState PlayingAndRecording() {
  EngineState s = Recording();
  s.output_enabled = true;
  s.output_running = true;
  return s;
}

EngineState Interrupted(EngineState s) {
  s.is_interrupted = true;
  return s;
}

// MARK: - Predicates

TEST(AudioEngineStateTest, DefaultStateIsIdle) {
  EngineState s;
  EXPECT_FALSE(s.IsInputEnabled());
  EXPECT_FALSE(s.IsOutputEnabled());
  EXPECT_FALSE(s.IsAnyEnabled());
  EXPECT_FALSE(s.IsAnyRunning());
  EXPECT_TRUE(s.IsInputDefaultDevice());
  EXPECT_TRUE(s.IsOutputDefaultDevice());
}

TEST(AudioEngineStateTest, DeviceModeOutputFollowsInput) {
  EngineState s = Recording();
  EXPECT_TRUE(s.IsInputEnabled());
  EXPECT_TRUE(s.IsInputRunning());
  EXPECT_TRUE(s.IsOutputEnabled());
  EXPECT_TRUE(s.IsOutputRunning());

  // Output alone does not enable input.
  s = Playing();
  EXPECT_TRUE(s.IsOutputEnabled());
  EXPECT_TRUE(s.IsOutputRunning());
  EXPECT_FALSE(s.IsInputEnabled());
  EXPECT_FALSE(s.IsInputRunning());
}

struct PredicateCase {
  const char* name;
  std::function<void(EngineState&)> edit;
  bool input_enabled;
  bool input_running;
  bool output_enabled;
  bool output_running;
};

TEST(AudioEngineStateTest, PredicateTable) {
  const std::vector<PredicateCase> cases = {
      {"recording", [](EngineState&) {}, true, true, true, true},
      {"restart mode muted", [](EngineState& s) {
         s.mute_mode = MuteMode::RestartEngine;
         s.input_muted = true;
       }, false, false, false, false},
      {"restart mode unmuted", [](EngineState& s) {
         s.mute_mode = MuteMode::RestartEngine;
         s.input_muted = false;
       }, true, true, true, true},
      {"vp mode muted keeps input", [](EngineState& s) {
         s.mute_mode = MuteMode::VoiceProcessing;
         s.input_muted = true;
       }, true, true, true, true},
      {"mixer mode muted keeps input", [](EngineState& s) {
         s.mute_mode = MuteMode::InputMixer;
         s.input_muted = true;
       }, true, true, true, true},
      {"input unavailable", [](EngineState& s) { s.input_available = false; }, false, false, false,
       false},
      {"output unavailable", [](EngineState& s) { s.output_available = false; }, true, true, false,
       false},
      {"persistent mode, not recording", [](EngineState& s) {
         s.input_enabled = false;
         s.input_running = false;
         s.input_enabled_persistent_mode = true;
       }, true, false, true, false},
      {"restart mode muted overrides persistent mode", [](EngineState& s) {
         s.input_enabled = false;
         s.input_running = false;
         s.input_enabled_persistent_mode = true;
         s.mute_mode = MuteMode::RestartEngine;
         s.input_muted = true;
       }, false, false, false, false},
      {"manual mode output enables input", [](EngineState& s) {
         s.render_mode = RenderMode::Manual;
         s.input_enabled = false;
         s.input_running = false;
         s.output_enabled = true;
         s.output_running = true;
       }, true, true, true, true},
      {"manual mode ignores restart mute", [](EngineState& s) {
         s.render_mode = RenderMode::Manual;
         s.mute_mode = MuteMode::RestartEngine;
         s.input_muted = true;
       }, true, true, true, true},
  };
  for (const auto& c : cases) {
    SCOPED_TRACE(c.name);
    EngineState s = Recording();
    c.edit(s);
    EXPECT_EQ(s.IsInputEnabled(), c.input_enabled);
    EXPECT_EQ(s.IsInputRunning(), c.input_running);
    EXPECT_EQ(s.IsOutputEnabled(), c.output_enabled);
    EXPECT_EQ(s.IsOutputRunning(), c.output_running);
  }
}

TEST(AudioEngineStateTest, EqualityCoversEveryField) {
  const std::vector<std::function<void(EngineState&)>> edits = {
      [](EngineState& s) { s.input_enabled = !s.input_enabled; },
      [](EngineState& s) { s.input_running = !s.input_running; },
      [](EngineState& s) { s.output_enabled = !s.output_enabled; },
      [](EngineState& s) { s.output_running = !s.output_running; },
      [](EngineState& s) { s.output_available = !s.output_available; },
      [](EngineState& s) { s.input_available = !s.input_available; },
      [](EngineState& s) { s.input_enabled_persistent_mode = !s.input_enabled_persistent_mode; },
      [](EngineState& s) { s.input_muted = !s.input_muted; },
      [](EngineState& s) { s.is_interrupted = !s.is_interrupted; },
      [](EngineState& s) { s.render_mode = RenderMode::Manual; },
      [](EngineState& s) { s.mute_mode = MuteMode::InputMixer; },
      [](EngineState& s) { s.platform_voice_processing_allowed = !s.platform_voice_processing_allowed; },
      [](EngineState& s) { s.voice_processing_enabled = !s.voice_processing_enabled; },
      [](EngineState& s) { s.voice_processing_bypassed = !s.voice_processing_bypassed; },
      [](EngineState& s) { s.voice_processing_agc_enabled = !s.voice_processing_agc_enabled; },
      [](EngineState& s) { s.built_in_aec_enabled = !s.built_in_aec_enabled; },
      [](EngineState& s) { s.built_in_ns_enabled = !s.built_in_ns_enabled; },
      [](EngineState& s) { s.advanced_ducking = !s.advanced_ducking; },
      [](EngineState& s) { s.ducking_level = AudioDuckingLevelMax; },
      [](EngineState& s) { s.output_device_id = 42; },
      [](EngineState& s) { s.input_device_id = 42; },
      [](EngineState& s) { s.default_output_device_update_count++; },
      [](EngineState& s) { s.default_input_device_update_count++; },
  };
  for (size_t i = 0; i < edits.size(); ++i) {
    SCOPED_TRACE(i);
    EngineState a;
    EngineState b;
    edits[i](b);
    EXPECT_NE(a, b);
    EXPECT_FALSE((EngineStateUpdate{a, b}.HasNoChanges()));
  }
  EXPECT_TRUE((EngineStateUpdate{EngineState(), EngineState()}.HasNoChanges()));
}

TEST(AudioEngineStateTest, InterruptionPredicates) {
  EngineStateUpdate begin{Playing(), Interrupted(Playing())};
  EXPECT_TRUE(begin.DidBeginInterruption());
  EXPECT_FALSE(begin.DidEndInterruption());
  EXPECT_FALSE(begin.DidUpdateAudioGraph());

  EngineStateUpdate end{Interrupted(Playing()), Playing()};
  EXPECT_FALSE(end.DidBeginInterruption());
  EXPECT_TRUE(end.DidEndInterruption());
}

TEST(AudioEngineStateTest, RecreateRequired) {
  struct Case {
    const char* name;
    EngineState prev;
    std::function<void(EngineState&)> edit;
    bool recreate;
  };
  const std::vector<Case> cases = {
      {"no change", PlayingAndRecording(), [](EngineState&) {}, false},
      {"output device id", Playing(), [](EngineState& s) { s.output_device_id = 7; }, true},
      {"input device id", Recording(), [](EngineState& s) { s.input_device_id = 7; }, true},
      {"default output changed, default selected", Playing(),
       [](EngineState& s) { s.default_output_device_update_count++; }, true},
      {"default output changed, explicit device selected",
       [] {
         EngineState s = Playing();
         s.output_device_id = 7;
         return s;
       }(),
       [](EngineState& s) { s.default_output_device_update_count++; }, false},
      {"default input changed, default selected", Recording(),
       [](EngineState& s) { s.default_input_device_update_count++; }, true},
      {"default input changed, explicit device selected",
       [] {
         EngineState s = Recording();
         s.input_device_id = 7;
         return s;
       }(),
       [](EngineState& s) { s.default_input_device_update_count++; }, false},
      {"speaker and mic to speaker only", PlayingAndRecording(),
       [](EngineState& s) {
         s.input_enabled = false;
         s.input_running = false;
       },
       true},
      {"mic only to nothing", Recording(),
       [](EngineState& s) {
         s.input_enabled = false;
         s.input_running = false;
       },
       false},
      {"voice processing toggled", Playing(),
       [](EngineState& s) { s.voice_processing_enabled = false; }, true},
      {"mute mode changed", PlayingAndRecording(),
       [](EngineState& s) { s.mute_mode = MuteMode::InputMixer; }, false},
  };
  for (const auto& c : cases) {
    SCOPED_TRACE(c.name);
    EngineState next = c.prev;
    c.edit(next);
    EXPECT_EQ((EngineStateUpdate{c.prev, next}.IsEngineRecreateRequired()), c.recreate);
  }
}

// MARK: - Transitions

TEST(AudioEngineStateTest, NoChangePlansOnlyBufferChecks) {
  // ModifyEngineState returns before planning when nothing changed. The
  // buffer start flags only mean "make sure it plays", so they stay on.
  EXPECT_THAT(Plan(Playing(), Playing()),
              ElementsAre("stop_record_buffer", "start_playout_buffer"));
  EXPECT_THAT(Plan(Idle(), Idle()), ElementsAre("stop_playout_buffer", "stop_record_buffer"));
}

TEST(AudioEngineStateTest, InitPlayoutFromIdle) {
  EXPECT_THAT(Plan(Idle(), PlayoutEnabled()),
              ElementsAre("stop_record_buffer", "create", "will_enable", "check_category",
                          "enable_output", "devices", "start_playout_buffer"));
}

TEST(AudioEngineStateTest, StartPlayoutAfterInit) {
  EXPECT_THAT(Plan(PlayoutEnabled(), Playing()),
              ElementsAre("stop_record_buffer", "start_playout_buffer", "start"));
}

TEST(AudioEngineStateTest, StopPlayout) {
  EXPECT_THAT(Plan(Playing(), Idle()),
              ElementsAre("stop", "stop_playout_buffer", "stop_record_buffer", "disable_output",
                          "did_disable", "final_release"));
}

TEST(AudioEngineStateTest, InitAndStartRecordingFromIdle) {
  EXPECT_THAT(Plan(Idle(), Recording()),
              ElementsAre("create", "will_enable", "check_permission", "check_category",
                          "configure_vp", "enable_output", "enable_input", "ducking", "vp_bypass",
                          "agc", "devices", "start_playout_buffer", "start_record_buffer",
                          "start"));
}

TEST(AudioEngineStateTest, StartRecordingWhilePlaying) {
  EngineState next = Playing();
  next.input_enabled = true;
  next.input_running = true;
  next.input_muted = false;
  // Enabling input changes the graph, so the engine restarts. Output stays
  // enabled, so no recreate and no new engine.
  EXPECT_THAT(Plan(Playing(), next),
              ElementsAre("stop", "will_enable", "check_permission", "check_category",
                          "configure_vp", "enable_input", "ducking", "vp_bypass", "agc",
                          "start_playout_buffer", "start_record_buffer", "start"));
}

TEST(AudioEngineStateTest, StopRecordingWhilePlayingRecreatesTheEngine) {
  EngineState next = PlayingAndRecording();
  next.input_enabled = false;
  next.input_running = false;
  EXPECT_THAT(Plan(PlayingAndRecording(), next),
              ElementsAre("stop", "stop_playout_buffer", "stop_record_buffer", "release",
                          "create", "enable_output", "did_disable", "devices",
                          "start_playout_buffer", "start"));
}

TEST(AudioEngineStateTest, StopRecordingOnly) {
  EXPECT_THAT(Plan(Recording(), Idle()),
              ElementsAre("stop", "stop_playout_buffer", "stop_record_buffer", "disable_output",
                          "disable_input", "did_disable", "final_release"));
}

TEST(AudioEngineStateTest, RestartModeMuteWhilePlayingRecreatesTheEngine) {
  EngineState prev = PlayingAndRecording();
  prev.mute_mode = MuteMode::RestartEngine;
  EngineState next = prev;
  next.input_muted = true;
  EXPECT_THAT(Plan(prev, next),
              ElementsAre("stop", "stop_playout_buffer", "stop_record_buffer", "release",
                          "create", "enable_output", "did_disable", "devices",
                          "start_playout_buffer", "start"));
}

TEST(AudioEngineStateTest, RestartModeUnmuteWhilePlaying) {
  EngineState prev = PlayingAndRecording();
  prev.mute_mode = MuteMode::RestartEngine;
  prev.input_muted = true;
  EngineState next = prev;
  next.input_muted = false;
  EXPECT_THAT(Plan(prev, next),
              ElementsAre("stop", "will_enable", "check_permission", "check_category",
                          "configure_vp", "enable_input", "ducking", "vp_bypass", "agc",
                          "start_playout_buffer", "start_record_buffer", "start"));
}

TEST(AudioEngineStateTest, RestartModeMuteRecordingOnlyReleasesTheEngine) {
  EngineState prev = Recording();
  prev.mute_mode = MuteMode::RestartEngine;
  EngineState next = prev;
  next.input_muted = true;
  EXPECT_THAT(Plan(prev, next),
              ElementsAre("stop", "stop_playout_buffer", "stop_record_buffer", "disable_output",
                          "disable_input", "did_disable", "final_release"));
}

TEST(AudioEngineStateTest, VoiceProcessingModeMuteTouchesNoStep) {
  EngineState prev = PlayingAndRecording();
  EngineState next = prev;
  next.input_muted = true;
  // The voice processing mute itself is a property write outside the plan.
  EXPECT_THAT(Plan(prev, next), ElementsAre("start_playout_buffer", "start_record_buffer"));
}

TEST(AudioEngineStateTest, VoiceProcessingOffRecreates) {
  EngineState next = PlayingAndRecording();
  next.voice_processing_enabled = false;
  // A fresh engine has voice processing off, so there is nothing to configure.
  EXPECT_THAT(Plan(PlayingAndRecording(), next),
              ElementsAre("stop", "stop_playout_buffer", "stop_record_buffer", "release",
                          "create", "enable_output", "enable_input", "devices",
                          "start_playout_buffer", "start_record_buffer", "start"));
}

TEST(AudioEngineStateTest, VoiceProcessingOnRecreatesAndConfigures) {
  EngineState prev = PlayingAndRecording();
  prev.voice_processing_enabled = false;
  EXPECT_THAT(Plan(prev, PlayingAndRecording()),
              ElementsAre("stop", "stop_playout_buffer", "stop_record_buffer", "release",
                          "create", "configure_vp", "enable_output", "enable_input", "ducking",
                          "vp_bypass", "agc", "devices", "start_playout_buffer",
                          "start_record_buffer", "start"));
}

TEST(AudioEngineStateTest, OutputDeviceChangeRecreates) {
  EngineState next = Playing();
  next.output_device_id = 7;
  EXPECT_THAT(Plan(Playing(), next),
              ElementsAre("stop", "stop_playout_buffer", "stop_record_buffer", "release",
                          "create", "enable_output", "devices", "start_playout_buffer", "start"));
}

TEST(AudioEngineStateTest, DefaultOutputDeviceChangeRecreatesOnlyOnTheDefault) {
  EngineState next = Playing();
  next.default_output_device_update_count++;
  EXPECT_TRUE(PlanDeviceTransition({Playing(), next}).recreate_engine);

  EngineState explicit_prev = Playing();
  explicit_prev.output_device_id = 7;
  EngineState explicit_next = explicit_prev;
  explicit_next.default_output_device_update_count++;
  EXPECT_THAT(Plan(explicit_prev, explicit_next),
              ElementsAre("stop_record_buffer", "start_playout_buffer"));
}

TEST(AudioEngineStateTest, AvailabilityOffWhileRunning) {
  EngineState next = PlayingAndRecording();
  next.input_available = false;
  next.output_available = false;
  EXPECT_THAT(Plan(PlayingAndRecording(), next),
              ElementsAre("stop", "stop_playout_buffer", "stop_record_buffer", "disable_output",
                          "disable_input", "did_disable", "final_release"));
}

TEST(AudioEngineStateTest, AvailabilityBackOn) {
  EngineState prev = PlayingAndRecording();
  prev.input_available = false;
  prev.output_available = false;
  EXPECT_THAT(Plan(prev, PlayingAndRecording()), ElementsAre(
      "create", "will_enable", "check_permission", "check_category", "configure_vp",
      "enable_output", "enable_input", "ducking", "vp_bypass", "agc", "devices",
      "start_playout_buffer", "start_record_buffer", "start"));
}

// Interruption begin and end in each running state.
TEST(AudioEngineStateTest, InterruptionBeginStopsARunningEngine) {
  for (const EngineState& running : {Playing(), Recording(), PlayingAndRecording()}) {
    TransitionPlan plan = PlanDeviceTransition({running, Interrupted(running)});
    EXPECT_TRUE(plan.stop_engine);
    EXPECT_FALSE(plan.start_engine);
    EXPECT_FALSE(plan.start_engine_if_not_running);
    EXPECT_FALSE(plan.release_engine);
    EXPECT_FALSE(plan.final_release);
    EXPECT_FALSE(plan.did_disable);
  }
}

TEST(AudioEngineStateTest, InterruptionEndRestartsOnlyAStoppedEngine) {
  for (const EngineState& running : {Playing(), Recording(), PlayingAndRecording()}) {
    TransitionPlan plan = PlanDeviceTransition({Interrupted(running), running});
    // The state stays running across the interruption, so the plain start
    // and stop steps do not fire. Both depend on the engine's actual state.
    EXPECT_FALSE(plan.stop_engine);
    EXPECT_TRUE(plan.stop_engine_if_stopped_during_interruption);
    EXPECT_FALSE(plan.start_engine);
    EXPECT_TRUE(plan.start_engine_if_not_running);
    EXPECT_FALSE(plan.create_engine);
    EXPECT_FALSE(plan.will_enable);
  }
}

TEST(AudioEngineStateTest, InterruptionWhileEnabledButNotRunning) {
  EXPECT_THAT(Plan(PlayoutEnabled(), Interrupted(PlayoutEnabled())),
              ElementsAre("stop_record_buffer", "start_playout_buffer"));
  EXPECT_THAT(Plan(Interrupted(PlayoutEnabled()), PlayoutEnabled()),
              ElementsAre("stop_record_buffer", "start_playout_buffer"));
}

TEST(AudioEngineStateTest, InterruptionWhileIdle) {
  EXPECT_THAT(Plan(Idle(), Interrupted(Idle())),
              ElementsAre("stop_playout_buffer", "stop_record_buffer"));
  EXPECT_THAT(Plan(Interrupted(Idle()), Idle()),
              ElementsAre("stop_playout_buffer", "stop_record_buffer"));
}

TEST(AudioEngineStateTest, StartWhileInterruptedIsNotGated) {
  // Characterization: nothing keeps an engine from starting while the
  // session is interrupted.
  EXPECT_TRUE(PlanDeviceTransition({Interrupted(Idle()), Interrupted(Playing())}).start_engine);
}

// ReconfigureEngine shuts the engine down by clearing the enabled and running
// flags, then restores the previous state. In recording always prepared mode
// the persistent flag keeps input enabled, so the graph is not rebuilt.
// Characterization of today's behavior, see the plan's PR 7.
TEST(AudioEngineStateTest, ReconfigureInPersistentModeDoesNotRebuildTheGraph) {
  EngineState current = PlayingAndRecording();
  current.input_enabled_persistent_mode = true;

  EngineState shutdown = current;
  shutdown.input_enabled = false;
  shutdown.input_running = false;
  shutdown.output_enabled = false;
  shutdown.output_running = false;

  TransitionPlan down = PlanDeviceTransition({current, shutdown});
  EXPECT_TRUE(down.stop_engine);
  EXPECT_FALSE(down.release_engine);
  EXPECT_FALSE(down.disable_input);
  EXPECT_FALSE(down.disable_output);
  EXPECT_FALSE(down.did_disable);
  EXPECT_FALSE(down.final_release);

  TransitionPlan up = PlanDeviceTransition({shutdown, current});
  EXPECT_TRUE(up.start_engine);
  EXPECT_FALSE(up.create_engine);
  EXPECT_FALSE(up.enable_input);
  EXPECT_FALSE(up.enable_output);
  EXPECT_FALSE(up.will_enable);
}

TEST(AudioEngineStateTest, ReconfigureWithoutPersistentModeRebuilds) {
  EngineState current = PlayingAndRecording();
  EngineState shutdown = current;
  shutdown.input_enabled = false;
  shutdown.input_running = false;
  shutdown.output_enabled = false;
  shutdown.output_running = false;

  EXPECT_TRUE(PlanDeviceTransition({current, shutdown}).final_release);
  TransitionPlan up = PlanDeviceTransition({shutdown, current});
  EXPECT_TRUE(up.create_engine);
  EXPECT_TRUE(up.will_enable);
  EXPECT_TRUE(up.start_engine);
}

TEST(AudioEngineStateTest, VoiceProcessingWasConfigured) {
  EXPECT_FALSE(PlanDeviceTransition({Idle(), Recording()}).voice_processing_was_configured);
  EXPECT_TRUE(
      PlanDeviceTransition({Recording(), Recording()}).voice_processing_was_configured);

  EngineState vp_off = Recording();
  vp_off.voice_processing_enabled = false;
  EXPECT_FALSE(PlanDeviceTransition({vp_off, vp_off}).voice_processing_was_configured);

  // A recreate starts from a fresh engine.
  EngineState other_device = Recording();
  other_device.input_device_id = 7;
  EXPECT_FALSE(
      PlanDeviceTransition({Recording(), other_device}).voice_processing_was_configured);
}

TEST(AudioEngineStateTest, DuckingChangeReappliesOnlyDucking) {
  EngineState next = Recording();
  next.ducking_level = AudioDuckingLevelMax;
  EXPECT_THAT(Plan(Recording(), next),
              ElementsAre("ducking", "start_playout_buffer", "start_record_buffer"));
}

TEST(AudioEngineStateTest, RenderModePredicates) {
  EngineState manual;
  manual.render_mode = RenderMode::Manual;
  EXPECT_TRUE((EngineStateUpdate{Idle(), manual}.DidEnableManualRenderingMode()));
  EXPECT_FALSE((EngineStateUpdate{Idle(), manual}.DidEnableDeviceRenderingMode()));
  EXPECT_TRUE((EngineStateUpdate{manual, Idle()}.DidEnableDeviceRenderingMode()));
  EXPECT_FALSE((EngineStateUpdate{manual, manual}.DidEnableManualRenderingMode()));
}

TEST(AudioEngineStateTest, IdleToIdleNeedsNoEngine) {
  TransitionPlan plan = PlanDeviceTransition({Idle(), Idle()});
  EXPECT_FALSE(plan.create_engine);
  EXPECT_FALSE(plan.final_release);
  EXPECT_FALSE(plan.start_engine);
  EXPECT_THAT(Steps(TransitionPlan()), IsEmpty());
}

// MARK: - Rebuild after a failed transition

TEST(AudioEngineStateTest, RebuildBaselineHasNothingEnabledAndKeepsConfiguration) {
  EngineState committed = PlayingAndRecording();
  committed.input_enabled_persistent_mode = true;
  committed.mute_mode = MuteMode::InputMixer;
  committed.voice_processing_enabled = false;
  committed.output_device_id = 7;
  committed.default_input_device_update_count = 3;
  committed.is_interrupted = true;

  EngineState baseline = RebuildBaseline(committed);
  EXPECT_FALSE(baseline.IsAnyEnabled());
  EXPECT_FALSE(baseline.IsAnyRunning());
  EXPECT_EQ(baseline.mute_mode, MuteMode::InputMixer);
  EXPECT_FALSE(baseline.voice_processing_enabled);
  EXPECT_EQ(baseline.output_device_id, 7u);
  EXPECT_EQ(baseline.default_input_device_update_count, 3u);
  EXPECT_TRUE(baseline.is_interrupted);
}

TEST(AudioEngineStateTest, RebuildIsAFreshEnable) {
  // Same steps as from idle: a new engine, will-enable, then start. Nothing to
  // stop or release, so a failure in a rebuild commits nothing.
  EXPECT_EQ(Plan(RebuildBaseline(Playing()), Playing()), Plan(Idle(), Playing()));
  EXPECT_EQ(Plan(RebuildBaseline(PlayingAndRecording()), PlayingAndRecording()),
            Plan(Idle(), Recording()));

  TransitionPlan plan = PlanDeviceTransition({RebuildBaseline(Playing()), Playing()});
  EXPECT_FALSE(plan.stop_engine);
  EXPECT_FALSE(plan.release_engine);
  EXPECT_FALSE(plan.final_release);
  EXPECT_TRUE(plan.create_engine);
  EXPECT_TRUE(plan.will_enable);
  EXPECT_TRUE(plan.start_engine);
}

TEST(AudioEngineStateTest, StopWhileRebuildPendingTouchesNoEngine) {
  TransitionPlan plan = PlanDeviceTransition({RebuildBaseline(Playing()), Idle()});
  EXPECT_FALSE(plan.stop_engine);
  EXPECT_FALSE(plan.did_disable);
  EXPECT_FALSE(plan.final_release);
  EXPECT_FALSE(plan.create_engine);
}

TEST(AudioEngineStateTest, RebuildToAnotherDeviceStartsFromNothing) {
  EngineState next = Playing();
  next.output_device_id = 7;
  TransitionPlan plan = PlanDeviceTransition({RebuildBaseline(Playing()), next});
  // The device change still counts as a recreate, but there is no engine to
  // release. ApplyDeviceEngineState skips the release without an engine.
  EXPECT_TRUE(plan.release_engine);
  EXPECT_FALSE(plan.stop_engine);
  EXPECT_TRUE(plan.create_engine);
  EXPECT_TRUE(plan.will_enable);
}

}  // namespace
}  // namespace audio_engine
}  // namespace webrtc
