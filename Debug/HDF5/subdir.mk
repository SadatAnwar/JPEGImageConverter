################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../HDF5/Array.cpp \
../HDF5/AtomicType.cpp \
../HDF5/AtomicTypes.cpp \
../HDF5/Attribute.cpp \
../HDF5/BaseTypes.cpp \
../HDF5/ComplexConversion.cpp \
../HDF5/CompoundType.cpp \
../HDF5/DataSet.cpp \
../HDF5/DataSpace.cpp \
../HDF5/DataType.cpp \
../HDF5/DataTypes.cpp \
../HDF5/DelayedArray.cpp \
../HDF5/Exception.cpp \
../HDF5/File.cpp \
../HDF5/Group.cpp \
../HDF5/IdComponent.cpp \
../HDF5/Matlab.cpp \
../HDF5/MatlabDiagMatrix3.cpp \
../HDF5/MatlabVector2.cpp \
../HDF5/MatlabVector3.cpp \
../HDF5/Object.cpp \
../HDF5/OpaqueType.cpp \
../HDF5/PropList.cpp \
../HDF5/PropLists.cpp \
../HDF5/ReferenceType.cpp \
../HDF5/SerializationKey.cpp \
../HDF5/Type.cpp \
../HDF5/Util.cpp 

OBJS += \
./HDF5/Array.o \
./HDF5/AtomicType.o \
./HDF5/AtomicTypes.o \
./HDF5/Attribute.o \
./HDF5/BaseTypes.o \
./HDF5/ComplexConversion.o \
./HDF5/CompoundType.o \
./HDF5/DataSet.o \
./HDF5/DataSpace.o \
./HDF5/DataType.o \
./HDF5/DataTypes.o \
./HDF5/DelayedArray.o \
./HDF5/Exception.o \
./HDF5/File.o \
./HDF5/Group.o \
./HDF5/IdComponent.o \
./HDF5/Matlab.o \
./HDF5/MatlabDiagMatrix3.o \
./HDF5/MatlabVector2.o \
./HDF5/MatlabVector3.o \
./HDF5/Object.o \
./HDF5/OpaqueType.o \
./HDF5/PropList.o \
./HDF5/PropLists.o \
./HDF5/ReferenceType.o \
./HDF5/SerializationKey.o \
./HDF5/Type.o \
./HDF5/Util.o 

CPP_DEPS += \
./HDF5/Array.d \
./HDF5/AtomicType.d \
./HDF5/AtomicTypes.d \
./HDF5/Attribute.d \
./HDF5/BaseTypes.d \
./HDF5/ComplexConversion.d \
./HDF5/CompoundType.d \
./HDF5/DataSet.d \
./HDF5/DataSpace.d \
./HDF5/DataType.d \
./HDF5/DataTypes.d \
./HDF5/DelayedArray.d \
./HDF5/Exception.d \
./HDF5/File.d \
./HDF5/Group.d \
./HDF5/IdComponent.d \
./HDF5/Matlab.d \
./HDF5/MatlabDiagMatrix3.d \
./HDF5/MatlabVector2.d \
./HDF5/MatlabVector3.d \
./HDF5/Object.d \
./HDF5/OpaqueType.d \
./HDF5/PropList.d \
./HDF5/PropLists.d \
./HDF5/ReferenceType.d \
./HDF5/SerializationKey.d \
./HDF5/Type.d \
./HDF5/Util.d 


# Each subdirectory must supply rules for building sources it contributes
HDF5/%.o: ../HDF5/%.cpp
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C++ Compiler'
	g++ -DOMPI_SKIP_MPICXX -I"/Users/sadat/Documents/workspace/Project" -I/opt/local/include -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


