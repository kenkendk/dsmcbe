################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../datastructures.c \
../dsmcbe_ppu.c \
../ppu.c 

OBJS += \
./datastructures.o \
./dsmcbe_ppu.o \
./ppu.o 

C_DEPS += \
./datastructures.d \
./dsmcbe_ppu.d \
./ppu.d 


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	/opt/cell/toolchain/bin/ppu-gcc -I/opt/cell/sysroot/opt/cell/sdk/usr/include -I/root/dsmcbe/Stage1/Common -O0 -g3 -Wall -c -fmessage-length=0 -Winline -Wextra -fno-inline -m32 -mabi=altivec -maltivec -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


