RACK_DIR ?= /Users/dom/Development/Rack-SDK
DAISYSP_DIR = ../../DaisySP

FLAGS += -I$(DAISYSP_DIR)/Source
FLAGS += -I$(DAISYSP_DIR)/Source/Utility
FLAGS += -I$(DAISYSP_DIR)/Source/Control
FLAGS += -I$(DAISYSP_DIR)/Source/Synthesis
FLAGS += -I$(DAISYSP_DIR)/Source/Filters
FLAGS += -I$(DAISYSP_DIR)/Source/Effects
FLAGS += -I..
FLAGS += -DDAISYSP_NO_ARDUINO

SOURCES += src/plugin.cpp
SOURCES += src/Tymbal.cpp

SOURCES += $(DAISYSP_DIR)/Source/Control/adsr.cpp
SOURCES += $(DAISYSP_DIR)/Source/Synthesis/oscillator.cpp
SOURCES += $(DAISYSP_DIR)/Source/Filters/svf.cpp
SOURCES += $(DAISYSP_DIR)/Source/Effects/chorus.cpp

DISTRIBUTABLES += res

include $(RACK_DIR)/plugin.mk
