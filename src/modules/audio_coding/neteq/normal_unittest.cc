/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// Unit tests for Normal class.

#include "modules/audio_coding/neteq/normal.h"

#include <memory>
#include <vector>

#include "common_audio/signal_processing/include/signal_processing_library.h"
#include "modules/audio_coding/neteq/audio_multi_vector.h"
#include "modules/audio_coding/neteq/background_noise.h"
#include "modules/audio_coding/neteq/expand.h"
#include "modules/audio_coding/neteq/mock/mock_decoder_database.h"
#include "modules/audio_coding/neteq/mock/mock_expand.h"
#include "modules/audio_coding/neteq/random_vector.h"
#include "modules/audio_coding/neteq/statistics_calculator.h"
#include "modules/audio_coding/neteq/sync_buffer.h"
#include "test/gtest.h"

using ::testing::_;
using ::testing::Invoke;

namespace webrtc {

namespace {

int ExpandProcess120ms(AudioMultiVector* output) {
  AudioMultiVector dummy_audio(1, 11520u);
  dummy_audio.CopyTo(output);
  return 0;
}

}  // namespace

TEST(Normal, CreateAndDestroy) {
  MockDecoderDatabase db;
  int fs = 8000;
  size_t channels = 1;
  BackgroundNoise bgn(channels);
  SyncBuffer sync_buffer(1, 1000);
  RandomVector random_vector;
  StatisticsCalculator statistics;
  Expand expand(&bgn, &sync_buffer, &random_vector, &statistics, fs, channels);
  Normal normal(fs, &db, bgn, &expand, &statistics);
  EXPECT_CALL(db, Die());  // Called when `db` goes out of scope.
}

TEST(Normal, AvoidDivideByZero) {
  MockDecoderDatabase db;
  int fs = 8000;
  size_t channels = 1;
  BackgroundNoise bgn(channels);
  SyncBuffer sync_buffer(1, 1000);
  RandomVector random_vector;
  StatisticsCalculator statistics;
  MockExpand expand(&bgn, &sync_buffer, &random_vector, &statistics, fs,
                    channels);
  Normal normal(fs, &db, bgn, &expand, &statistics);

  int16_t input[1000] = {0};
  AudioMultiVector output(channels);

  // Zero input length.
  EXPECT_EQ(0, normal.Process(input, 0, NetEq::Mode::kExpand, &output));
  EXPECT_EQ(0u, output.Size());

  // Try to make energy_length >> scaling = 0;
  EXPECT_CALL(expand, SetParametersForNormalAfterExpand());
  EXPECT_CALL(expand, Process(_));
  EXPECT_CALL(expand, Reset());
  // If input_size_samples < 64, then energy_length in Normal::Process() will
  // be equal to input_size_samples. Since the input is all zeros, decoded_max
  // will be zero, and scaling will be >= 6. Thus, energy_length >> scaling = 0,
  // and using this as a denominator would lead to problems.
  int input_size_samples = 63;
  EXPECT_EQ(input_size_samples, normal.Process(input, input_size_samples,
                                               NetEq::Mode::kExpand, &output));

  EXPECT_CALL(db, Die());      // Called when `db` goes out of scope.
  EXPECT_CALL(expand, Die());  // Called when `expand` goes out of scope.
}

TEST(Normal, InputLengthAndChannelsDoNotMatch) {
  MockDecoderDatabase db;
  int fs = 8000;
  size_t channels = 2;
  BackgroundNoise bgn(channels);
  SyncBuffer sync_buffer(channels, 1000);
  RandomVector random_vector;
  StatisticsCalculator statistics;
  MockExpand expand(&bgn, &sync_buffer, &random_vector, &statistics, fs,
                    channels);
  Normal normal(fs, &db, bgn, &expand, &statistics);

  int16_t input[1000] = {0};
  AudioMultiVector output(channels);

  // Let the number of samples be one sample less than 80 samples per channel.
  size_t input_len = 80 * channels - 1;
  EXPECT_EQ(0, normal.Process(input, input_len, NetEq::Mode::kExpand, &output));
  EXPECT_EQ(0u, output.Size());

  EXPECT_CALL(db, Die());      // Called when `db` goes out of scope.
  EXPECT_CALL(expand, Die());  // Called when `expand` goes out of scope.
}

TEST(Normal, LastModeExpand120msPacket) {
  MockDecoderDatabase db;
  const int kFs = 48000;
  const size_t kPacketsizeBytes = 11520u;
  const size_t kChannels = 1;
  BackgroundNoise bgn(kChannels);
  SyncBuffer sync_buffer(kChannels, 1000);
  RandomVector random_vector;
  StatisticsCalculator statistics;
  MockExpand expand(&bgn, &sync_buffer, &random_vector, &statistics, kFs,
                    kChannels);
  Normal normal(kFs, &db, bgn, &expand, &statistics);

  int16_t input[kPacketsizeBytes] = {0};
  AudioMultiVector output(kChannels);

  EXPECT_CALL(expand, SetParametersForNormalAfterExpand());
  EXPECT_CALL(expand, Process(_)).WillOnce(Invoke(ExpandProcess120ms));
  EXPECT_CALL(expand, Reset());
  EXPECT_EQ(
      static_cast<int>(kPacketsizeBytes),
      normal.Process(input, kPacketsizeBytes, NetEq::Mode::kExpand, &output));

  EXPECT_EQ(kPacketsizeBytes, output.Size());

  EXPECT_CALL(db, Die());      // Called when `db` goes out of scope.
  EXPECT_CALL(expand, Die());  // Called when `expand` goes out of scope.
}

TEST(Normal, ShortFramesAfterComfortNoise) {
  for (int fs : {8000, 16000, 32000, 48000}) {
    for (size_t length : {1, 2, 10, 47, 48, 480}) {
      MockDecoderDatabase db;
      BackgroundNoise bgn(1);
      Normal normal(fs, &db, bgn, nullptr, nullptr);
      AudioMultiVector output(1);
      std::vector<int16_t> input(length, 1000);
      EXPECT_CALL(db, GetActiveCngDecoder()).WillOnce(::testing::Return(nullptr));
      EXPECT_EQ(static_cast<int>(length),
                normal.Process(input.data(), length, NetEq::Mode::kRfc3389Cng,
                               &output));
      ASSERT_EQ(length, output.Size());
      for (size_t i = 0; i < length; ++i) {
        EXPECT_EQ(1000, output[0][i]);
      }
      EXPECT_CALL(db, Die());
    }
  }
}

TEST(Normal, ShortExpandedAudio) {
  for (size_t channels : {1, 2}) {
    for (size_t source_length : {0, 1, 2, 10}) {
      MockDecoderDatabase db;
      BackgroundNoise bgn(channels);
      SyncBuffer sync_buffer(channels, 1000);
      RandomVector random_vector;
      StatisticsCalculator statistics;
      MockExpand expand(&bgn, &sync_buffer, &random_vector, &statistics, 48000,
                        channels);
      Normal normal(48000, &db, bgn, &expand, &statistics);
      AudioMultiVector output(channels);
      std::vector<int16_t> input(480 * channels, 1000);
      EXPECT_CALL(expand, SetParametersForNormalAfterExpand());
      EXPECT_CALL(expand, Process(_))
          .WillOnce(Invoke([source_length](AudioMultiVector* expanded) {
            expanded->Zeros(source_length);
            return 0;
          }));
      EXPECT_CALL(expand, Reset());
      EXPECT_EQ(static_cast<int>(input.size()),
                normal.Process(input.data(), input.size(),
                               NetEq::Mode::kExpand, &output));
      ASSERT_EQ(480u, output.Size());
      for (size_t channel = 0; channel < channels; ++channel) {
        EXPECT_EQ(1000, output[channel][479]);
      }
      EXPECT_CALL(db, Die());
      EXPECT_CALL(expand, Die());
    }
  }
}

// TODO(hlundin): Write more tests.

}  // namespace webrtc
