#include "VideoReader.h"

/**
 * insert your stream url here.
 */
const char *url      = "";

int frames_processed = 0;
int stop_at          = 100;

int main() {
    VideoReader reader;
    reader.start(url, [&reader](CMSampleBufferRef buffer) {
        CFShow(buffer);
        if (buffer) {
            CFRelease(buffer);
        }
        frames_processed++;
        if (frames_processed == stop_at) {
            reader.stop();
        }
    });
    return 0;
}
