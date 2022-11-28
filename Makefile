# g++ -lm -L/usr/X11/lib -lX11 -lXi -lXcursor -lEGL -lGLESv2 imgui/imgui*.cpp main.cpp -o demo

CXX = g++
CXXFLAGS = -Wall -g

SOURCES = imgui/imgui.cpp imgui/imgui_demo.cpp imgui/imgui_draw.cpp imgui/imgui_tables.cpp imgui/imgui_widgets.cpp main.cpp
OBJS = $(addprefix obj/, $(addsuffix .o, $(basename $(notdir $(SOURCES)))))
LIBS = -lm -L/usr/X11/lib -lX11 -lXi -lXcursor -lEGL -lGLESv2 -Lcpr -lcpr -lcurl -l:libz.a -lssh2 -lssl -lcrypto -Lgumbo -lgumbo
EXE = stol

obj/%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

obj/%.o: imgui/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(EXE): $(OBJS)
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LIBS)

all : $(EXE)

clean:
	rm -f $(EXE) $(OBJS)

# TODO: dependencies maybe

.PHONY: all clean
