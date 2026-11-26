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

#import <AVFAudio/AVFAudio.h>

#include <map>
#include <string>
#include <vector>

#include "api/audio/audio_device.h"
#include "api/environment/environment_factory.h"
#include "api/make_ref_counted.h"
#include "api/scoped_refptr.h"
#include "modules/audio_device/audio_engine_device.h"
#include "test/run_loop.h"
#include "test/gmock.h"
#include "test/gtest.h"

// Drives AudioEngineDevice through the transitions that the observer hooks and
// the fault injector can make fail, and checks the hook order, the committed
// state and that no hook receives a nil engine. Output only, so no microphone
// permission is needed. Skips on a host without an output device.

namespace webrtc {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

constexpr int32_t kHookError = -1234;

class RecordingObserver : public AudioDeviceObserver {
 public:
  // Fails the next call of `hook` with kHookError.
  void FailOnce(const std::string& hook) { fail_once_[hook] = true; }

  std::vector<std::string> TakeCalls() {
    std::vector<std::string> calls;
    calls.swap(calls_);
    return calls;
  }

  int nil_engine_calls() const { return nil_engine_calls_; }

  int32_t OnEngineDidCreate(AVAudioEngine* engine) override {
    return Record("DidCreate", engine);
  }
  int32_t OnEngineWillEnable(AVAudioEngine* engine, bool playout_enabled,
                             bool recording_enabled, bool voice_processing_enabled) override {
    return Record(Name("WillEnable", playout_enabled, recording_enabled), engine, "WillEnable");
  }
  int32_t OnEngineWillStart(AVAudioEngine* engine, bool playout_enabled,
                            bool recording_enabled) override {
    return Record(Name("WillStart", playout_enabled, recording_enabled), engine, "WillStart");
  }
  int32_t OnEngineDidStop(AVAudioEngine* engine, bool playout_enabled,
                          bool recording_enabled) override {
    return Record(Name("DidStop", playout_enabled, recording_enabled), engine, "DidStop");
  }
  int32_t OnEngineDidDisable(AVAudioEngine* engine, bool playout_enabled,
                             bool recording_enabled) override {
    return Record(Name("DidDisable", playout_enabled, recording_enabled), engine, "DidDisable");
  }
  int32_t OnEngineWillRelease(AVAudioEngine* engine) override {
    return Record("WillRelease", engine);
  }
  int32_t OnEngineWillConnectOutput(AVAudioEngine* engine, AVAudioNode* src, AVAudioNode* dst,
                                    AVAudioFormat* format, NSDictionary* context) override {
    return Record("WillConnectOutput", engine);
  }
  int32_t OnEngineWillConnectInput(AVAudioEngine* engine, AVAudioNode* src, AVAudioNode* dst,
                                   AVAudioFormat* format, NSDictionary* context) override {
    return Record("WillConnectInput", engine);
  }

 private:
  static std::string Name(const char* hook, bool playout, bool recording) {
    return std::string(hook) + "(" + (playout ? "1" : "0") + "," + (recording ? "1" : "0") + ")";
  }

  int32_t Record(const std::string& call, AVAudioEngine* engine, const std::string& hook = "") {
    const std::string key = hook.empty() ? call : hook;
    if (engine == nil) {
      nil_engine_calls_++;
      calls_.push_back(call + "[nil]");
    } else {
      calls_.push_back(call);
    }
    auto it = fail_once_.find(key);
    if (it != fail_once_.end() && it->second) {
      it->second = false;
      calls_.back() += "!";
      return kHookError;
    }
    return 0;
  }

