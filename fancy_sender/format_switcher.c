// format_switcher.c

#include <gst/gst.h>

// 외부 pipeline 접근을 위한 전역 포인터
extern GstElement *pipeline;

// 현재 연결된 bin 추적
static GstElement *current_bin = NULL;

// 외부 생성 함수
GstElement* create_pcm_pipeline_bin(GstElement **out_sink);
GstElement* create_ac3_pipeline_bin(GstElement **out_sink);

static void replace_bin(GstElement *appsrc, GstElement *new_bin) {
    // 파이프라인 일시 정지
    gst_element_set_state(pipeline, GST_STATE_PAUSED);

    if (current_bin) {
        gst_bin_remove(GST_BIN(pipeline), current_bin);
        gst_element_set_state(current_bin, GST_STATE_NULL);
        gst_object_unref(current_bin);
        current_bin = NULL;
    }

    // 새로운 bin 추가 및 연결
    gst_bin_add(GST_BIN(pipeline), new_bin);
    gst_element_sync_state_with_parent(new_bin);

    // appsrc → new_bin 연결
    if (!gst_element_link(appsrc, new_bin)) {
        g_printerr("appsrc와 새 pipeline 연결 실패\n");
    }

    current_bin = new_bin;

    // 전체 pipeline 재생
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
}

// 외부에서 호출하는 포맷 전환 함수들
void switch_to_pcm_pipeline(GstElement *appsrc) {
    g_print("[FORMAT_SWITCHER] PCM pipeline 생성 중...\n");
    GstElement *pcm_bin = create_pcm_pipeline_bin(NULL);
    replace_bin(appsrc, pcm_bin);
}

void switch_to_ac3_pipeline(GstElement *appsrc) {
    g_print("[FORMAT_SWITCHER] AC3 pipeline 생성 중...\n");
    GstElement *ac3_bin = create_ac3_pipeline_bin(NULL);
    replace_bin(appsrc, ac3_bin);
}
