// gst_sender.c

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <string.h>

// forward declarations
void switch_to_pcm_pipeline(GstElement *appsrc);
void switch_to_ac3_pipeline(GstElement *appsrc);

static GMainLoop *main_loop;
static GstElement *pipeline, *appsrc, *typefind, *fakesink;

static gboolean first_format_detected = FALSE;

// 📌 typefind 콜백
static void on_have_type(GstElement *src, guint prob, GstCaps *caps, gpointer user_data) {
    if (first_format_detected) return; // 첫 포맷 감지 후 무시
    first_format_detected = TRUE;

    gchar *type = gst_caps_to_string(caps);
    g_print("[TYPEFIND] 감지된 포맷: %s\n", type);

    if (g_str_has_prefix(type, "audio/ac3")) {
        g_print("[SWITCH] AC3 pipeline으로 전환합니다.\n");
        switch_to_ac3_pipeline(appsrc);
    } else {
        g_print("[SWITCH] PCM pipeline으로 전환합니다.\n");
        switch_to_pcm_pipeline(appsrc);
    }

    g_free(type);
}

// 📌 테스트용 데이터 푸시 타이머 (초기에는 0.1초에 한번씩 push)
static gboolean feed_dummy_data(gpointer data) {
    static int counter = 0;
    guint size = 48000 * 2 * 2 / 10; // 0.1초 분량

    GstBuffer *buffer;
    guint8 *raw_data = g_malloc0(size);
    GstFlowReturn ret;

    // 샘플값 (AC3 패턴 넣기: 0x0B77)
    if (counter >= 20 && counter < 40) {
        raw_data[0] = 0x0B;
        raw_data[1] = 0x77;
    }

    buffer = gst_buffer_new_wrapped(g_memdup(raw_data, size), size);
    g_signal_emit_by_name(appsrc, "push-buffer", buffer, &ret);
    gst_buffer_unref(buffer);
    g_free(raw_data);

    counter++;
    return TRUE;
}

int main(int argc, char *argv[]) {
    gst_init(&argc, &argv);
    main_loop = g_main_loop_new(NULL, FALSE);

    pipeline = gst_pipeline_new("detect-pipeline");
    appsrc = gst_element_factory_make("appsrc", "mysrc");
    typefind = gst_element_factory_make("typefind", "typefinder");
    fakesink = gst_element_factory_make("fakesink", "fakesink");

    if (!pipeline || !appsrc || !typefind || !fakesink) {
        g_printerr("요소 생성 실패\n");
        return -1;
    }

    // appsrc 설정
    g_object_set(appsrc,
        "format", GST_FORMAT_TIME,
        "is-live", TRUE,
        "block", TRUE,
        NULL);

    // typefind 시그널 연결
    g_signal_connect(typefind, "have-type", G_CALLBACK(on_have_type), NULL);

    // 파이프라인 구성
    gst_bin_add_many(GST_BIN(pipeline), appsrc, typefind, fakesink, NULL);
    if (!gst_element_link_many(appsrc, typefind, fakesink, NULL)) {
        g_printerr("파이프라인 연결 실패\n");
        return -1;
    }

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    g_timeout_add(100, feed_dummy_data, NULL);

    g_main_loop_run(main_loop);

    // 정리
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    g_main_loop_unref(main_loop);
    return 0;
}
