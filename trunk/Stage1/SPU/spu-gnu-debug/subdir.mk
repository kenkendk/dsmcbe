################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../datastructures.c \
../dsmcbe_spu.c 

OBJS += \
./datastructures.o \
./dsmcbe_spu.o 

C_DEPS += \
./datastructures.d \
./dsmcbe_spu.d 


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	/opt/cell/toolchain/bin/spu-gcc -I/opt/ibm/systemsim-cell/include/callthru/spu -I/root/dsmcbe/Stage1/Common -I/opt/cell/sysroot/opt/cell/sdk/usr/include -O0 -g3 -Wall -c -fmessage-length=0 -Winline -Wextra -fno-inline -mtune=cell -mfloat=fast -mdouble=fast -Wno-main -march=cell -mea32 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


