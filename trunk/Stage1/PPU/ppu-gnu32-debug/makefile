################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Custom includes added by CellDT makefile generator
-include import_spu.mk

# Macros added by the CDT's makefile generator

-include ../makefile.init

RM := rm -fr

# All of the sources participating in the build are defined here
-include sources.mk
-include subdir.mk
-include spu_objs/subdir.mk
-include ThreadbasedHandler/subdir.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif

-include ../makefile.defs

# Add inputs and outputs from these tool invocations to the build variables 

# All Target
all: PPU

dependents:
	-cd /root/dsmcbe/Stage1/SPU/spu-gnu-debug && $(MAKE) all

# Tool invocations
PPU: $(OBJS) $(USER_OBJS) /root/dsmcbe/Stage1/SPU/spu-gnu-debug/SPU
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C Linker'
	/opt/cell/toolchain/bin/ppu-gcc -m32 -o"PPU" $(OBJS) $(USER_OBJS) $(LIBS)
	@echo 'Finished building target: $@'
	@echo ' '

# Other Targets
clean:
	-$(RM) $(OBJS)$(EXECUTABLES)$(C_DEPS) PPU
	-@echo ' '

.PHONY: all clean dependents
.SECONDARY:
/root/dsmcbe/Stage1/SPU/spu-gnu-debug/SPU:

-include ../makefile.targets
