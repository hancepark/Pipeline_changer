
gst-launch-1.0 -v udpsrc port=5000 caps="application/x-rtp, media=audio, encoding-name=OPUS, payload=96" ! \
rtpopusdepay ! opusdec ! audioconvert ! audioresample ! autoaudiosink
