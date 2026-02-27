SHELL := /bin/bash

CXX = g++

SYSTEMC_LIB = $(SYSTEMC_HOME)/lib-linux64

CXXFLAGS = -std=c++17 -I$(SYSTEMC_HOME)/include -DSC_ALLOW_DEPRECATED_IEEE_API
LDFLAGS = -L$(SYSTEMC_LIB) -lsystemc -lm

# Build directory
BUILD_DIR = build

# Output directory for simulation results
OUT_DIR = out

# Target executable
TARGET = $(BUILD_DIR)/main

# Source files
SRCS = main.cpp

# Object files in build directory
OBJS = $(addprefix $(BUILD_DIR)/, $(SRCS:.cpp=.o))

all: $(TARGET)

# Create build directory if it doesn't exist
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# Create output directory if it doesn't exist
$(OUT_DIR):
	@mkdir -p $(OUT_DIR)/{log,vcd}

# Build executable
$(TARGET): $(BUILD_DIR) $(OBJS)
	@echo "Linking $(TARGET)..."
	$(CXX) $(OBJS) $(LDFLAGS) -o $(TARGET)
	@echo "Build successful!"

# Compile source files
$(BUILD_DIR)/%.o: %.cpp
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Simulation output name (can be overridden: make run SIM_NAME=mytest)
SIM_NAME ?= $(shell date +%Y%m%d_%H%M%S)

# Run simulation
run: $(TARGET) $(OUT_DIR)
	@echo "Running simulation..."
	@echo "Output will be saved to: $(OUT_DIR)/log/sim_$(SIM_NAME).txt"
	@echo ""
	@$(TARGET) | tee $(OUT_DIR)/log/sim_$(SIM_NAME).txt

# Clean build artifacts
clean:
	@echo "Cleaning..."
	rm -rf $(BUILD_DIR)
	@echo "Clean complete!"

# Phony targets
.PHONY: all clean
# Target for FFT Testbench
FFT_TB_SRCS = tb_fft.cpp
FFT_TB_OBJS = $(addprefix $(BUILD_DIR)/, $(FFT_TB_SRCS:.cpp=.o))
FFT_TB_TARGET = $(BUILD_DIR)/tb_fft

$(FFT_TB_TARGET): $(BUILD_DIR) $(FFT_TB_OBJS)
	@echo "Linking $(FFT_TB_TARGET)..."
	$(CXX) $(FFT_TB_OBJS) $(LDFLAGS) -o $(FFT_TB_TARGET)
	@echo "Build successful!"

run_fft_tb: $(FFT_TB_TARGET) $(OUT_DIR)
	@echo "Running FFT testbench..."
	@$(FFT_TB_TARGET)

