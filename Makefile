# Part 1 - MESI snooping cache coherence simulator
#
#   make            build
#   make run        build and run
#   make clean      remove the build output

CXX      = g++
CXXFLAGS = -Wall -O1 -std=c++11
TARGET   = mesi_sim
OBJS     = main.o sim.o screens.o tests.o cache.o bus.o memory.o mesi.o ui.o
HEADERS  = config.h mesi.h cache.h bus.h memory.h sim.h ui.h tests.h

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET) $(TARGET).exe run_log.txt

.PHONY: all run clean
