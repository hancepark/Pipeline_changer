gst-launch-1.0 -v udpsrc port=5000 caps="application/x-rtp, media=audio, encoding-name=AC3, payload=96" ! \
rtpac3depay ! avdec_ac3 ! audioconvert ! audioresample ! autoaudiosink
