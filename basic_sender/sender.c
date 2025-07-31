/*
 * gst_sender_gemini.c
 * build : sender % gcc gst_sender.c -o gst_sender `pkg-config --cflags --libs gstreamer-1.0 gstreamer-base-1.0 gstreamer-app-1.0 glib-2.0` -lm
 * PCM test : gst-launch-1.0 udpsrc port=5000 ! "application/x-rtp,media=(string)audio,clock-rate=(int)48000,encoding-name=(string)L16,encoding-params=(string)2,channels=(int)2,payload=(int)96" ! rtpL16depay ! audioconvert ! autoaudiosink
 * AC3 test : gst-launch-1.0 udpsrc port=5000 ! "application/x-rtp,media=(string)audio,clock-rate=(int)48000,encoding-name=(string)AC3" ! rtpac3depay ! ac3parse ! avdec_ac3 ! audioconvert ! autoaudiosink
*/


#include <gst/gst.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <math.h> // For sin() in PCM data generation for demonstration

// REQUIRED for appsrc functions and macros
#include <gst/app/gstappsrc.h>

// Global GStreamer elements and state variables
GstElement *pipeline = NULL;      // The main GStreamer pipeline
GstElement *appsrc = NULL;        // Source element to push data into the pipeline
GstElement *audioconvert = NULL;  // Converts audio formats (e.g., S16LE to float)
GstElement *audioresample = NULL; // Resamples audio to a different rate
GstElement *encoder = NULL;       // Encoder for specific formats (e.g., AC3) or passthrough element
GstElement *rtppayloader = NULL;  // Converts raw audio/encoded data into RTP packets
GstElement *udpsink = NULL;       // Sends data over UDP

GstBus *bus = NULL;               // GStreamer bus for receiving messages
gulong bus_watch_id = 0;          // ID for the bus watch source

// Flag to track the current audio format in the pipeline (TRUE for PCM, FALSE for AC3)
gboolean is_pcm_format = FALSE;

// Global GMainLoop instance so it can be accessed from configure_pipeline
GMainLoop *main_loop = NULL;

// Structure to hold current audio data parameters for generation
typedef struct _CurrentAudioDataParams {
    char format[10];  // "PCM" or "AC3"
    int sample_rate;  // Sample rate (e.g., 48000 Hz)
    int channels;     // Number of audio channels (e.g., 2 for stereo)
    int depth;        // Bit depth (e.g., 16 for S16LE)
} CurrentAudioDataParams;

CurrentAudioDataParams current_audio_data_params; // Global instance to store current format parameters

// --- Function Prototypes ---
static void start_feed(GstElement *appsrc, guint size, gpointer user_data);
static void stop_feed(GstElement *appsrc, gpointer user_data);
static void configure_pipeline(const char *audio_format);
// Removed 'static' keyword from declaration
void handle_audio_format_change(const char *new_format); 
static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data);
static gboolean simulate_audio_data_feed(gpointer user_data); // Renamed for clarity


/**
 * @brief Configures (creates or re-creates) the GStreamer pipeline based on the audio format.
 * This is the core logic for dynamic pipeline switching.
 *
 * @param audio_format The desired audio format for the pipeline ("PCM" or "AC3").
 */
