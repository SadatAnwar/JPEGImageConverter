################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../OpenCL/Device.cpp \
../OpenCL/Error.cpp \
../OpenCL/Event.cpp \
../OpenCL/GetError.cpp \
../OpenCL/Program.cpp 

OBJS += \
./OpenCL/Device.o \
./OpenCL/Error.o \
./OpenCL/Event.o \
./OpenCL/GetError.o \
./OpenCL/Program.o 

CPP_DEPS += \
./OpenCL/Device.d \
./OpenCL/Error.d \
./OpenCL/Event.d \
./OpenCL/GetError.d \
./OpenCL/Program.d 


# Each subdirectory must supply rules for building sources it contributes
OpenCL/%.o: ../OpenCL/%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ -DOMPI_SKIP_MPICXX -I"/home/azeemmd/workspace/JpegEncoder" -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


