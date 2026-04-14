CC := gcc

all: proxy
CFLAGS := -ggdb -O2 -Wall -W -std=c99
TARGET := proxy

$(TARGET): main.c lib.c
	$(CC) main.c lib.c -o $(TARGET) $(CFLAGS)

run: $(TARGET)
	./$(TARGET)
	
clean:
	rm -f proxy
