/*
 * ALAC decoder wrapper: the interface rtp.c uses, implemented over Apple's reference decoder.
 * (c) 2026 Dmytro Skorin, MIT License.
 */
#pragma once

#include <stdbool.h>

struct alac_codec_s;

#ifdef __cplusplus
extern "C" {
#endif

// magic_cookie is the 24-byte ALACSpecificConfig rtp.c assembles from the SDP fmtp line.
struct alac_codec_s *alac_create_decoder(int magic_cookie_size, unsigned char *magic_cookie,
                                         unsigned char *sample_size, unsigned *sample_rate,
                                         unsigned char *channels, unsigned int *block_size);
void alac_delete_decoder(struct alac_codec_s *codec);
// Decodes one packet into interleaved 16-bit PCM; out_frames receives the frame count.
bool alac_to_pcm(struct alac_codec_s *codec, unsigned char *input, unsigned char *output, char channels,
                 unsigned *out_frames);

#ifdef __cplusplus
}
#endif
