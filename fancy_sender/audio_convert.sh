# .wav → .ac3
ffmpeg -i input.wav -acodec ac3 -b:a 192k output.ac3

# .wav → .pcm
ffmpeg -i input.wav -f s16le -acodec pcm_s16le -ar 48000 -ac 2 output.pcm
