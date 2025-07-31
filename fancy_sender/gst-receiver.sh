#!/bin/bash

PORT=5000

echo "[RECEIVER] Listening on UDP port $PORT ..."

gst-launch-1.0 -v udpsrc port=$PORT caps="application/x-rtp" ! \
    application/x-rtp,encoding-name=OPUS,payload=96 ! \
    rtpopusdepay ! opusdec ! audioconvert ! audioresample ! autoaudiosink \
    2> >(grep --line-buffered "RTP" >&2) \
    || gst-launch-1.0 -v udpsrc port=$PORT caps="application/x-rtp" ! \
    application/x-rtp,encoding-name=AC3,payload=96 ! \
    rtpac3depay ! avdec_ac3 ! audioconvert ! audioresample ! autoaudiosink
# If the first pipeline fails, it will try the second one for AC3 audio.