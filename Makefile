SHELL := /bin/bash

CXX = g++ -g

SYSTEMC_LIB = $(SYSTEMC_HOME)/lib-linux64

SOURCE_DIR ?= src
TESTBENCH_DIR ?= test

# VPATH for finding source files
VPATH = $(SOURCE_DIR) $(TESTBENCH_DIR)

INCDIRS := -I. -I$(SOURCE_DIR) -I$(TESTBENCH_DIR)
INCDIRS += -I$(SYSTEMC_HOME)/include -I$(SYSTEMC_HOME)/src
INCDIRS += -I$(CONNECTIONS_HOME)/include
INCDIRS += -I$(MATCHLIB_HOME)/cmod/include
INCDIRS += -I$(BOOST_HOME)/include
INCDIRS += -I$(RAPIDJSON_HOME)/include
INCDIRS += -I$(AC_TYPES)/include
INCDIRS += -I$(AC_SIMUTILS)/include
INCDIRS += -I$(MATCHLIB_EXAMPLES)/include
INCDIRS += -I$(MEMORY)

CXXFLAGS = -std=c++17 $(INCDIRS) -DSC_ALLOW_DEPRECATED_IEEE_API -DSC_INCLUDE_DYNAMIC_PROCESSES -DBOOST_NULLPTR=nullptr $(EXTRA_CXXFLAGS)
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
$(BUILD_DIR)/%.o: %.cpp ac_reset_signal_is.h
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
	rm -rf $(BUILD_DIR) ac_reset_signal_is.h
	@echo "Clean complete!"

# Phony targets
.PHONY: all clean run run_fft_tb run_mem_tb run_dma_tb run_system_tb

# FFT Testbench
FFT_TB_SRCS = tb_fft.cpp
FFT_TB_OBJS = $(addprefix $(BUILD_DIR)/, $(FFT_TB_SRCS:.cpp=.o))
FFT_TB_TARGET = $(BUILD_DIR)/tb_fft

$(FFT_TB_TARGET): $(BUILD_DIR) $(FFT_TB_OBJS)
	@echo "Linking $(FFT_TB_TARGET)..."
	$(CXX) $(FFT_TB_OBJS) $(LDFLAGS) -o $(FFT_TB_TARGET)
	@echo "Build successful!"

run_fft_tb: $(FFT_TB_TARGET) $(OUT_DIR)
	@echo "Running FFT testbench..."
	@echo "Output will be saved to: $(OUT_DIR)/log/sim_fft_tb.txt"
	@echo ""
	@$(FFT_TB_TARGET) | tee $(OUT_DIR)/log/sim_fft_tb.txt

# Memory Testbench
MEM_TB_SRCS = tb_memory.cpp
MEM_TB_OBJS = $(addprefix $(BUILD_DIR)/, $(MEM_TB_SRCS:.cpp=.o))
MEM_TB_TARGET = $(BUILD_DIR)/tb_memory

$(MEM_TB_TARGET): $(BUILD_DIR) $(MEM_TB_OBJS)
	@echo "Linking $(MEM_TB_TARGET)..."
	$(CXX) $(MEM_TB_OBJS) $(LDFLAGS) -o $(MEM_TB_TARGET)
	@echo "Build successful!"

run_mem_tb: $(MEM_TB_TARGET) $(OUT_DIR)
	@echo "Running Memory testbench..."
	@echo "Output will be saved to: $(OUT_DIR)/log/sim_mem_tb.txt"
	@echo ""
	@$(MEM_TB_TARGET) | tee $(OUT_DIR)/log/sim_mem_tb.txt

# DMA Testbench
DMA_TB_SRCS = tb_dma.cpp
DMA_TB_OBJS = $(addprefix $(BUILD_DIR)/, $(DMA_TB_SRCS:.cpp=.o))
DMA_TB_TARGET = $(BUILD_DIR)/tb_dma

$(DMA_TB_TARGET): $(BUILD_DIR) $(DMA_TB_OBJS)
	@echo "Linking $(DMA_TB_TARGET)..."
	$(CXX) $(DMA_TB_OBJS) $(LDFLAGS) -o $(DMA_TB_TARGET)
	@echo "Build successful!"

run_dma_tb: $(DMA_TB_TARGET) $(OUT_DIR)
	@echo "Running DMA testbench..."
	@echo "Output will be saved to: $(OUT_DIR)/log/sim_dma_tb.txt"
	@echo ""
	@$(DMA_TB_TARGET) | tee $(OUT_DIR)/log/sim_dma_tb.txt

# System Testbench
SYSTEM_TB_SRCS = tb_system.cpp
SYSTEM_TB_OBJS = $(addprefix $(BUILD_DIR)/, $(SYSTEM_TB_SRCS:.cpp=.o))
SYSTEM_TB_TARGET = $(BUILD_DIR)/tb_system

$(SYSTEM_TB_TARGET): $(BUILD_DIR) $(SYSTEM_TB_OBJS)
	@echo "Linking $(SYSTEM_TB_TARGET)..."
	$(CXX) $(SYSTEM_TB_OBJS) $(LDFLAGS) -o $(SYSTEM_TB_TARGET)
	@echo "Build successful!"

run_system_tb: $(SYSTEM_TB_TARGET) $(OUT_DIR)
	@echo "Running System testbench..."
	@echo "Output will be saved to: $(OUT_DIR)/log/sim_system_tb.txt"
	@echo ""
	@$(SYSTEM_TB_TARGET) | tee $(OUT_DIR)/log/sim_system_tb.txt

# System Testbench with FI Memory
SYS_TB_MEM_SRCS = tb_system_wmem.cpp
SYS_TB_MEM_OBJS = $(addprefix $(BUILD_DIR)/, $(SYS_TB_MEM_SRCS:.cpp=.o))
SYS_TB_MEM_TARGET = $(BUILD_DIR)/tb_system_wmem

$(SYS_TB_MEM_TARGET): $(BUILD_DIR) $(SYS_TB_MEM_OBJS)
	@echo "Linking $(SYS_TB_MEM_TARGET)..."
	$(CXX) $(SYS_TB_MEM_OBJS) $(LDFLAGS) -o $(SYS_TB_MEM_TARGET)
	@echo "Build successful!"

run_system_tb_wmem: $(SYS_TB_MEM_TARGET) $(OUT_DIR)
	@echo "Running System testbench..."
	@echo "Output will be saved to: $(OUT_DIR)/log/sim_system_tb.txt"
	@echo ""
	@$(SYS_TB_MEM_TARGET) | tee $(OUT_DIR)/log/sim_system_tb.txt

# Rule to generate the dummy catapult header for compatibility
ac_reset_signal_is.h:
	@touch ac_reset_signal_is.h