static void configure_pipeline(const char *audio_format) {
    g_print("Configuring pipeline for %s format.\n", audio_format);

    // If a pipeline already exists, set it to NULL state and unreference it
    // This effectively tears down the old pipeline and its elements.
    if (pipeline) {
        g_print("Cleaning up existing pipeline...\n");
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        pipeline = NULL; // Reset global pointers to NULL
        appsrc = NULL;
        audioconvert = NULL;
        audioresample = NULL;
        encoder = NULL;
        rtppayloader = NULL;
        udpsink = NULL;
        // Remove the existing bus watch to prevent callbacks on the old pipeline
        if (bus_watch_id) {
            g_source_remove(bus_watch_id);
            bus_watch_id = 0;
        }
    }

    // Create a new GStreamer pipeline
    pipeline = gst_pipeline_new("audio-sender-pipeline");
    if (!pipeline) {
        g_printerr("Failed to create pipeline.\n");
        goto error_exit; // Jump to error handling if pipeline creation fails
    }

    // Create the appsrc element
    appsrc = gst_element_factory_make("appsrc", "my-appsrc");
    if (!appsrc) {
        g_printerr("Failed to create appsrc element.\n");
        goto error_exit;
    }
    // Configure appsrc properties for live streaming
    g_object_set(G_OBJECT(appsrc), "is-live", TRUE, NULL);       // Treat as a live source
    g_object_set(G_OBJECT(appsrc), "format", GST_FORMAT_TIME, NULL); // Use time-based format
    g_object_set(G_OBJECT(appsrc), "do-timestamp", TRUE, NULL);  // Automatically add timestamps to buffers

    // Connect "need-data" and "enough-data" signals to appsrc
    g_signal_connect(appsrc, "need-data", G_CALLBACK(start_feed), NULL);
    g_signal_connect(appsrc, "enough-data", G_CALLBACK(stop_feed), NULL);

    // Create the udpsink element
    udpsink = gst_element_factory_make("udpsink", "my-udpsink");
    if (!udpsink) {
        g_printerr("Failed to create udpsink element.\n");
        goto error_exit;
    }
    // Configure udpsink to send to localhost on port 5000
    g_object_set(G_OBJECT(udpsink), "host", "127.0.0.1", "port", 5000, NULL);

    // Dynamically create and link elements based on the audio format
    if (g_strcmp0(audio_format, "PCM") == 0) {
        is_pcm_format = TRUE; // Update global format flag
        g_print("Configuring PCM pipeline: appsrc -> audioconvert -> audioresample -> rtpL16pay -> udpsink\n");

        // Set PCM CAPS on appsrc
        GstCaps *appsrc_caps = gst_caps_new_simple("audio/x-raw",
                                          "format", G_TYPE_STRING, "S16LE",
                                          "rate", G_TYPE_INT, current_audio_data_params.sample_rate,
                                          "channels", G_TYPE_INT, current_audio_data_params.channels,
                                          "layout", G_TYPE_STRING, "interleaved",
                                          NULL);
        g_object_set(G_OBJECT(appsrc), "caps", appsrc_caps, NULL);
        gst_caps_unref(appsrc_caps);

        // Create audioconvert (for format conversion if needed)
        audioconvert = gst_element_factory_make("audioconvert", "my-audioconvert");
        if (!audioconvert) { g_printerr("Failed to create audioconvert.\n"); goto error_exit; }
        // Create audioresample (for sample rate conversion if needed)
        audioresample = gst_element_factory_make("audioresample", "my-audioresample");
        if (!audioresample) { g_printerr("Failed to create audioresample.\n"); goto error_exit; }
        // Create rtpL16pay (RTP payload for raw 16-bit linear PCM)
        encoder = gst_element_factory_make("rtpL16pay", "my-rtppay");
        if (!encoder) { g_printerr("Failed to create rtpL16pay.\n"); goto error_exit; }
        rtppayloader = encoder; // rtpL16pay acts as the payloader

        // Add all elements to the pipeline bin
        gst_bin_add_many(GST_BIN(pipeline), appsrc, audioconvert, audioresample, rtppayloader, udpsink, NULL);
        // Link the elements in sequence
        if (!gst_element_link_many(appsrc, audioconvert, audioresample, rtppayloader, udpsink, NULL)) {
            g_printerr("Failed to link PCM pipeline elements.\n");
            goto error_exit;
        }

    } else if (g_strcmp0(audio_format, "AC3") == 0) {
        is_pcm_format = FALSE; // Update global format flag
        g_print("Configuring AC3 pipeline: appsrc -> audioconvert -> audioresample -> avenc_ac3 -> ac3parse -> rtpac3pay -> udpsink\n");

        // For AC3, we'll feed PCM data to appsrc and encode it to AC3
        GstCaps *appsrc_caps = gst_caps_new_simple("audio/x-raw",
                                          "format", G_TYPE_STRING, "S16LE",
                                          "rate", G_TYPE_INT, current_audio_data_params.sample_rate,
                                          "channels", G_TYPE_INT, current_audio_data_params.channels,
                                          "layout", G_TYPE_STRING, "interleaved",
                                          NULL);
        g_object_set(G_OBJECT(appsrc), "caps", appsrc_caps, NULL);
        gst_caps_unref(appsrc_caps);

        // Create audioconvert and audioresample for format preparation
        audioconvert = gst_element_factory_make("audioconvert", "my-audioconvert");
        if (!audioconvert) { g_printerr("Failed to create audioconvert.\n"); goto error_exit; }
        
        audioresample = gst_element_factory_make("audioresample", "my-audioresample");
        if (!audioresample) { g_printerr("Failed to create audioresample.\n"); goto error_exit; }

        // Create AC3 encoder
        GstElement *ac3encoder = gst_element_factory_make("avenc_ac3", "my-ac3encoder");
        if (!ac3encoder) { 
            g_printerr("Failed to create avenc_ac3. Trying lamemp3enc as fallback.\n"); 
            // Fallback to MP3 if AC3 encoder is not available
            ac3encoder = gst_element_factory_make("lamemp3enc", "my-mp3encoder");
            if (!ac3encoder) {
                g_printerr("Failed to create any audio encoder.\n");
                goto error_exit;
            }
        }
        encoder = ac3encoder;

        // Create ac3parse (parses AC3 stream and adds proper headers)
        GstElement *ac3parse = gst_element_factory_make("ac3parse", "my-ac3parse");
        if (!ac3parse) { g_printerr("Failed to create ac3parse.\n"); goto error_exit; }

        // Create rtpac3pay (RTP payload for AC3 encoded audio)
        rtppayloader = gst_element_factory_make("rtpac3pay", "my-rtpac3pay");
        if (!rtppayloader) { 
            g_printerr("Failed to create rtpac3pay. Using rtpmpapay as fallback.\n"); 
            rtppayloader = gst_element_factory_make("rtpmpapay", "my-rtpmpapay");
            if (!rtppayloader) {
                g_printerr("Failed to create RTP payloader.\n");
                goto error_exit;
            }
        }

        // Add elements to the pipeline bin
        gst_bin_add_many(GST_BIN(pipeline), appsrc, audioconvert, audioresample, encoder, ac3parse, rtppayloader, udpsink, NULL);
        
        // Link the elements
        if (!gst_element_link_many(appsrc, audioconvert, audioresample, encoder, ac3parse, rtppayloader, udpsink, NULL)) {
            g_printerr("Failed to link AC3 pipeline elements.\n");
            goto error_exit;
        }
    } else {
        g_printerr("Unsupported audio format: %s\n", audio_format);
        goto error_exit;
    }

    // Set the pipeline to PLAYING state. This makes it ready to process data.
    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr("Failed to set pipeline to PLAYING state.\n");
        goto error_exit;
    } else if (ret == GST_STATE_CHANGE_ASYNC) {
        g_print("Pipeline state change is ASYNC, waiting...\n");
        // Wait for state change to complete
        GstState current_state, pending_state;
        ret = gst_element_get_state(pipeline, &current_state, &pending_state, 2 * GST_SECOND);
        if (ret == GST_STATE_CHANGE_FAILURE) {
            g_printerr("Failed to get pipeline state after ASYNC change.\n");
            goto error_exit;
        }
        g_print("Pipeline state after waiting: %s (pending: %s)\n",
                gst_element_state_get_name(current_state),
                gst_element_state_get_name(pending_state));
    }
    g_print("Pipeline state: PLAYING\n");

    // Get the bus from the pipeline
    bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    if (!bus) {
        g_printerr("Failed to get bus.\n");
        goto error_exit;
    }
    // Add a watch to the bus to receive messages (errors, EOS, state changes)
    bus_watch_id = gst_bus_add_watch(bus, bus_call, main_loop);
    gst_object_unref(bus); // Unreference the bus object after adding the watch
    return; // Successful pipeline configuration

