#include "VideoReader.h"

/**
 * insert your stream url here.
 */
const char *url = "";

int main() {
    VideoReader reader;
    reader.start(url);

    return 0;
}
