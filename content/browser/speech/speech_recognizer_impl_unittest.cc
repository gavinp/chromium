// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "content/browser/browser_thread_impl.h"
#include "content/browser/speech/google_one_shot_remote_engine.h"
#include "content/browser/speech/speech_recognizer_impl.h"
#include "content/public/browser/speech_recognition_event_listener.h"
#include "content/test/test_url_fetcher_factory.h"
#include "media/audio/audio_manager.h"
#include "media/audio/fake_audio_input_stream.h"
#include "media/audio/fake_audio_output_stream.h"
#include "media/audio/test_audio_input_controller_factory.h"
#include "net/base/net_errors.h"
#include "net/url_request/url_request_status.h"
#include "testing/gtest/include/gtest/gtest.h"

using content::BrowserThread;
using content::BrowserThreadImpl;
using media::AudioInputController;
using media::TestAudioInputController;
using media::TestAudioInputControllerFactory;

namespace {

class MockAudioManager : public AudioManagerBase {
 public:
  MockAudioManager() {
    audio_thread_.reset(new base::Thread("MockAudioThread"));
    CHECK(audio_thread_->Start());
  }
  virtual bool HasAudioOutputDevices() OVERRIDE { return true; }
  virtual bool HasAudioInputDevices() OVERRIDE { return true; }
  virtual string16 GetAudioInputDeviceModel() OVERRIDE { return string16(); }
  virtual bool CanShowAudioInputSettings() OVERRIDE { return false; }
  virtual void ShowAudioInputSettings() OVERRIDE {}
  virtual void GetAudioInputDeviceNames(
      media::AudioDeviceNames* device_names) OVERRIDE {}
  virtual AudioOutputStream* MakeAudioOutputStream(
        const AudioParameters& params) OVERRIDE {
    return FakeAudioOutputStream::MakeFakeStream(this, params);
  }
  virtual AudioOutputStream* MakeAudioOutputStreamProxy(
        const AudioParameters& params) OVERRIDE {
    NOTREACHED();
    return NULL;
  }
  virtual AudioInputStream* MakeAudioInputStream(
        const AudioParameters& params, const std::string& device_id) OVERRIDE {
    return FakeAudioInputStream::MakeFakeStream(this, params);
  }
  virtual AudioOutputStream* MakeLinearOutputStream(
      const AudioParameters& params) OVERRIDE {
    NOTREACHED();
    return NULL;
  }
  virtual AudioOutputStream* MakeLowLatencyOutputStream(
      const AudioParameters& params) OVERRIDE {
    NOTREACHED();
    return NULL;
  }
  virtual AudioInputStream* MakeLinearInputStream(
      const AudioParameters& params, const std::string& device_id) OVERRIDE {
    NOTREACHED();
    return NULL;
  }
  virtual AudioInputStream* MakeLowLatencyInputStream(
      const AudioParameters& params, const std::string& device_id) OVERRIDE {
    NOTREACHED();
    return NULL;
  }
  virtual void MuteAll() OVERRIDE {}
  virtual void UnMuteAll() OVERRIDE {}
  virtual bool IsRecordingInProcess() OVERRIDE { return false; }
  virtual scoped_refptr<base::MessageLoopProxy> GetMessageLoop() OVERRIDE {
    return audio_thread_->message_loop_proxy();
  }
  virtual void Init() OVERRIDE {};
 private:
  scoped_ptr<base::Thread> audio_thread_;
  DISALLOW_COPY_AND_ASSIGN(MockAudioManager);
};
}  // namespace