// Error handling label: if any error occurs above, jump here to clean up and exit
error_exit:
    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL); // Set to NULL state to release resources
        gst_object_unref(pipeline); // Unreference the pipeline
        pipeline = NULL; // Reset global pipeline pointer
    }
    g_printerr("An error occurred during pipeline configuration. Exiting application.\n");
    // Quit the main loop if an error occurred during pipeline configuration
    if (main_loop) {
        g_main_loop_quit(main_loop);
    }
}


/**
 * @brief Callback for appsrc's "need-data" signal.
 * This function generates and pushes audio data to appsrc when it needs more.
 *
 * @param appsrc The GstAppSrc element.
 * @param size The amount of data (in bytes) appsrc ideally needs.
 * @param user_data User data (not used in this case, but could be for context).
 */
static void start_feed(GstElement *appsrc, guint size, gpointer user_data) {
    GstBuffer *buffer;
    GstFlowReturn ret;
    guint8 *data_buffer;
    guint data_size;

    // Generate PCM data for both PCM and AC3 formats
    // For AC3, the PCM data will be encoded by avenc_ac3 in the pipeline
    
    // Generate 0.1 seconds of PCM data
    data_size = current_audio_data_params.sample_rate * current_audio_data_params.channels * (current_audio_data_params.depth / 8) / 10;
    data_buffer = g_malloc(data_size);
    if (!data_buffer) {
        g_printerr("Memory allocation failed for PCM data in start_feed.\n");
        gst_app_src_end_of_stream(GST_APP_SRC(appsrc));
        return;
    }
    
    // Fill with a simple sine wave (440 Hz) for demonstration
    for (int i = 0; i < data_size / (current_audio_data_params.depth / 8); ++i) {
        ((gint16*)data_buffer)[i] = (gint16)(32000 * sin(2 * G_PI * 440 * (double)i / (current_audio_data_params.sample_rate * current_audio_data_params.channels)));
    }

    buffer = gst_buffer_new_and_alloc(data_size);
    gst_buffer_fill(buffer, 0, data_buffer, data_size);
    g_free(data_buffer); // Free the temporary data buffer

    ret = gst_app_src_push_buffer(GST_APP_SRC(appsrc), buffer);
    if (ret != GST_FLOW_OK) {
        g_printerr("Failed to push data to appsrc: %d\n", ret);
        gst_buffer_unref(buffer);
        gst_app_src_end_of_stream(GST_APP_SRC(appsrc)); // Signal EOS on push failure
    }
}

