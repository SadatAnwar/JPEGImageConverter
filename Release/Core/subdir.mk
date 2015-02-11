################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../Core/Assert.cpp \
../Core/CheckedCast.cpp \
../Core/Error.cpp \
../Core/Exception.cpp \
../Core/Image.cpp \
../Core/Memory.cpp \
../Core/NumericException.cpp \
../Core/Time.cpp \
../Core/TimeSpan.cpp \
../Core/Type.cpp \
../Core/WindowsError.cpp 

C_SRCS += \
../Core/StrError.c 

OBJS += \
./Core/Assert.o \
./Core/CheckedCast.o \
./Core/Error.o \
./Core/Exception.o \
./Core/Image.o \
./Core/Memory.o \
./Core/NumericException.o \
./Core/StrError.o \
./Core/Time.o \
./Core/TimeSpan.o \
./Core/Type.o \
./Core/WindowsError.o 

C_DEPS += \
./Core/StrError.d 

CPP_DEPS += \
./Core/Assert.d \
./Core/CheckedCast.d \
./Core/Error.d \
./Core/Exception.d \
./Core/Image.d \
./Core/Memory.d \
./Core/NumericException.d \
./Core/Time.d \
./Core/TimeSpan.d \
./Core/Type.d \
./Core/WindowsError.d 


# Each subdirectory must supply rules for building sources it contributes
Core/%.o: ../Core/%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ -DOMPI_SKIP_MPICXX -I"/home/azeemmd/workspace/JpegEncoder" -I/usr/include/mpi -O3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

Core/%.o: ../Core/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -I"/home/azeemmd/workspace/JpegEncoder" -I/usr/include/mpi -O3 -Wall -c -fmessage-length=0 -DOMPI_SKIP_MPICXX -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


