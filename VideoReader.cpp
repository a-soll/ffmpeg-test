#include "VideoReader.h"
#include <CoreVideo/CoreVideo.h>
#include <iostream>

static void print_dimensions(AVStream *stream) {
    std::cout << '(' << stream->codecpar->width << ", " << stream->codecpar->height
              << ")\n";
}

void VideoReader::_create_pixel_buffer() {
    CVPixelBufferRef pixel_buffer =
        reinterpret_cast<CVPixelBufferRef>(this->_frame->data[3]);
    CMVideoFormatDescriptionRef desc = nullptr;
    CMVideoFormatDescriptionCreateForImageBuffer(
        kCFAllocatorDefault, pixel_buffer, &desc
    );
    CMTime pts = CMTimeMake(
        this->_frame->pts * this->_stream->time_base.num, this->_stream->time_base.den
    );

    double frame_duration_seconds = this->_frame->duration *
                                    av_q2d(this->_stream->time_base);
    CMSampleTimingInfo timing;
    timing.duration        = CMTimeMakeWithSeconds(frame_duration_seconds, 1000000000);
    timing.decodeTimeStamp = kCMTimeInvalid;

    CMSampleBufferRef buffer;
    CMSampleBufferCreateForImageBuffer(
        kCFAllocatorDefault, pixel_buffer, true, nullptr, nullptr, desc, &timing, &buffer
    );
    this->_callback(buffer);
    CFRelease(desc);
}

/**
 * setup video parameters
 */
void VideoReader::_prepare_video_stream() {
    const AVCodec *codec = avcodec_find_decoder(this->_stream->codecpar->codec_id);
    if (!codec) {
        std::cerr << "No decoder for codec\n";
        return;
    }
    if (!this->_codec_ctx) {
        this->_codec_ctx                = avcodec_alloc_context3(codec);
        this->_codec_ctx->hw_device_ctx = this->_hw_device_ctx;

        /**
         * set the codec format to VideoToolbox for hardware acceleration
         */
        this->_codec_ctx->get_format = [
        ](AVCodecContext * ctx, const enum AVPixelFormat *pix_fmt) -> enum AVPixelFormat {
            for (const enum AVPixelFormat *p = pix_fmt; *p != -1; ++p) {
                if (*p == AV_PIX_FMT_VIDEOTOOLBOX) {
                    return AV_PIX_FMT_VIDEOTOOLBOX;
                }
            }
            return pix_fmt[0];
        };
    }
    avcodec_parameters_to_context(this->_codec_ctx, this->_stream->codecpar);
    if (avcodec_open2(this->_codec_ctx, codec, nullptr) < 0) {
        std::cerr << "Failed to open codec\n";
    }
}

void VideoReader::_prepare_audio_stream() {
    const AVCodec *codec = avcodec_find_decoder(this->_stream->codecpar->codec_id);
    if (!codec) {
        std::cerr << "No audio decoder found\n";
        return;
    }
    if (!this->_audio_ctx) {
        this->_audio_ctx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(this->_audio_ctx, this->_stream->codecpar);
        if (avcodec_open2(this->_audio_ctx, codec, nullptr) < 0) {
            std::cerr << "Failed to open audio codec\n";
            avcodec_free_context(&this->_audio_ctx);
            return;
        }
    }
}

