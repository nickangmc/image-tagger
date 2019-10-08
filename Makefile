CC = gcc
CFLAGS =  
# CFLAGS = -std=c99 -O3 -Wall -Wpedantic -g

SRC = image_tagger.c
TARGET = image_tagger

all: $(SRC)
	$(CC) $(SRC) -o $(TARGET) $(CFLAGS)

clean:
	rm -f $(TARGET) *.o
