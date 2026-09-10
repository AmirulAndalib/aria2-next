/* Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later. */
#ifndef D_MEDIA_MUXER_H
#define D_MEDIA_MUXER_H
#include "MediaStore.h"
#include <memory>
#include <string>
#include <vector>
namespace aria2 {
namespace media {
class Muxer {
public:
  static void write(const std::vector<Segment>& segments,
                    const std::string& output, const std::string& directory,
                    const std::string& format, bool video, bool audio,
                    bool subtitles, bool overwrite,
                    const std::shared_ptr<Control>& control);
};
} // namespace media
} // namespace aria2
#endif
