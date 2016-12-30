CFLAGS := -Wall -O2
OBJS := main.o p2s.o
INCLUDES := p2s.h
LIBS := -lpthread
TARGET := main
all: $(TARGET)
main: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LIBS)
main.o: $(INCLUDES)
ps2.o: p2s.c $(INCLUDES)
.PHONY: clean
clean:
	$(RM) $(TARGET) $(OBJS)
