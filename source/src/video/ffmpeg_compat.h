#pragma once

// 1. Essential C linkage wrap for standard C++ compilation compatibility
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
}

// 2. Public Portability Bridge: Translates legacy/modern API shifts across platforms
#ifndef av_packet_unref
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(57, 7, 0)
#define av_packet_unref av_free_packet
#endif
#endif

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(55, 28, 1)
#define av_frame_alloc avcodec_alloc_frame
#define av_frame_free avcodec_free_frame
#endif

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(55, 52, 0)
#define avcodec_free_context av_freep
#endif
