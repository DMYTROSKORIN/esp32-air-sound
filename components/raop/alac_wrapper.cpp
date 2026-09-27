#include "alac_wrapper.h"

#include <cstdlib>
#include <cstring>
#include <new>
#include <arpa/inet.h>
#include "ALACBitUtilities.h"
#include "ALACDecoder.h"
#include "esp_log.h"

struct alac_codec_s {
    ALACDecoder *decoder;
    unsigned block_size;
    unsigned channels;
};

extern "C" struct alac_codec_s *alac_create_decoder(int magic_cookie_size, unsigned char *magic_cookie,
                                                    unsigned char *sample_size, unsigned *sample_rate,
                                                    unsigned char *channels, unsigned int *block_size)
{
    auto *codec = static_cast<alac_codec_s *>(calloc(1, sizeof(alac_codec_s)));
    if (!codec) return nullptr;
    codec->decoder = new (std::nothrow) ALACDecoder();
    int32_t st = codec->decoder ? codec->decoder->Init(magic_cookie, magic_cookie_size) : -1;
    if (st != ALAC_noErr) {
        ESP_LOGE("alac", "Init failed: %ld (cookie %d bytes, frameLength raw 0x%08lx)", (long)st, magic_cookie_size,
                 (unsigned long)*(uint32_t *)magic_cookie);
        delete codec->decoder;
        free(codec);
        return nullptr;
    }
    const ALACSpecificConfig &cfg = codec->decoder->mConfig;
    codec->block_size = cfg.frameLength;
    codec->channels = cfg.numChannels;
    if (sample_size) *sample_size = cfg.bitDepth;
    if (sample_rate) *sample_rate = cfg.sampleRate;
    if (channels) *channels = cfg.numChannels;
    if (block_size) *block_size = cfg.frameLength;
    return codec;
}

extern "C" void alac_delete_decoder(struct alac_codec_s *codec)
{
    if (!codec) return;
    delete codec->decoder;
    free(codec);
}

extern "C" bool alac_to_pcm(struct alac_codec_s *codec, unsigned char *input, unsigned char *output, char channels,
                            unsigned *out_frames)
{
    BitBuffer bits;
    // The caller's buffer holds one packet; its length is not passed, so the bit reader is given
    // the largest a frame can legally be (the decoder stops at the end-of-frame tag).
    BitBufferInit(&bits, input, codec->block_size * codec->channels * 4 + 64);
    uint32_t frames = 0;
    int32_t err = codec->decoder->Decode(&bits, output, codec->block_size, channels, &frames);
    if (out_frames) *out_frames = frames;
    return err == ALAC_noErr;
}
