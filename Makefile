ifeq ($(CXX),g++)
    EXTRA_FLAGS = --no-gnu-unique
else
    EXTRA_FLAGS =
endif

CXXFLAGS ?= -O2
CXXFLAGS += -shared -fPIC -std=c++2b -Wno-c++11-narrowing

INCLUDES = `pkg-config --cflags hyprland`
LIBS = `pkg-config --libs hyprland`

SRC = $(wildcard src/*.cpp)
TARGET = cde.so

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(EXTRA_FLAGS) $(INCLUDES) $^ -o $@ $(LIBS)

clean:
	rm -f ./$(TARGET)

.PHONY: all clean
