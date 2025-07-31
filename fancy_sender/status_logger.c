// status_logger.c
/*
 *🔧 사용 방법:
 *
 *log_format("PCM") or log_format("AC3")
 *
 *log_buffer(buffer) — appsrc에 push 전 출력
 */
#include <gst/gst.h>
#include <stdio.h>
#include <time.h>

void log_format(const char *format_name) {
    time_t now = time(NULL);
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    g_print("[FORMAT SWITCHED] %s -> %s\n", timebuf, format_name);
}

void log_buffer(GstBuffer *buffer) {
    GstClockTime pts = GST_BUFFER_PTS(buffer);
    g_print("  > PTS: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(pts));
}
