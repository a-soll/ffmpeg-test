#ifndef VIDEOREADER_H
#define VIDEOREADER_H

#include <CoreMedia/CoreMedia.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

class VideoReader {
public:
    VideoReader() {}

    ~VideoReader() {
        this->_teardown();
    }

    void start(const char *url);

    /**
     * buffer that can be handed to AVSampleBufferDisplayLayer
     */
    CMSampleBufferRef buffer = nullptr;

private:
    void _handle_video_stream();
    void _handle_audio_stream();
    void _send_video_packet();
    void _create_pixel_buffer();

    inline void _teardown() {
        av_frame_free(&this->_frame);
        av_packet_free(&this->_packet);
        avformat_close_input(&this->_format_ctx);
        avformat_free_context(this->_format_ctx);
        avformat_network_deinit();
        if (this->_codec_ctx) {
            avcodec_free_context(&this->_codec_ctx);
        }
        CFRelease(buffer);
    }

    inline void _alloc_items() {
        if (!this->_packet) {
            this->_packet = av_packet_alloc();
        }
        if (!this->_frame) {
            this->_frame = av_frame_alloc();
        }
        int ret = av_hwdevice_ctx_create(
            &this->_hw_device_ctx, AV_HWDEVICE_TYPE_VIDEOTOOLBOX, NULL, NULL, 0
        );
    }

    AVBufferRef *_hw_device_ctx  = nullptr;
    int _video_stream_index      = -1;
    int _audio_stream_index      = -1;
    int64_t _last_pts            = 0;
    AVPacket *_packet            = nullptr;
    AVFrame *_frame              = nullptr;
    AVStream *_stream            = nullptr;
    AVFormatContext *_format_ctx = nullptr;
    AVInputFormat *_input_format = nullptr;
    AVCodecContext *_codec_ctx   = nullptr;
    bool _stop                   = false;
    int _num_processed           = 0;
};

#endif /* VIDEOREADER_H */