namespace speech {

class SpeechRecognizerImplTest : public content::SpeechRecognitionEventListener,
                                 public testing::Test {
 public:
  SpeechRecognizerImplTest()
      : io_thread_(BrowserThread::IO, &message_loop_),
        audio_manager_(new MockAudioManager()),
        audio_ended_(false),
        recognition_ended_(false),
        result_received_(false),
        audio_started_(false),
        error_(content::SPEECH_RECOGNITION_ERROR_NONE),
        volume_(-1.0f) {
    recognizer_ = new SpeechRecognizerImpl(
        this, 1, std::string(), std::string(), NULL, false, std::string(),
        std::string());
    recognizer_->SetAudioManagerForTesting(audio_manager_.get());
    int audio_packet_length_bytes =
        (SpeechRecognizerImpl::kAudioSampleRate *
         GoogleOneShotRemoteEngine::kAudioPacketIntervalMs *
         ChannelLayoutToChannelCount(SpeechRecognizerImpl::kChannelLayout) *
         SpeechRecognizerImpl::kNumBitsPerAudioSample) / (8 * 1000);
    audio_packet_.resize(audio_packet_length_bytes);
  }

  // Overridden from content::SpeechRecognitionEventListener:
  virtual void OnAudioStart(int caller_id) OVERRIDE {
    audio_started_ = true;
  }

  virtual void OnAudioEnd(int caller_id) OVERRIDE {
    audio_ended_ = true;
  }

  virtual void OnRecognitionResult(
      int caller_id, const content::SpeechRecognitionResult& result) OVERRIDE {
    result_received_ = true;
  }

  virtual void OnRecognitionError(
      int caller_id,
      const content::SpeechRecognitionError& error) OVERRIDE {
    error_ = error.code;
  }

  virtual void OnAudioLevelsChange(int caller_id, float volume,
                                   float noise_volume) OVERRIDE {
    volume_ = volume;
    noise_volume_ = noise_volume;
  }

  virtual void OnRecognitionEnd(int caller_id) OVERRIDE {
    recognition_ended_ = true;
  }

  virtual void OnRecognitionStart(int caller_id) OVERRIDE {}
  virtual void OnEnvironmentEstimationComplete(int caller_id) OVERRIDE {}
  virtual void OnSoundStart(int caller_id) OVERRIDE {}
  virtual void OnSoundEnd(int caller_id) OVERRIDE {}

  // testing::Test methods.
  virtual void SetUp() OVERRIDE {
    AudioInputController::set_factory_for_testing(
        &audio_input_controller_factory_);
  }

  virtual void TearDown() OVERRIDE {
    AudioInputController::set_factory_for_testing(NULL);
  }

  void FillPacketWithTestWaveform() {
    // Fill the input with a simple pattern, a 125Hz sawtooth waveform.
    for (size_t i = 0; i < audio_packet_.size(); ++i)
      audio_packet_[i] = static_cast<uint8>(i);
  }

  void FillPacketWithNoise() {
    int value = 0;
    int factor = 175;
    for (size_t i = 0; i < audio_packet_.size(); ++i) {
      value += factor;
      audio_packet_[i] = value % 100;
    }
  }

 protected:
  MessageLoopForIO message_loop_;
  BrowserThreadImpl io_thread_;
  scoped_refptr<SpeechRecognizerImpl> recognizer_;
  scoped_ptr<AudioManager> audio_manager_;
  bool audio_ended_;
  bool recognition_ended_;
  bool result_received_;
  bool audio_started_;
  content::SpeechRecognitionErrorCode error_;
  TestURLFetcherFactory url_fetcher_factory_;
  TestAudioInputControllerFactory audio_input_controller_factory_;
  std::vector<uint8> audio_packet_;
  float volume_;
  float noise_volume_;
};

TEST_F(SpeechRecognizerImplTest, StopNoData) {
  // Check for callbacks when stopping record before any audio gets recorded.
  recognizer_->StartRecognition();
  recognizer_->AbortRecognition();
  EXPECT_FALSE(audio_ended_);
  EXPECT_FALSE(recognition_ended_);
  EXPECT_FALSE(result_received_);
  EXPECT_FALSE(audio_started_);
  EXPECT_EQ(content::SPEECH_RECOGNITION_ERROR_NONE, error_);
}

TEST_F(SpeechRecognizerImplTest, CancelNoData) {
  // Check for callbacks when canceling recognition before any audio gets
  // recorded.
  recognizer_->StartRecognition();
  recognizer_->StopAudioCapture();
  EXPECT_TRUE(audio_ended_);
  EXPECT_TRUE(recognition_ended_);
  EXPECT_FALSE(result_received_);
  EXPECT_FALSE(audio_started_);
  EXPECT_EQ(content::SPEECH_RECOGNITION_ERROR_NONE, error_);
}

TEST_F(SpeechRecognizerImplTest, StopWithData) {
  // Start recording, give some data and then stop. This should wait for the
  // network callback to arrive before completion.
  recognizer_->StartRecognition();
  TestAudioInputController* controller =
      audio_input_controller_factory_.controller();
  ASSERT_TRUE(controller);

  // Try sending 5 chunks of mock audio data and verify that each of them
  // resulted immediately in a packet sent out via the network. This verifies
  // that we are streaming out encoded data as chunks without waiting for the
  // full recording to complete.
  const size_t kNumChunks = 5;
  for (size_t i = 0; i < kNumChunks; ++i) {
    controller->event_handler()->OnData(controller, &audio_packet_[0],
                                        audio_packet_.size());
    MessageLoop::current()->RunAllPending();
    TestURLFetcher* fetcher = url_fetcher_factory_.GetFetcherByID(0);
    ASSERT_TRUE(fetcher);
    EXPECT_EQ(i + 1, fetcher->upload_chunks().size());
  }

  recognizer_->StopAudioCapture();
  EXPECT_TRUE(audio_started_);
  EXPECT_TRUE(audio_ended_);
  EXPECT_FALSE(recognition_ended_);
  EXPECT_FALSE(result_received_);
  EXPECT_EQ(content::SPEECH_RECOGNITION_ERROR_NONE, error_);

  // Issue the network callback to complete the process.
  TestURLFetcher* fetcher = url_fetcher_factory_.GetFetcherByID(0);
  ASSERT_TRUE(fetcher);

  fetcher->set_url(fetcher->GetOriginalURL());
  net::URLRequestStatus status;
  status.set_status(net::URLRequestStatus::SUCCESS);
  fetcher->set_status(status);
  fetcher->set_response_code(200);
  fetcher->SetResponseString(
      "{\"status\":0,\"hypotheses\":[{\"utterance\":\"123\"}]}");
  fetcher->delegate()->OnURLFetchComplete(fetcher);

  EXPECT_TRUE(recognition_ended_);
  EXPECT_TRUE(result_received_);
  EXPECT_EQ(content::SPEECH_RECOGNITION_ERROR_NONE, error_);
}

TEST_F(SpeechRecognizerImplTest, CancelWithData) {
  // Start recording, give some data and then cancel. This should create
  // a network request but give no callbacks.
  recognizer_->StartRecognition();
  TestAudioInputController* controller =
      audio_input_controller_factory_.controller();
  ASSERT_TRUE(controller);
  controller->event_handler()->OnData(controller, &audio_packet_[0],
                                      audio_packet_.size());
  MessageLoop::current()->RunAllPending();
  recognizer_->AbortRecognition();
  ASSERT_TRUE(url_fetcher_factory_.GetFetcherByID(0));
  EXPECT_TRUE(audio_started_);
  EXPECT_FALSE(audio_ended_);
  EXPECT_FALSE(recognition_ended_);
  EXPECT_FALSE(result_received_);
  EXPECT_EQ(content::SPEECH_RECOGNITION_ERROR_NONE, error_);
}

TEST_F(SpeechRecognizerImplTest, ConnectionError) {
  // Start recording, give some data and then stop. Issue the network callback
  // with a connection error and verify that the recognizer bubbles the error up
  recognizer_->StartRecognition();
  TestAudioInputController* controller =
      audio_input_controller_factory_.controller();
  ASSERT_TRUE(controller);
  controller->event_handler()->OnData(controller, &audio_packet_[0],
                                      audio_packet_.size());
  MessageLoop::current()->RunAllPending();
  TestURLFetcher* fetcher = url_fetcher_factory_.GetFetcherByID(0);
  ASSERT_TRUE(fetcher);

  recognizer_->StopAudioCapture();
  EXPECT_TRUE(audio_started_);
  EXPECT_TRUE(audio_ended_);
  EXPECT_FALSE(recognition_ended_);
  EXPECT_FALSE(result_received_);
  EXPECT_EQ(content::SPEECH_RECOGNITION_ERROR_NONE, error_);

  // Issue the network callback to complete the process.
  fetcher->set_url(fetcher->GetOriginalURL());
  net::URLRequestStatus status;
  status.set_status(net::URLRequestStatus::FAILED);
  status.set_error(net::ERR_CONNECTION_REFUSED);
  fetcher->set_status(status);
  fetcher->set_response_code(0);
  fetcher->SetResponseString("");
  fetcher->delegate()->OnURLFetchComplete(fetcher);

  EXPECT_FALSE(recognition_ended_);
  EXPECT_FALSE(result_received_);
  EXPECT_EQ(content::SPEECH_RECOGNITION_ERROR_NETWORK, error_);
}

TEST_F(SpeechRecognizerImplTest, ServerError) {
  // Start recording, give some data and then stop. Issue the network callback
  // with a 500 error and verify that the recognizer bubbles the error up
  recognizer_->StartRecognition();
  TestAudioInputController* controller =
      audio_input_controller_factory_.controller();
  ASSERT_TRUE(controller);
  controller->event_handler()->OnData(controller, &audio_packet_[0],
                                      audio_packet_.size());
  MessageLoop::current()->RunAllPending();
  TestURLFetcher* fetcher = url_fetcher_factory_.GetFetcherByID(0);
  ASSERT_TRUE(fetcher);

  recognizer_->StopAudioCapture();
  EXPECT_TRUE(audio_started_);
  EXPECT_TRUE(audio_ended_);
  EXPECT_FALSE(recognition_ended_);
  EXPECT_FALSE(result_received_);
  EXPECT_EQ(content::SPEECH_RECOGNITION_ERROR_NONE, error_);

  // Issue the network callback to complete the process.
  fetcher->set_url(fetcher->GetOriginalURL());
  net::URLRequestStatus status;
  status.set_status(net::URLRequestStatus::SUCCESS);
  fetcher->set_status(status);
  fetcher->set_response_code(500);
  fetcher->SetResponseString("Internal Server Error");
  fetcher->delegate()->OnURLFetchComplete(fetcher);

  EXPECT_FALSE(recognition_ended_);
  EXPECT_FALSE(result_received_);
  EXPECT_EQ(content::SPEECH_RECOGNITION_ERROR_NETWORK, error_);
}

TEST_F(SpeechRecognizerImplTest, AudioControllerErrorNoData) {
  // Check if things tear down properly if AudioInputController threw an error.
  recognizer_->StartRecognition();
  TestAudioInputController* controller =
      audio_input_controller_factory_.controller();
  ASSERT_TRUE(controller);
  controller->event_handler()->OnError(controller, 0);
  MessageLoop::current()->RunAllPending();
  EXPECT_FALSE(audio_started_);
  EXPECT_FALSE(audio_ended_);
  EXPECT_FALSE(recognition_ended_);
  EXPECT_FALSE(result_received_);
  EXPECT_EQ(content::SPEECH_RECOGNITION_ERROR_AUDIO, error_);
}

TEST_F(SpeechRecognizerImplTest, AudioControllerErrorWithData) {
  // Check if things tear down properly if AudioInputController threw an error
  // after giving some audio data.
  recognizer_->StartRecognition();
  TestAudioInputController* controller =
      audio_input_controller_factory_.controller();
  ASSERT_TRUE(controller);
  controller->event_handler()->OnData(controller, &audio_packet_[0],
                                      audio_packet_.size());
  controller->event_handler()->OnError(controller, 0);
  MessageLoop::current()->RunAllPending();
  ASSERT_TRUE(url_fetcher_factory_.GetFetcherByID(0));
  EXPECT_TRUE(audio_started_);
  EXPECT_FALSE(audio_ended_);
  EXPECT_FALSE(recognition_ended_);
  EXPECT_FALSE(result_received_);
  EXPECT_EQ(content::SPEECH_RECOGNITION_ERROR_AUDIO, error_);
}

TEST_F(SpeechRecognizerImplTest, NoSpeechCallbackIssued) {
  // Start recording and give a lot of packets with audio samples set to zero.
  // This should trigger the no-speech detector and issue a callback.
  recognizer_->StartRecognition();
  TestAudioInputController* controller =
      audio_input_controller_factory_.controller();
  ASSERT_TRUE(controller);
  controller = audio_input_controller_factory_.controller();
  ASSERT_TRUE(controller);

  int num_packets = (SpeechRecognizerImpl::kNoSpeechTimeoutMs) /
                     GoogleOneShotRemoteEngine::kAudioPacketIntervalMs;
  // The vector is already filled with zero value samples on create.
  for (int i = 0; i < num_packets; ++i) {
    controller->event_handler()->OnData(controller, &audio_packet_[0],
                                        audio_packet_.size());
  }
  MessageLoop::current()->RunAllPending();
  EXPECT_TRUE(audio_started_);
  EXPECT_FALSE(audio_ended_);
  EXPECT_FALSE(recognition_ended_);
  EXPECT_FALSE(result_received_);
  EXPECT_EQ(content::SPEECH_RECOGNITION_ERROR_NO_SPEECH, error_);
}

TEST_F(SpeechRecognizerImplTest, NoSpeechCallbackNotIssued) {
  // Start recording and give a lot of packets with audio samples set to zero
  // and then some more with reasonably loud audio samples. This should be
  // treated as normal speech input and the no-speech detector should not get
  // triggered.
  recognizer_->StartRecognition();
  TestAudioInputController* controller =
      audio_input_controller_factory_.controller();
  ASSERT_TRUE(controller);
  controller = audio_input_controller_factory_.controller();
  ASSERT_TRUE(controller);

  int num_packets = (SpeechRecognizerImpl::kNoSpeechTimeoutMs) /
                     GoogleOneShotRemoteEngine::kAudioPacketIntervalMs;

  // The vector is already filled with zero value samples on create.
  for (int i = 0; i < num_packets / 2; ++i) {
    controller->event_handler()->OnData(controller, &audio_packet_[0],
                                        audio_packet_.size());
  }

  FillPacketWithTestWaveform();
  for (int i = 0; i < num_packets / 2; ++i) {
    controller->event_handler()->OnData(controller, &audio_packet_[0],
                                        audio_packet_.size());
  }

  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(content::SPEECH_RECOGNITION_ERROR_NONE, error_);
  EXPECT_TRUE(audio_started_);
  EXPECT_FALSE(audio_ended_);
  EXPECT_FALSE(recognition_ended_);
  recognizer_->AbortRecognition();
}

TEST_F(SpeechRecognizerImplTest, SetInputVolumeCallback) {
  // Start recording and give a lot of packets with audio samples set to zero
  // and then some more with reasonably loud audio samples. Check that we don't
  // get the callback during estimation phase, then get zero for the silence
  // samples and proper volume for the loud audio.
  recognizer_->StartRecognition();
  TestAudioInputController* controller =
      audio_input_controller_factory_.controller();
  ASSERT_TRUE(controller);
  controller = audio_input_controller_factory_.controller();
  ASSERT_TRUE(controller);

  // Feed some samples to begin with for the endpointer to do noise estimation.
  int num_packets = SpeechRecognizerImpl::kEndpointerEstimationTimeMs /
                    GoogleOneShotRemoteEngine::kAudioPacketIntervalMs;
  FillPacketWithNoise();
  for (int i = 0; i < num_packets; ++i) {
    controller->event_handler()->OnData(controller, &audio_packet_[0],
                                        audio_packet_.size());
  }
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(-1.0f, volume_);  // No audio volume set yet.

  // The vector is already filled with zero value samples on create.
  controller->event_handler()->OnData(controller, &audio_packet_[0],
                                      audio_packet_.size());
  MessageLoop::current()->RunAllPending();
  EXPECT_FLOAT_EQ(0.74939233f, volume_);

  FillPacketWithTestWaveform();
  controller->event_handler()->OnData(controller, &audio_packet_[0],
                                      audio_packet_.size());
  MessageLoop::current()->RunAllPending();
  EXPECT_FLOAT_EQ(0.89926866f, volume_);
  EXPECT_FLOAT_EQ(0.75071919f, noise_volume_);

  EXPECT_EQ(content::SPEECH_RECOGNITION_ERROR_NONE, error_);
  EXPECT_FALSE(audio_ended_);
  EXPECT_FALSE(recognition_ended_);
  recognizer_->AbortRecognition();
}

}  // namespace speech
