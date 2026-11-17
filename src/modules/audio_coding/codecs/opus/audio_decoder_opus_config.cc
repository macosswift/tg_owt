#include "modules/audio_coding/codecs/opus/audio_decoder_opus.h"
#include "modules/audio_coding/codecs/opus/opus_inst.h"
#include "rtc_base/checks.h"

namespace webrtc {

AudioDecoderOpusImpl::AudioDecoderOpusImpl(size_t num_channels,
                                         int sample_rate_hz,
                                         const FieldTrialsView& field_trials)
    : channels_(num_channels),
      sample_rate_hz_(sample_rate_hz),
      generate_plc_(field_trials.IsEnabled("WebRTC-Audio-OpusGeneratePlc")) {
  RTC_DCHECK(num_channels == 1 || num_channels == 2);
  RTC_DCHECK(sample_rate_hz == 16000 || sample_rate_hz == 48000);
  const int error = WebRtcOpus_DecoderCreate(&dec_state_, channels_, sample_rate_hz_);
  RTC_DCHECK(error == 0);
  dec_state_->plc_use_prev_decoded_samples =
      field_trials.IsEnabled("WebRTC-Audio-OpusPlcUsePrevDecodedSamples");
  dec_state_->prev_decoded_samples = sample_rate_hz_ / 50;
  WebRtcOpus_DecoderInit(dec_state_);
}

}
