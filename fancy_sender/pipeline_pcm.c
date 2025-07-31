// pipeline_pcm.c

#include <gst/gst.h>

#define UDP_PORT 5000

// PCM용 bin 생성 함수
GstElement* create_pcm_pipeline_bin(GstElement **out_sink) {
    GstElement *bin, *convert, *resample, *encoder, *pay, *sink;
    GstPad *ghost_pad;

    bin = gst_bin_new("pcm_bin");

    convert = gst_element_factory_make("audioconvert", NULL);
    resample = gst_element_factory_make("audioresample", NULL);
    encoder = gst_element_factory_make("opusenc", NULL);
    pay = gst_element_factory_make("rtpopuspay", NULL);
    sink = gst_element_factory_make("udpsink", NULL);

    if (!convert || !resample || !encoder || !pay || !sink) {
        g_printerr("PCM pipeline 요소 생성 실패\n");
        return NULL;
    }

    // udpsink 설정
    g_object_set(sink,
                 "host", "127.0.0.1",
                 "port", UDP_PORT,
                 "sync", FALSE,
                 "async", FALSE,
                 NULL);

    // bin에 요소 추가 및 연결
    gst_bin_add_many(GST_BIN(bin), convert, resample, encoder, pay, sink, NULL);
    if (!gst_element_link_many(convert, resample, encoder, pay, sink, NULL)) {
        g_printerr("PCM 요소 연결 실패\n");
        return NULL;
    }

    // ghost pad 생성 (appsrc가 bin을 통해 연결될 수 있도록)
    GstPad *pad = gst_element_get_static_pad(convert, "sink");
    ghost_pad = gst_ghost_pad_new("sink", pad);
    gst_element_add_pad(bin, ghost_pad);
    gst_object_unref(pad);

    if (out_sink) *out_sink = sink;
    return bin;
}
