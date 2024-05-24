WRITER  = writer
READER  = reader
CFLAGS  = -std=c++17 -ggdb
LDFLAGS = -lglfw -lvulkan -ldl -lpthread -lX11 -lXxf86vm -lXrandr -lXi

all: writer.cpp reader.cpp
	g++ $(CFLAGS) -o $(WRITER) writer.cpp $(LDFLAGS)
	g++ $(CFLAGS) -o $(READER) reader.cpp $(LDFLAGS)

clean:
	rm -fv $(WRITER) $(READER)