/**
 * @brief Callback for appsrc's "enough-data" signal.
 * This function is called when appsrc has enough data.
 * For this example, we don't need to do anything specific here,
 * as data is pushed on demand via "need-data".
 *
 * @param appsrc The GstAppSrc element.
 * @param user_data User data.
 */
static void stop_feed(GstElement *appsrc, gpointer user_data) {
    g_print("Appsrc has enough data. Temporarily stopping data generation (if continuous).\n");
    // In a real scenario, you might pause a data generation thread here.
}


/**
 * @brief This function will alternate between PCM and AC3 format.
 * It updates the global `current_audio_data_params` and triggers pipeline reconfiguration.
 * Data pushing is handled by appsrc's "need-data" signal.
 *
 * @param user_data User data (GMainLoop).
 * @return gboolean TRUE to continue calling this timeout function, FALSE to stop.
 */
static gboolean simulate_audio_data_feed(gpointer user_data) {
    static int toggle = 0; // Used to alternate between PCM and AC3
    GMainLoop *loop_ptr = (GMainLoop *)user_data; // Cast user_data to GMainLoop*

    if (toggle % 2 == 0) {
        g_print("Switching to PCM format.\n");
        strcpy(current_audio_data_params.format, "PCM");
        current_audio_data_params.sample_rate = 48000;
        current_audio_data_params.channels = 2;
        current_audio_data_params.depth = 16;
        handle_audio_format_change("PCM");
    } else {
        g_print("Switching to AC3 format.\n");
        strcpy(current_audio_data_params.format, "AC3");
        // For AC3 encoding, we still need PCM input parameters
        current_audio_data_params.sample_rate = 48000; // AC3 supports 48kHz
        current_audio_data_params.channels = 2;        // Stereo for AC3
        current_audio_data_params.depth = 16;          // 16-bit PCM input
        handle_audio_format_change("AC3");
    }
    
    toggle++; // Increment toggle for next format switch
    return G_SOURCE_CONTINUE; // Continue calling this timeout function
}


/**
 * @brief Callback function for handling messages from the GStreamer bus.
 * Processes EOS (End-of-Stream) and ERROR messages to control the main loop.
 *
 * @param bus The GstBus object.
 * @param msg The GstMessage received from the bus.
 * @param data User data (in this case, the GMainLoop).
 * @return gboolean TRUE to continue watching the bus, FALSE to stop.
 */
