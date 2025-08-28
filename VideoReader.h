#ifndef VIDEOREADER_H
#define VIDEOREADER_H

#include <CoreMedia/CoreMedia.h>
#include <functional>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

class VideoReader {
public:
    using reader_callback = std::function<void(CMSampleBufferRef)>;

    VideoReader() {}

    ~VideoReader() {
        this->_teardown();
    }

    void start(const char *url, reader_callback callback);

    inline void stop() {
        this->_stop = true;
    }

private:
    void _prepare_video_stream();
    void _prepare_audio_stream();
    void _send_audio_packet();
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
        if (this->_audio_ctx) {
            avcodec_free_context(&this->_audio_ctx);
        }
        sws_freeContext(this->_sws_ctx);
        swr_free(&this->_swr_ctx);
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
    AVCodecContext *_audio_ctx   = nullptr;
    struct SwsContext *_sws_ctx  = nullptr;
    struct SwrContext *_swr_ctx  = nullptr;
    bool _stop                   = false;
    reader_callback _callback;
};

#endif /* VIDEOREADER_H */
