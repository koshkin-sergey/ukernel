# This makefile accepts two params: ARCH and COMPILER. Both are mandatory.
#
#  ARCH: the following values are valid:
#
#     cortex_m0
#     cortex_m0plus    // it's not implemented
#     cortex_m1        // it's not implemented
#     cortex_m3
#     cortex_m4
#     cortex_m4f       // it's not implemented
#
#  COMPILER: the following values are valid:
#
#     armcc
#     armclang         // it's not implemented
#
#  Example invocation:
#
#     $ make ARCH=cortex_m3 COMPILER=armcc
#

ERR_MSG_STD = See comments in the Makefile

ifneq ($(MAKECMDGOALS),clean)
  ifeq ($(ARCH),)
    $(error ARCH is undefined. $(ERR_MSG_STD))
  endif

  ifeq ($(COMPILER),)
     $(error COMPILER is undefined. $(ERR_MSG_STD))
  endif
  
endif

# set the locations of the bin and lib output directories
ifndef BIN_FOLDER
BIN_FOLDER = ./bin/$(ARCH)/$(COMPILER)
endif

ifndef SRC
SRC = ../../src
endif

ifndef OBJ_FOLDER
OBJ_FOLDER = ./obj/$(ARCH)/$(COMPILER)
endif

MKDIR = mkdir
RM = rm

#---------------------------------------------------------------------------
# Cortex-M series
#---------------------------------------------------------------------------

ifeq ($(ARCH), $(filter $(ARCH), cortex_m0 cortex_m0plus cortex_m1 cortex_m3 cortex_m4 cortex_m4f))
  
  ifeq ($(COMPILER), armcc)
    
    ARCH_DIR = $(SRC)/kernel/ARM
    
    ifeq ($(ARCH), cortex_m0)
      CPU := --cpu=cortex-m0
      ARCH_IRQ := $(ARCH_DIR)/irq_cm0.s
    endif

    ifeq ($(ARCH), cortex_m3)
      CPU := --cpu=cortex-m3
      ARCH_IRQ := $(ARCH_DIR)/irq_cm3.s
    endif

    ifeq ($(ARCH), cortex_m4)
      CPU := --cpu=cortex-m4
      ARCH_IRQ := $(ARCH_DIR)/irq_cm3.s
    endif

    CC = armcc
    AS = armasm
    LD = armlink
    AR = armar

    DEPFLAGS = --depend=$(@:.o=.d) --depend_format=unix_escaped
    CFLAGS = $(INC) $(CPU) $(DEPFLAGS) $(DEFINES) --c99 -O2 -Otime --split_sections
    ASFLAGS = $(CPU) $(DEPFLAGS)
    
  endif
  
endif

BINARY = $(BIN_FOLDER)/ukernel.lib

INC := -I $(SRC)/include \
       -I $(SRC)/Config \
       -I $(SRC)/CMSIS/Core/Include

DIRS := $(SRC)/kernel \
        $(ARCH_DIR)

CFILES := $(foreach dir,$(DIRS),$(wildcard $(dir)/*.c))
SFILES := $(ARCH_IRQ)
OBJS := $(patsubst %.c,$(OBJ_FOLDER)/%.o, $(notdir $(CFILES))) $(patsubst %.s,$(OBJ_FOLDER)/%.o,$(notdir $(SFILES)))
DEPS := $(OBJS:.o=.d)

VPATH = $(DIRS)

ifneq ($(MAKECMDGOALS),clean)
  -include $(DEPS)
endif

# All Target
all: $(OBJ_FOLDER) $(BIN_FOLDER) $(BINARY)

# Other Targets
clean:
	@echo Cleaning...
	@$(RM) -rf $(BIN_FOLDER) $(OBJ_FOLDER)
	@echo Done

.PHONY: all clean

$(BIN_FOLDER):
	@$(MKDIR) -p $@

$(OBJ_FOLDER):
	@$(MKDIR) -p $@

$(BINARY): $(OBJS)
	@$(AR) -r $@ $?
	@$(AR) --zt $@
	@echo Done 

$(OBJ_FOLDER)/%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_FOLDER)/%.o: %.s
	$(AS) $(ASFLAGS) -o $@ $<