static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
    GMainLoop *loop = (GMainLoop *)data; // Cast user data to GMainLoop

    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS: // End-of-Stream message
            g_print("End of stream.\n");
            g_main_loop_quit(loop); // Quit the main loop
            break;
        case GST_MESSAGE_ERROR: { // Error message
            GError *err = NULL;
            gchar *debug_info = NULL;

            // Parse the error message
            gst_message_parse_error(msg, &err, &debug_info);
            g_printerr("Error: %s\n", err->message);
            g_printerr("Debug info: %s\n", debug_info ? debug_info : "none");
            g_error_free(err);       // Free the error object
            g_free(debug_info);      // Free the debug info string
            g_main_loop_quit(loop);  // Quit the main loop on error
            break;
        }
        case GST_MESSAGE_WARNING: { // Warning message - 추가
            GError *err = NULL;
            gchar *debug_info = NULL;

            gst_message_parse_warning(msg, &err, &debug_info);
            g_printerr("Warning: %s\n", err->message);
            g_printerr("Debug info: %s\n", debug_info ? debug_info : "none");
            g_error_free(err);
            g_free(debug_info);
            break;
        }
        case GST_MESSAGE_STATE_CHANGED: { // State change message
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
            // Only print state changes for the pipeline itself
            if (GST_MESSAGE_SRC(msg) == GST_OBJECT(pipeline)) {
                g_print("Pipeline state changed: %s -> %s\n",
                        gst_element_state_get_name(old_state),
                        gst_element_state_get_name(new_state));
            }
            break;
        }
        default:
            break;
    }
    return TRUE; // Keep watching the bus
}

/**
 * @brief Checks if the audio format has changed and triggers pipeline reconfiguration.
 * This function is called when new audio data arrives.
 *
 * @param new_format The format of the new audio data ("PCM" or "AC3").
 */
// Removed 'static' keyword from definition and moved it here
void handle_audio_format_change(const char *new_format) {
    gboolean new_is_pcm = (g_strcmp0(new_format, "PCM") == 0);

    // Reconfigure pipeline only if it's NULL (first run) or if the format has changed
    if (pipeline == NULL || (new_is_pcm != is_pcm_format)) {
        g_print("Audio format change detected: Current %s -> New %s. Reconfiguring pipeline.\n", 
                is_pcm_format ? "PCM" : "AC3", new_format);
        configure_pipeline(new_format);
    }
}

/**
 * @brief Main function of the gst_sender application.
 * Initializes GStreamer, sets up a main loop, and simulates audio data
 * input with format switching.
 *
 * @param argc Number of command line arguments.
 * @param argv Array of command line arguments.
 * @return int Application exit code.
 */
int main(int argc, char *argv[]) {
    // Initialize GStreamer library
    gst_init(&argc, &argv);

    // Create a GLib Main Loop to handle events (like GStreamer messages, timeouts)
    main_loop = g_main_loop_new(NULL, FALSE); // Assign to global main_loop

    g_print("Starting `gst_sender` application. Waiting for audio data...\n");

    // Initialize current_audio_data_params with a default format (e.g., PCM)
    strcpy(current_audio_data_params.format, "PCM");
    current_audio_data_params.sample_rate = 48000;
    current_audio_data_params.channels = 2;
    current_audio_data_params.depth = 16;
    
    // Configure the initial pipeline
    configure_pipeline("PCM");

    // This timeout function will alternate between PCM and AC3 format every 5 seconds.
    // It only triggers the format change, data pushing is handled by appsrc signals.
    g_timeout_add_seconds(5, simulate_audio_data_feed, main_loop); // Pass global main_loop

    // Start the GLib Main Loop, which will run until g_main_loop_quit() is called
    g_main_loop_run(main_loop);

    // --- Cleanup ---
    // Set the pipeline to NULL state to release all resources
    if (pipeline) {
        g_print("Application exiting: Cleaning up pipeline...\n");
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline); // Unreference the pipeline object
    }
    g_main_loop_unref(main_loop); // Unreference the main loop
    gst_deinit(); // Deinitialize GStreamer resources

    return 0; // Exit successfully
}