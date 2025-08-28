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
    if (pixel_buffer) {
        CFShow(pixel_buffer);
    }
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
    timing.duration = CMTimeMakeWithSeconds(frame_duration_seconds, 1000000000);

    CMSampleBufferCreateForImageBuffer(
        kCFAllocatorDefault,
        pixel_buffer,
        true,
        nullptr,
        nullptr,
        desc,
        &timing,
        &this->buffer
    );
    CFRelease(desc);
}

/**
 * setup video parameters
 */
void VideoReader::_handle_video_stream() {
    AVCodec *codec =
        const_cast<AVCodec *>(avcodec_find_decoder(this->_stream->codecpar->codec_id));
    if (!codec) {
        std::cerr << "No decoder for codec\n";
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

void VideoReader::_send_video_packet() {
    int ret = avcodec_send_packet(this->_codec_ctx, this->_packet);
    if (ret < 0) {
        std::cerr << "Error sending packet\n";
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
            exit(1);
        }
        this->_last_pts      = this->_frame->pts;
        AVPixelFormat format = static_cast<AVPixelFormat>(this->_frame->format);
        if (format == AV_PIX_FMT_VIDEOTOOLBOX) {
            std::cout << "Format is VideoToolbox\n";
        } else {
            std::cout << "Format: " << format << '\n';
        }
    }
}

void VideoReader::start(const char *url) {
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
            this->_handle_video_stream();
        } else if (this->_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            this->_audio_stream_index = i;
        }
    }

    /**
     * main stream loop
     */
    while (!this->_stop) {
        int ret = av_read_frame(this->_format_ctx, this->_packet);

        if (this->_packet->stream_index == this->_video_stream_index) {
            this->_send_video_packet();
        }

        if (ret < 0) {
            std::cerr << "Failed to open input\n";
            return;
        }
        this->_num_processed++;
        if (this->_num_processed == 20) {
            this->_stop = true;
        }
    }
}