void VideoReader::_send_audio_packet() {
    int ret = avcodec_send_packet(this->_audio_ctx, this->_packet);

    if (ret < 0) {
        std::cerr << "Error sending audio packet\n";
    }
    AVChannelLayout out_ch_layout;
    av_channel_layout_default(&out_ch_layout, this->_audio_ctx->ch_layout.nb_channels);
    AVSampleFormat out_fmt = AV_SAMPLE_FMT_FLT;
    while (ret >= 0) {
        ret = avcodec_receive_frame(this->_audio_ctx, this->_frame);

        if (!this->_swr_ctx) {

            swr_alloc_set_opts2(
                &this->_swr_ctx,
                &out_ch_layout,
                out_fmt,
                this->_audio_ctx->sample_rate,
                &this->_audio_ctx->ch_layout,
                this->_audio_ctx->sample_fmt,
                this->_audio_ctx->sample_rate,
                0,
                nullptr
            );
            swr_init(this->_swr_ctx);
        }

        AudioStreamBasicDescription asbd;
        asbd.mSampleRate       = this->_audio_ctx->sample_rate;
        asbd.mFormatID         = kAudioFormatLinearPCM;
        asbd.mFormatFlags      = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        asbd.mChannelsPerFrame = out_ch_layout.nb_channels;
        asbd.mBytesPerFrame    = sizeof(float) * asbd.mChannelsPerFrame;
        asbd.mBytesPerPacket   = asbd.mBytesPerFrame;
        asbd.mFramesPerPacket  = 1;
        asbd.mBitsPerChannel   = 32;
        asbd.mReserved         = 0;

        uint8_t **output       = nullptr;
        int output_linesize    = 0;
        av_samples_alloc_array_and_samples(
            &output,
            &output_linesize,
            out_ch_layout.nb_channels,
            this->_frame->nb_samples,
            out_fmt,
            0
        );

        int converted_samples = swr_convert(
            this->_swr_ctx,
            &output[0],
            this->_frame->nb_samples,
            this->_frame->extended_data,
            this->_frame->nb_samples
        );
        if (converted_samples <= 0) {
            std::cout << "No converted audio samples\n";
            return;
        }

        CMAudioFormatDescriptionRef audio_format = nullptr;
        OSStatus status                          = CMAudioFormatDescriptionCreate(
            kCFAllocatorDefault, &asbd, 0, nullptr, 0, nullptr, nullptr, &audio_format
        );

        CMBlockBufferRef block_buffer = nullptr;
        CMBlockBufferCustomBlockSource custom_source;
        custom_source.version   = 0;
        custom_source.FreeBlock = [](void *refcon, void *memory_block, size_t size) {
            uint8_t **output = (uint8_t **)refcon;
            av_freep(&output[0]);
            av_freep(&output);
        };
        custom_source.refCon = output;

        status               = CMBlockBufferCreateWithMemoryBlock(
            kCFAllocatorDefault,
            output[0],
            converted_samples * asbd.mBytesPerFrame,
            kCFAllocatorDefault,
            &custom_source,
            0,
            converted_samples * asbd.mBytesPerFrame,
            0,
            &block_buffer
        );

        CMTime pts = CMTimeMake(this->_frame->pts, this->_audio_ctx->sample_rate);
        CMSampleTimingInfo timing;
        timing.duration                = CMTimeMake(converted_samples, asbd.mSampleRate);
        timing.decodeTimeStamp         = kCMTimeInvalid;
        timing.presentationTimeStamp   = pts;

        CMSampleBufferRef audio_buffer = nullptr;
        CMAudioSampleBufferCreateReadyWithPacketDescriptions(
            kCFAllocatorDefault,
            block_buffer,
            audio_format,
            converted_samples,
            pts,
            nullptr,
            &audio_buffer
        );
        this->_callback(audio_buffer);
        CFRelease(block_buffer);
        CFRelease(audio_format);
    }
}

void VideoReader::_send_video_packet() {
    int ret = avcodec_send_packet(this->_codec_ctx, this->_packet);
    if (ret < 0) {
        std::cerr << "Error sending video packet\n";
        return;
    }

    if (this->_frame->flags & AV_FRAME_FLAG_CORRUPT) {
        std::cout << "Frame is corrupt\n";
    }

    while (ret >= 0) {
        ret = avcodec_receive_frame(this->_codec_ctx, this->_frame);

        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            if (ret == AVERROR_EOF) {
                std::cout << "Stream ended\n";
                exit(1);
            }
            return;
        } else if (ret < 0) {
            std::cerr << "Other misc error on video frame\n";
            return;
        }

        /**
         * this can happen if a stream is playing ads
         * and swaps back to normal stream.
         *
         * need to normalize pts, but for now exit.
         */
        if (this->_frame->pts < this->_last_pts) {
            std::cout << "Ads ended?\n";
            exit(1);
        }
        this->_last_pts      = this->_frame->pts;
        AVPixelFormat format = static_cast<AVPixelFormat>(this->_frame->format);
        if (format == AV_PIX_FMT_VIDEOTOOLBOX) {
            this->_create_pixel_buffer();
        } else {
            std::cout << "Format: " << format << '\n';
        }
    }
}

void VideoReader::start(const char *url, reader_callback callback) {
    this->_callback = callback;
    avformat_network_init();
    avformat_open_input(&this->_format_ctx, url, this->_input_format, nullptr);

    this->_alloc_items();

    if (avformat_find_stream_info(this->_format_ctx, nullptr) < 0) {
        std::cerr << "Could not find stream info\n";
    }

    for (int i = 0; i < this->_format_ctx->nb_streams; i++) {
        this->_stream = this->_format_ctx->streams[i];
        if (this->_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            this->_video_stream_index = i;
            print_dimensions(this->_stream);
            this->_prepare_video_stream();
        } else if (this->_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            this->_audio_stream_index = i;
            this->_prepare_audio_stream();
        }
    }

    /**
     * main stream loop
     */
    while (!this->_stop) {
        int ret = av_read_frame(this->_format_ctx, this->_packet);

        if (this->_packet->stream_index == this->_video_stream_index) {
            this->_send_video_packet();
        } else if (this->_packet->stream_index == this->_audio_stream_index) {
            this->_send_audio_packet();
        }

        av_packet_unref(this->_packet);

        if (ret < 0) {
            std::cerr << "Failed to open input\n";
            return;
        }
    }
}
