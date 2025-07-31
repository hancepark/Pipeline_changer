CC = gcc
CFLAGS = `pkg-config --cflags gstreamer-1.0 gstreamer-app-1.0`
LIBS = `pkg-config --libs gstreamer-1.0 gstreamer-app-1.0`

SRCS = gst_sender.c format_switcher.c pipeline_pcm.c pipeline_ac3.c
OBJS = $(SRCS:.c=.o)
TARGET = gst_sender

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LIBS)

clean:
	rm -f $(TARGET) *.o