  std::vector<std::string> calls_;
  std::map<std::string, bool> fail_once_;
  int nil_engine_calls_ = 0;
};

class AudioEngineDeviceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    adm_ = make_ref_counted<AudioEngineDevice>(env_, /*voice_processing_bypassed=*/false);
    ASSERT_EQ(adm_->Init(), 0);
    adm_->SetObserver(&observer_);
  }

  void TearDown() override {
    adm_->SetFaultInjectorForTesting(nullptr);
    adm_ = nullptr;
    EXPECT_EQ(observer_.nil_engine_calls(), 0);
  }

  // Starts playout, or skips the test when the host has no output device.
  void StartPlayoutOrSkip() {
    int32_t init = adm_->InitPlayout();
    if (init == kAudioEnginePlayoutDeviceNotAvailableError) {
      GTEST_SKIP() << "No output device";
    }
    ASSERT_EQ(init, 0);
    ASSERT_EQ(adm_->StartPlayout(), 0);
    ASSERT_TRUE(adm_->IsEngineRunning());
    observer_.TakeCalls();
  }

  AudioEngineDevice::EngineState State() {
    AudioEngineDevice::EngineState state;
    adm_->GetEngineState(&state);
    return state;
  }

  // A default output device change recreates the engine while playout runs.
  int32_t RecreateForDefaultDeviceChange() {
    AudioEngineDevice::EngineState state = State();
    state.default_output_device_update_count++;
    return adm_->SetEngineState(state);
  }

  void FailEngineStartOnce() {
    auto remaining = std::make_shared<int>(1);
    adm_->SetFaultInjectorForTesting(
        [remaining](AudioEngineDevice::FaultPoint point) -> int32_t {
          if (point == AudioEngineDevice::FaultPoint::kStartEngine && *remaining > 0) {
            (*remaining)--;
            return kAudioEnginePlayoutStartError;
          }
          return 0;
        });
  }

  void ProcessPendingTasks() { run_loop_.Flush(); }

  // Makes the test thread the current thread, the ADM thread.
  test::RunLoop run_loop_;
  const Environment env_ = CreateEnvironment();
  scoped_refptr<AudioEngineDevice> adm_;
  RecordingObserver observer_;
};

TEST_F(AudioEngineDeviceTest, PlayoutStartAndStop) {
  int32_t init = adm_->InitPlayout();
  if (init == kAudioEnginePlayoutDeviceNotAvailableError) {
    GTEST_SKIP() << "No output device";
  }
  ASSERT_EQ(init, 0);
  EXPECT_THAT(observer_.TakeCalls(),
              ElementsAre("DidCreate", "WillEnable(1,0)", "WillConnectOutput"));

  ASSERT_EQ(adm_->StartPlayout(), 0);
  EXPECT_THAT(observer_.TakeCalls(), ElementsAre("WillStart(1,0)"));
  EXPECT_TRUE(adm_->IsEngineRunning());
  EXPECT_TRUE(adm_->Playing());

  ASSERT_EQ(adm_->StopPlayout(), 0);
  EXPECT_THAT(observer_.TakeCalls(),
              ElementsAre("DidStop(0,0)", "DidDisable(0,0)", "WillRelease"));
  EXPECT_FALSE(adm_->IsEngineRunning());
  EXPECT_FALSE(adm_->Playing());
}

// A failure before any step that stops or releases something rolls back and
// commits nothing.
TEST_F(AudioEngineDeviceTest, FailedEnableRollsBackAndCommitsNothing) {
  observer_.FailOnce("WillEnable");
  int32_t init = adm_->InitPlayout();
  if (init == kAudioEnginePlayoutDeviceNotAvailableError) {
    GTEST_SKIP() << "No output device";
  }
  EXPECT_EQ(init, kHookError);
  EXPECT_THAT(observer_.TakeCalls(),
              ElementsAre("DidCreate", "WillEnable(1,0)!", "WillRelease"));
  EXPECT_FALSE(adm_->PlayoutIsInitialized());
  EXPECT_FALSE(State().output_enabled);

  // The same request is retried.
  ASSERT_EQ(adm_->InitPlayout(), 0);
  ASSERT_EQ(adm_->StartPlayout(), 0);
  EXPECT_TRUE(adm_->IsEngineRunning());
}

TEST_F(AudioEngineDeviceTest, FailedStartFromIdleRollsBackTheEnable) {
  int32_t init = adm_->InitPlayout();
  if (init == kAudioEnginePlayoutDeviceNotAvailableError) {
    GTEST_SKIP() << "No output device";
  }
  ASSERT_EQ(init, 0);
  observer_.TakeCalls();

  FailEngineStartOnce();
  EXPECT_EQ(adm_->StartPlayout(), kAudioEnginePlayoutStartError);
  EXPECT_THAT(observer_.TakeCalls(), ElementsAre("WillStart(1,0)"));
  EXPECT_FALSE(adm_->Playing());
  EXPECT_FALSE(adm_->IsEngineRunning());

  ASSERT_EQ(adm_->StartPlayout(), 0);
  EXPECT_TRUE(adm_->IsEngineRunning());
}

