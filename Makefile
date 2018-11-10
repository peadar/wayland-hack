all: wh

CXXFLAGS += -g -std=c++17
FLING_OBJS=wh.o

WAYLAND_LIBS += -lwayland-client -lEGL -lGLESv2
wh: $(FLING_OBJS)
	$(CXX) -o $@ $< $(WAYLAND_LIBS)
