// send_from_file.c
/*
* 사용 시: send_audio_file_to_appsrc("sample.pcm", appsrc, "PCM");
*/
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

#define BUFFER_SIZE 4096

void send_audio_file_to_appsrc(const char *filename, GstElement *appsrc, const char *format) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        g_printerr("파일 열기 실패: %s\n", filename);
        return;
    }

    guint8 buffer_data[BUFFER_SIZE];
    GstBuffer *buffer;
    GstFlowReturn ret;
    size_t bytes_read;
    GstClockTime timestamp = 0;
    int count = 0;

    while ((bytes_read = fread(buffer_data, 1, BUFFER_SIZE, fp)) > 0) {
        buffer = gst_buffer_new_wrapped(g_memdup(buffer_data, bytes_read), bytes_read);
        GST_BUFFER_PTS(buffer) = timestamp;
        GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale(bytes_read, GST_SECOND, 48000 * 2 * 2);
        timestamp += GST_BUFFER_DURATION(buffer);

        g_signal_emit_by_name(appsrc, "push-buffer", buffer, &ret);
        gst_buffer_unref(buffer);
        count++;

        g_usleep(10000); // 10ms

        if (count % 100 == 0) {
            g_print("  [FILE->appsrc] %d번째 버퍼 전송됨\n", count);
        }
    }

    fclose(fp);
    g_print("📁 파일 전송 완료 (%d개 버퍼)\n", count);
}