// Interruption end with a failed restart. The stop ran at the begin, so the
// end itself stops nothing, and it commits nothing on failure: the next end
// restarts.
TEST_F(AudioEngineDeviceTest, FailedRestartAtInterruptionEnd) {
  StartPlayoutOrSkip();
  if (IsSkipped()) return;

  adm_->OnInterruptionBegin(0);
  ProcessPendingTasks();
  EXPECT_THAT(observer_.TakeCalls(), ElementsAre("DidStop(1,0)"));
  EXPECT_TRUE(adm_->IsInterrupted());

  FailEngineStartOnce();
  adm_->OnInterruptionEnd(true);
  ProcessPendingTasks();
  EXPECT_THAT(observer_.TakeCalls(), ElementsAre("WillStart(1,0)"));
  EXPECT_TRUE(adm_->IsInterrupted());
  EXPECT_FALSE(adm_->IsEngineRunning());

  adm_->OnInterruptionEnd(true);
  ProcessPendingTasks();
  EXPECT_THAT(observer_.TakeCalls(), ElementsAre("WillStart(1,0)"));
  EXPECT_FALSE(adm_->IsInterrupted());
  EXPECT_TRUE(adm_->IsEngineRunning());
}

// Failures during an engine recreate, after the running engine was stopped and
// released. Characterizes the behavior this commit is written against: nothing
// is committed, the module still reports playout with no running engine, and
// repeating the committed request does nothing.
struct RecreateFailure {
  const char* name;
  const char* hook;  // nullptr: the engine start fails
  std::vector<std::string> calls;
};

class AudioEngineDeviceRecreateFailureTest
    : public AudioEngineDeviceTest,
      public ::testing::WithParamInterface<RecreateFailure> {};

TEST_P(AudioEngineDeviceRecreateFailureTest, LeavesNoEngineAndCommitsNothing) {
  StartPlayoutOrSkip();
  if (IsSkipped()) return;
  const uint32_t count_before = State().default_output_device_update_count;

  const RecreateFailure& failure = GetParam();
  if (failure.hook != nullptr) {
    observer_.FailOnce(failure.hook);
  } else {
    FailEngineStartOnce();
  }
  EXPECT_NE(RecreateForDefaultDeviceChange(), 0);
  EXPECT_EQ(observer_.TakeCalls(), failure.calls);

  EXPECT_EQ(State().default_output_device_update_count, count_before);
  EXPECT_TRUE(adm_->Playing());
  EXPECT_FALSE(adm_->IsEngineRunning());

  // The committed request is a no-op.
  EXPECT_EQ(adm_->StartPlayout(), 0);
  EXPECT_THAT(observer_.TakeCalls(), IsEmpty());
  EXPECT_FALSE(adm_->IsEngineRunning());

  // Only the uncommitted request rebuilds, which also lets the fixture tear
  // down without a nil engine.
  EXPECT_EQ(RecreateForDefaultDeviceChange(), 0);
  EXPECT_TRUE(adm_->IsEngineRunning());
}

INSTANTIATE_TEST_SUITE_P(
    ,
    AudioEngineDeviceRecreateFailureTest,
    ::testing::Values(
        RecreateFailure{"EngineStart",
                        nullptr,
                        {"DidStop(1,0)", "WillRelease", "DidCreate", "WillConnectOutput",
                         "WillStart(1,0)", "WillRelease"}},
        RecreateFailure{"WillStart",
                        "WillStart",
                        {"DidStop(1,0)", "WillRelease", "DidCreate", "WillConnectOutput",
                         "WillStart(1,0)!", "WillRelease"}},
        RecreateFailure{"WillConnectOutput",
                        "WillConnectOutput",
                        {"DidStop(1,0)", "WillRelease", "DidCreate", "WillConnectOutput!",
                         "WillRelease"}},
        RecreateFailure{"DidCreate",
                        "DidCreate",
                        {"DidStop(1,0)", "WillRelease", "DidCreate!"}}),
    [](const ::testing::TestParamInfo<RecreateFailure>& info) { return info.param.name; });

}  // namespace
}  // namespace webrtc
