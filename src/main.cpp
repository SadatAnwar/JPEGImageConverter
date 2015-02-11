//////////////////////////////////////////////////////////////////////////////
// OpenCL Project Topic_1 (JPEG ENCODER)
//////////////////////////////////////////////////////////////////////////////

// includes

#include <stdio.h>
#include <Core/Assert.hpp>
#include <Core/Time.hpp>
#include <Core/Image.hpp>
#include <OpenCL/cl-patched.hpp>
#include <OpenCL/Program.hpp>
#include <OpenCL/Event.hpp>
#include <OpenCL/Device.hpp>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>
#include <boost/lexical_cast.hpp>
#include "savejpg.h"

//////////////////////////////////////////////////////////////////////////////
// Variable  Decleration
//////////////////////////////////////////////////////////////////////////////

static BYTE bytenew=0; // The byte that will be written in the JPG file
static SBYTE bytepos=7; //bit position in the byte we write (bytenew)
//should be<=7 and >=0
static WORD mask[16]={1,2,4,8,16,32,64,128,256,512,1024,2048,4096,8192,16384,32768};

// The Huffman tables we'll use:
static bitstring YDC_HT[12];
static bitstring CbDC_HT[12];
static bitstring YAC_HT[256];
static bitstring CbAC_HT[256];

static BYTE *category_alloc;
static BYTE *category; //Here we'll keep the category of the numbers in range: -32767..32767
static bitstring *bitcode_alloc;
static bitstring *bitcode; // their bitcoded representation

//Precalculated tables for a faster YCbCr->RGB transformation
// We use a SDWORD table because we'll scale values by 2^16 and work with integers
static SDWORD YRtab[256],YGtab[256],YBtab[256];
static SDWORD CbRtab[256],CbGtab[256],CbBtab[256];
static SDWORD CrRtab[256],CrGtab[256],CrBtab[256];
static float fdtbl_Y[64];
static float fdtbl_Cb[64]; //the same with the fdtbl_Cr[64]

static colorRGB *RGB_buffer; //image to be encoded
static WORD Ximage,Yimage;// image dimensions divisible by 8
static SBYTE YDU[64]; // This is the Data Unit of Y after YCbCr->RGB transformation
static SBYTE CbDU[64]; // Data unit of Cb
static SBYTE CrDU[64]; // Data unit of Cb
static SWORD DU_DCT[64]; // Current DU (after DCT and quantization) which we'll zigzag
static SWORD DU[64]; //zigzag reordered DU which will be Huffman coded
static FILE *fp_jpeg_stream;
static Core::TimeSpan dctCpu = Core::TimeSpan::fromSeconds(0);

//////////////////////////////////////////////////////////////////////////////
//Printing Performance data
//////////////////////////////////////////////////////////////////////////////

void printPerformanceHeader() {
	std::cout <<std::endl<< "Implementation \t \t CPU \t\t  GPU  \t\t  GPU-MT \t GPU+MT \t Speedup (w/o MT)" << std::endl;
}

void printPerformance(const std::string& name, Core::TimeSpan timeCalc, Core::TimeSpan timeMem, Core::TimeSpan timeCpu, bool showMem = true) {
	Core::TimeSpan overallTime = timeCalc + timeMem;
	std::stringstream str;
	str << std::setiosflags (std::ios::left) << std::setw (20) << name;
	str << std::setiosflags (std::ios::right);
	str << "\t" << std::setw (9) << timeCpu;
	str << "\t" << std::setw (9) << timeCalc;
	if (showMem)
		str << "\t" << std::setw (9) << timeMem;
	else
		str << "\t" << std::setw (9) << "";
	str << "\t" << std::setw (9) << overallTime;
	str << "\t" << (timeCpu.getSeconds() / overallTime.getSeconds());
	if (showMem)
		str << " (" << (timeCpu.getSeconds() / timeCalc.getSeconds()) << ")";
	std::cout << str.str () << std::endl;
}

void printPerformance(const std::string& name, Core::TimeSpan timeCalc, Core::TimeSpan timeCpu) {

	printPerformance(name, timeCalc, Core::TimeSpan::fromSeconds(0), timeCpu, false);
}


//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
/// Helper methods for GPU Huffman coding, Run length Coding and File writing
//////////////////////////////////////////////////////////////////////////////
void jpegCodingAndFileCreation(SWORD* dataUnit, SWORD *DC,bitstring *HTDC, bitstring *HTAC){

	bitstring EOB=HTAC[0x00];
	bitstring M16zeroes=HTAC[0xF0];
	BYTE startpos;
	BYTE end0pos;
	BYTE nrzeroes;
	BYTE nrmarker;
	SWORD Diff;


	Diff=dataUnit[0]-*DC;
	*DC=dataUnit[0];
	//Encode DC
	if (Diff==0)
		writebits(HTDC[0]); //Diff might be 0
	else {writebits(HTDC[category[Diff]]);
		writebits(bitcode[Diff]);
	}
	//Encode ACs
	for (end0pos=63;(end0pos>0)&&(dataUnit[end0pos]==0);end0pos--) ;
	//end0pos = first element in reverse order !=0
	if (end0pos==0) {
		writebits(EOB);
		return;
	}
	// Run length coding
	BYTE i=1;
	while (i<=end0pos)
	{
		startpos=i;
		for (; (dataUnit[i]==0)&&(i<=end0pos);i++) ;
		nrzeroes=i-startpos;
		if (nrzeroes>=16) {
			for (nrmarker=1;nrmarker<=nrzeroes/16;nrmarker++) {
				writebits(M16zeroes);
			}
			nrzeroes=nrzeroes%16;
		}
		writebits(HTAC[nrzeroes*16+category[dataUnit[i]]]);
		writebits(bitcode[dataUnit[i]]);
		i++;
	}
	if (end0pos!=63)
		writebits(EOB);

}

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
/// GPU Implementation of JPEG Encoder (Initialization and data transfer to GPU)
//////////////////////////////////////////////////////////////////////////////

void jpegEncoderGPU(int argc, char **argv,  Core::TimeSpan cpuTime){

	cl::Context context(CL_DEVICE_TYPE_GPU);


	// Get a device of the context
	int deviceNr = argc < 2 ? 1 : atoi(argv[1]);
	std::cout << "Using device " << deviceNr << " / " << context.getInfo<CL_CONTEXT_DEVICES>().size() << std::endl;
	ASSERT (deviceNr > 0);
	ASSERT ((size_t) deviceNr <= context.getInfo<CL_CONTEXT_DEVICES>().size());
	cl::Device device = context.getInfo<CL_CONTEXT_DEVICES>()[deviceNr - 1];
	std::vector<cl::Device> devices;
	devices.push_back(device);
	OpenCL::printDeviceInfo(std::cout, device);


	// Create a command queue
	cl::CommandQueue queue(context, device, CL_QUEUE_PROFILING_ENABLE);

	// Declare some values
	std::size_t wgSize = 32; /// Work items per WG
	std::size_t count = 8*Ximage*Yimage/64; // total work items
	std::size_t RGBBufferGpu =  Ximage*Yimage* sizeof (colorRGB);
	std::size_t sizeFdTbl = 64 * sizeof (float);
	std::size_t sizeDct_Du = Ximage*Yimage* sizeof (cl_short);

//	 Load the source code
	cl::Program program = OpenCL::loadProgramSource(context, "src/kernel.cl");
//	 Compile the source code. This is similar to program.build(devices) but will print more detailed error messages
//	 This will pass the value of wgSize as a preprocessor constant "WG_SIZE" to the OpenCL C compiler
	OpenCL::buildProgram(program, devices, "-DWG_SIZE=" + boost::lexical_cast<std::string>(wgSize));

	/*Create the data holders for input and op data to and from GPU*/

	std::vector<colorRGB> RGBBuffer(RGB_buffer, RGB_buffer+(Ximage*Yimage));
	std::vector<cl_short> YDuDct(sizeDct_Du);
	std::vector<cl_short> CbDuDct(sizeDct_Du);
	std::vector<cl_short> CrDuDct(sizeDct_Du);
	std::vector<float> fdtbl_YGpu (fdtbl_Y, fdtbl_Y+64);
	std::vector<float> fdtbl_CGpu (fdtbl_Cb, fdtbl_Cb+64);


	//Reset memmory
	memset(YDuDct.data(), 255, sizeDct_Du);
	memset(CbDuDct.data(), 255, sizeDct_Du);
	memset(CrDuDct.data(), 255, sizeDct_Du);
	//Will not reset fdtbl_YGpu, fdtbl_CGpu, and RGBBUffer as they have valid data to be used


//	 Allocate space for input and output data on the device
	cl::Buffer d_RGBBuffer (context, CL_MEM_READ_WRITE, RGBBufferGpu);
	cl::Buffer d_YDuDct (context, CL_MEM_READ_WRITE, sizeDct_Du);
	cl::Buffer d_CbDuDct (context, CL_MEM_READ_WRITE, sizeDct_Du);
	cl::Buffer d_CrDuDct (context, CL_MEM_READ_WRITE, sizeDct_Du);
	cl::Buffer d_fdtbl_YGpu (context, CL_MEM_READ_ONLY, sizeFdTbl); //tables are constant so defined as read only (some performance)
	cl::Buffer d_fdtbl_CGpu (context, CL_MEM_READ_ONLY, sizeFdTbl); //tables are constant so defined as read only (some performance)
	cl::Buffer d_Ximage (context, CL_MEM_READ_WRITE,sizeof(int));
	cl::Buffer d_Yimage (context, CL_MEM_READ_WRITE, sizeof(int));


//	reset mem on GPU
	queue.enqueueWriteBuffer(d_YDuDct, true, 0, sizeDct_Du, YDuDct.data());
	queue.enqueueWriteBuffer(d_CbDuDct, true, 0, sizeDct_Du, CbDuDct.data());
	queue.enqueueWriteBuffer(d_CrDuDct, true, 0, sizeDct_Du, CrDuDct.data());

	//Fill required data into the RGBBuffer and FD tables
	cl::Event copyRGBBuffer;
	cl::Event copyFdTableY;
	cl::Event copyFdTableC;

	queue.enqueueWriteBuffer(d_RGBBuffer, true, 0, RGBBufferGpu, RGBBuffer.data(),NULL,&copyRGBBuffer);
	queue.enqueueWriteBuffer(d_fdtbl_YGpu, true, 0, sizeFdTbl, fdtbl_YGpu.data(),NULL,&copyFdTableY);
	queue.enqueueWriteBuffer(d_fdtbl_CGpu, true, 0, sizeFdTbl, fdtbl_CGpu.data(),NULL,&copyFdTableC);


//	 Create a kernel

	cl::Kernel kernel(program, "load_data_units_from_RGB");

	//Pass arguments to kernal function
	kernel.setArg<cl::Buffer>(0, d_RGBBuffer);
	kernel.setArg<cl::Buffer>(1, d_fdtbl_YGpu);
	kernel.setArg<cl::Buffer>(2, d_fdtbl_CGpu);
	kernel.setArg<int>(3, Ximage);
	kernel.setArg<cl::Buffer>(4, d_YDuDct);
	kernel.setArg<cl::Buffer>(5, d_CrDuDct);
	kernel.setArg<cl::Buffer>(6, d_CbDuDct);
	kernel.setArg<int>(7, Yimage);

	//launch kernel
	cl::Event kernelExecution;
	queue.enqueueNDRangeKernel(kernel, cl::NullRange, count,wgSize,NULL, &kernelExecution);

	//Events for profiling
	cl::Event copyY;
	cl::Event copyCr;
	cl::Event copyCb;
	// Copy output data back to host
	queue.enqueueReadBuffer(d_YDuDct, true, 0, sizeDct_Du, YDuDct.data(), NULL, &copyY);
	queue.enqueueReadBuffer(d_CrDuDct, true, 0, sizeDct_Du, CrDuDct.data(), NULL, &copyCr);
	queue.enqueueReadBuffer(d_CbDuDct, true, 0, sizeDct_Du, CbDuDct.data(), NULL, &copyCb);

	//profiling calculations
	Core::TimeSpan gpuTime = OpenCL::getElapsedTime(kernelExecution); //Time spent on GPU
	/*Calculation of time spent in memory operations*/
	Core::TimeSpan copyTime = OpenCL::getElapsedTime(copyRGBBuffer) + OpenCL::getElapsedTime(copyFdTableY) +
							  OpenCL::getElapsedTime(copyFdTableC)+ OpenCL::getElapsedTime(copyY) +
							  OpenCL::getElapsedTime(copyCr) + OpenCL::getElapsedTime(copyCb);

	//Profiling for Hoffman coding and RLC
	Core::TimeSpan cpuStart = Core::getCurrentTime();

	//Breaking the image into 64px blocks to perform hoff and RLC
	SWORD DCY=0,DCCb=0,DCCr=0; //DC coefficients used for differential encoding
	WORD xpos,ypos,x,y;
	for (ypos=0;ypos<Yimage;ypos+=8)
		for (xpos=0;xpos<Ximage;xpos+=8)
		{
			DWORD location=ypos*Ximage+xpos;
			SWORD YDU[64],CbDU[64],CrDU[64];

			int pos = 0;
			for (y=0;y<8;y++)
			    {
			        for (x=0;x<8;x++)
			        {
			        	YDU[pos] = YDuDct[location];
			        	CrDU[pos] = CrDuDct[location];
			        	CbDU[pos] = CbDuDct[location];
			        	pos++;
			        	location++;

			        }
			        location+=Ximage-8;
			    }
			//Perform RLC and Hoff on each block of 64px
			jpegCodingAndFileCreation(YDU, &DCY, YDC_HT, YAC_HT);
			jpegCodingAndFileCreation(CbDU, &DCCb,CbDC_HT, CbAC_HT);
			jpegCodingAndFileCreation(CrDU, &DCCr,CbDC_HT, CbAC_HT);
		}

	/////////////////////////////////////
		///Printing performance stat///
	Core::TimeSpan cpuEnd = Core::getCurrentTime();
	Core::TimeSpan gpuCpuTime = cpuEnd - cpuStart;
	printPerformanceHeader();
	printPerformance("Formula Execution", gpuTime, copyTime, dctCpu);
	printPerformance("Hoff & RLC On CPU", gpuCpuTime, Core::TimeSpan::fromSeconds(0), Core::TimeSpan::fromSeconds(0), false);
	gpuTime = gpuTime+gpuCpuTime;
	printPerformance("Full Solution", gpuTime, copyTime, cpuTime);


}
//##########################################################################//
//##########################Library Code Starts############################//
//##########################################################################//

void writebits(bitstring bs)
// A portable version; it should be done in assembler
{
    WORD value;
    SBYTE posval;//bit position in the bitstring we read, should be<=15 and >=0
    value=bs.value;
    posval=bs.length-1;
    while (posval>=0)
    {
        if (value & mask[posval]) bytenew|=mask[bytepos];
        posval--;bytepos--;
        if (bytepos<0) { if (bytenew==0xFF) {writebyte(0xFF);writebyte(0);}
        else {writebyte(bytenew);}
		    bytepos=7;bytenew=0;
        }
    }
}

void closeJpegFile(bitstring fillbits){
	bytenew=0;
		bytepos=7;

	 	if (bytepos>=0)
		    {
		        fillbits.length=bytepos+1;
		        fillbits.value=(1<<(bytepos+1))-1;
		        writebits(fillbits);
		    }
		writeword(0xFFD9); //EOI
		fclose(fp_jpeg_stream);
}


void exitmessage(std::string error_message)
{
    printf("%s\n",error_message.c_str());
    exit(EXIT_FAILURE);
}

//////////////////////////////////////////////////////////////////////////////
////////////////////////  CPU Implementation Start  /////////////////////////


void jpegEncoderCpu(){
SWORD DCY=0,DCCb=0,DCCr=0; //DC coefficients used for differential encoding
    WORD xpos,ypos;
    for (ypos=0;ypos<Yimage;ypos+=8){
        for (xpos=0;xpos<Ximage;xpos+=8)
        {
           load_data_units_from_RGB_buffer(xpos,ypos);
           process_DU(YDU,fdtbl_Y,&DCY,YDC_HT,YAC_HT);
           process_DU(CbDU,fdtbl_Cb,&DCCb,CbDC_HT,CbAC_HT);
           process_DU(CrDU,fdtbl_Cb,&DCCr,CbDC_HT,CbAC_HT);
        }
    }


}

////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////// DCT and Quantization/////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

void process_DU(SBYTE *ComponentDU,float *fdtbl,SWORD *DC,
                bitstring *HTDC,bitstring *HTAC)
{

	Core::TimeSpan startDCTCpu = Core::getCurrentTime();

	fdct_and_quantization(ComponentDU,fdtbl,DU_DCT);

	Core::TimeSpan endDCTCpu = Core::getCurrentTime();
	dctCpu =dctCpu+ endDCTCpu-startDCTCpu;

   //zigzag reorder
	BYTE i;
    for (i=0;i<=63;i++) {
        DU[zigzag[i]]=DU_DCT[i];
    }

    bitstring EOB=HTAC[0x00];
	bitstring M16zeroes=HTAC[0xF0];
	BYTE startpos;
	BYTE end0pos;
	BYTE nrzeroes;
	BYTE nrmarker;
	SWORD Diff;

    Diff=DU[0]-*DC;
    *DC=DU[0];
    //Encode DC
    if (Diff==0)
        writebits(HTDC[0]); //Diff might be 0
    else {writebits(HTDC[category[Diff]]);
        writebits(bitcode[Diff]);
    }
    //Encode ACs
    for (end0pos=63;(end0pos>0)&&(DU[end0pos]==0);end0pos--) ;
    //end0pos = first element in reverse order !=0
    if (end0pos==0) {
        writebits(EOB);
        return;
    }

    i=1;
    while (i<=end0pos)
    {
        startpos=i;
        for (; (DU[i]==0)&&(i<=end0pos);i++) ;
        nrzeroes=i-startpos;
        if (nrzeroes>=16) {
            for (nrmarker=1;nrmarker<=nrzeroes/16;nrmarker++) {
                writebits(M16zeroes);
            }
            nrzeroes=nrzeroes%16;
        }
        writebits(HTAC[nrzeroes*16+category[DU[i]]]);
        writebits(bitcode[DU[i]]);
        i++;
    }
    if (end0pos!=63)
        writebits(EOB);
}
////////////////////////////////////////////////////////////////////////////////////////////////

void fdct_and_quantization(SBYTE *data,float *fdtbl,SWORD *outdata)
{
    float tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    float tmp10, tmp11, tmp12, tmp13;
    float z1, z2, z3, z4, z5, z11, z13;
    float *dataptr;
    float datafloat[64];
    float temp;
    SBYTE ctr;
    BYTE i;
    for (i=0;i<64;i++)
        datafloat[i]=data[i];

    // Pass 1: process rows.
    dataptr=datafloat;
    for (ctr = 7; ctr >= 0; ctr--) {
        tmp0 = dataptr[0] + dataptr[7];
        tmp7 = dataptr[0] - dataptr[7];
        tmp1 = dataptr[1] + dataptr[6];
        tmp6 = dataptr[1] - dataptr[6];
        tmp2 = dataptr[2] + dataptr[5];
        tmp5 = dataptr[2] - dataptr[5];
        tmp3 = dataptr[3] + dataptr[4];
        tmp4 = dataptr[3] - dataptr[4];

        // Even part

        tmp10 = tmp0 + tmp3;	// phase 2
        tmp13 = tmp0 - tmp3;
        tmp11 = tmp1 + tmp2;
        tmp12 = tmp1 - tmp2;

        dataptr[0] = tmp10 + tmp11; // phase 3
        dataptr[4] = tmp10 - tmp11;

        z1 = (tmp12 + tmp13) * ((float) 0.707106781); // c4
        dataptr[2] = tmp13 + z1;	// phase 5
        dataptr[6] = tmp13 - z1;

        // Odd part

        tmp10 = tmp4 + tmp5;	// phase 2
        tmp11 = tmp5 + tmp6;
        tmp12 = tmp6 + tmp7;

        // The rotator is modified from fig 4-8 to avoid extra negations
        z5 = (tmp10 - tmp12) * ((float) 0.382683433); // c6
        z2 = ((float) 0.541196100) * tmp10 + z5; // c2-c6
        z4 = ((float) 1.306562965) * tmp12 + z5; // c2+c6
        z3 = tmp11 * ((float) 0.707106781); // c4

        z11 = tmp7 + z3;		// phase 5
        z13 = tmp7 - z3;

        dataptr[5] = z13 + z2;	// phase 6
        dataptr[3] = z13 - z2;
        dataptr[1] = z11 + z4;
        dataptr[7] = z11 - z4;

        dataptr += 8;		//advance pointer to next row
    }

    // Pass 2: process columns

    dataptr = datafloat;
    for (ctr = 7; ctr >= 0; ctr--) {
        tmp0 = dataptr[0] + dataptr[56];
        tmp7 = dataptr[0] - dataptr[56];
        tmp1 = dataptr[8] + dataptr[48];
        tmp6 = dataptr[8] - dataptr[48];
        tmp2 = dataptr[16] + dataptr[40];
        tmp5 = dataptr[16] - dataptr[40];
        tmp3 = dataptr[24] + dataptr[32];
        tmp4 = dataptr[24] - dataptr[32];

        //Even part/

        tmp10 = tmp0 + tmp3;	//phase 2
        tmp13 = tmp0 - tmp3;
        tmp11 = tmp1 + tmp2;
        tmp12 = tmp1 - tmp2;

        dataptr[0] = tmp10 + tmp11; // phase 3
        dataptr[32] = tmp10 - tmp11;

        z1 = (tmp12 + tmp13) * ((float) 0.707106781); // c4
        dataptr[16] = tmp13 + z1; // phase 5
        dataptr[48] = tmp13 - z1;

        // Odd part

        tmp10 = tmp4 + tmp5;	// phase 2
        tmp11 = tmp5 + tmp6;
        tmp12 = tmp6 + tmp7;

        // The rotator is modified from fig 4-8 to avoid extra negations.
        z5 = (tmp10 - tmp12) * ((float) 0.382683433); // c6
        z2 = ((float) 0.541196100) * tmp10 + z5; // c2-c6
        z4 = ((float) 1.306562965) * tmp12 + z5; // c2+c6
        z3 = tmp11 * ((float) 0.707106781); // c4

        z11 = tmp7 + z3;		// phase 5
        z13 = tmp7 - z3;
        dataptr[40] = z13 + z2; // phase 6
        dataptr[24] = z13 - z2;
        dataptr[8] = z11 + z4;
        dataptr[56] = z11 - z4;

        dataptr++;			// advance pointer to next column
    }
    // Quantize/descale the coefficients, and store into output array
    for (i = 0; i < 64; i++) {
        // Apply the quantization and scaling factor
        temp = datafloat[i] * fdtbl[i];
//        outputFile << temp<< "\t";
        //Round to nearest integer.
        //Since C does not specify the direction of rounding for negative
        //quotients, we have to force the dividend positive for portability.
        //The maximum coefficient size is +-16K (for 12-bit data), so this
        //code should work for either 16-bit or 32-bit ints.
        outdata[i] = (SWORD) ((SWORD)(temp + 16384.5) - 16384);
    }

}

////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////

void load_data_units_from_RGB_buffer(WORD xpos,WORD ypos)
{
    BYTE x,y;
    BYTE pos=0;
    DWORD location;
    BYTE R,G,B;
    location=ypos*Ximage+xpos;
    for (y=0;y<8;y++)
    {
        for (x=0;x<8;x++)
        {
            R=RGB_buffer[location].R;
            G=RGB_buffer[location].G;
            B=RGB_buffer[location].B;
            YDU[pos]=Y(R,G,B);
            CbDU[pos]=Cb(R,G,B);
            CrDU[pos]=Cr(R,G,B);
            location++;
            pos++;
        }
        location+=Ximage-8;
    }
}

//////////////////////////////////////////////////////////////////////////////////////
/////////////////////Start of Initialization seq/////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////



void set_DQTinfo()
{
    int scalefactor=4;// scalefactor controls the visual quality of the image
    // the smaller is, the better image we'll get, and the smaller
    // compression we'll achieve
    DQTinfo.marker=0xFFDB;
    DQTinfo.length=132;
    DQTinfo.QTYinfo=0;
    DQTinfo.QTCbinfo=1;
    set_quant_table(std_luminance_qt,scalefactor,DQTinfo.Ytable);
    set_quant_table(std_chrominance_qt,scalefactor,DQTinfo.Cbtable);
}

void write_APP0info()
//Nothing to overwrite for APP0info
{
    writeword(APP0info.marker);
    writeword(APP0info.length);
    writebyte('J');writebyte('F');writebyte('I');writebyte('F');writebyte(0);
    writebyte(APP0info.versionhi);writebyte(APP0info.versionlo);
    writebyte(APP0info.xyunits);
    writeword(APP0info.xdensity);writeword(APP0info.ydensity);
    writebyte(APP0info.thumbnwidth);writebyte(APP0info.thumbnheight);
}

void write_SOF0info()
// We should overwrite width and height
{
    writeword(SOF0info.marker);
    writeword(SOF0info.length);
    writebyte(SOF0info.precision);
    writeword(SOF0info.height);writeword(SOF0info.width);
    writebyte(SOF0info.nrofcomponents);
    writebyte(SOF0info.IdY);writebyte(SOF0info.HVY);writebyte(SOF0info.QTY);
    writebyte(SOF0info.IdCb);writebyte(SOF0info.HVCb);writebyte(SOF0info.QTCb);
    writebyte(SOF0info.IdCr);writebyte(SOF0info.HVCr);writebyte(SOF0info.QTCr);
}

void write_DQTinfo()
{
    BYTE i;
    writeword(DQTinfo.marker);
    writeword(DQTinfo.length);
    writebyte(DQTinfo.QTYinfo);
    for (i=0;i<64;i++) writebyte(DQTinfo.Ytable[i]);
    writebyte(DQTinfo.QTCbinfo);
    for (i=0;i<64;i++) writebyte(DQTinfo.Cbtable[i]);
}

void set_quant_table(BYTE *basic_table,int scale_factor,BYTE *newtable)
// Set quantization table and zigzag reorder it
{
    BYTE i;
    long temp;
    for (i = 0; i < 64; i++) {
        temp = ((long) basic_table[i] /scale_factor);
        //limit the values to the valid range
        if (temp <= 0L) temp = 1L;
        if (temp > 255L) temp = 255L; //limit to baseline range if requested
        newtable[zigzag[i]] = (BYTE) temp;
    }
}


void write_DHTinfo()
{
    BYTE i;
    writeword(DHTinfo.marker);
    writeword(DHTinfo.length);
    writebyte(DHTinfo.HTYDCinfo);
    for (i=0;i<16;i++)  writebyte(DHTinfo.YDC_nrcodes[i]);
    for (i=0;i<=11;i++) writebyte(DHTinfo.YDC_values[i]);
    writebyte(DHTinfo.HTYACinfo);
    for (i=0;i<16;i++)  writebyte(DHTinfo.YAC_nrcodes[i]);
    for (i=0;i<=161;i++) writebyte(DHTinfo.YAC_values[i]);
    writebyte(DHTinfo.HTCbDCinfo);
    for (i=0;i<16;i++)  writebyte(DHTinfo.CbDC_nrcodes[i]);
    for (i=0;i<=11;i++)  writebyte(DHTinfo.CbDC_values[i]);
    writebyte(DHTinfo.HTCbACinfo);
    for (i=0;i<16;i++)  writebyte(DHTinfo.CbAC_nrcodes[i]);
    for (i=0;i<=161;i++) writebyte(DHTinfo.CbAC_values[i]);
}

void set_DHTinfo()
{
    BYTE i;
    DHTinfo.marker=0xFFC4;
    DHTinfo.length=0x01A2;
    DHTinfo.HTYDCinfo=0;
    for (i=0;i<16;i++)  DHTinfo.YDC_nrcodes[i]=std_dc_luminance_nrcodes[i+1];
    for (i=0;i<=11;i++)  DHTinfo.YDC_values[i]=std_dc_luminance_values[i];
    DHTinfo.HTYACinfo=0x10;
    for (i=0;i<16;i++)  DHTinfo.YAC_nrcodes[i]=std_ac_luminance_nrcodes[i+1];
    for (i=0;i<=161;i++) DHTinfo.YAC_values[i]=std_ac_luminance_values[i];
    DHTinfo.HTCbDCinfo=1;
    for (i=0;i<16;i++)  DHTinfo.CbDC_nrcodes[i]=std_dc_chrominance_nrcodes[i+1];
    for (i=0;i<=11;i++)  DHTinfo.CbDC_values[i]=std_dc_chrominance_values[i];
    DHTinfo.HTCbACinfo=0x11;
    for (i=0;i<16;i++)  DHTinfo.CbAC_nrcodes[i]=std_ac_chrominance_nrcodes[i+1];
    for (i=0;i<=161;i++) DHTinfo.CbAC_values[i]=std_ac_chrominance_values[i];
}

void write_SOSinfo()
//Nothing to overwrite for SOSinfo
{
    writeword(SOSinfo.marker);
    writeword(SOSinfo.length);
    writebyte(SOSinfo.nrofcomponents);
    writebyte(SOSinfo.IdY);writebyte(SOSinfo.HTY);
    writebyte(SOSinfo.IdCb);writebyte(SOSinfo.HTCb);
    writebyte(SOSinfo.IdCr);writebyte(SOSinfo.HTCr);
    writebyte(SOSinfo.Ss);writebyte(SOSinfo.Se);writebyte(SOSinfo.Bf);
}

void write_comment(BYTE *comment)
{
    WORD i,length;
    writeword(0xFFFE); //The COM marker
    length=(WORD)strlen((const char *)comment);
    writeword(length+2);
    for (i=0;i<length;i++) writebyte(comment[i]);
}


void compute_Huffman_table(BYTE *nrcodes,BYTE *std_table,bitstring *HT)
{
    BYTE k,j;
    BYTE pos_in_table;
    WORD codevalue;
    codevalue=0;
    pos_in_table=0;
    for (k=1;k<=16;k++)
    {
        for (j=1;j<=nrcodes[k];j++) {
            HT[std_table[pos_in_table]].value=codevalue;
            HT[std_table[pos_in_table]].length=k;
            pos_in_table++;
            codevalue++;
        }
        codevalue*=2;
    }
}
void init_Huffman_tables()
{
    compute_Huffman_table(std_dc_luminance_nrcodes,std_dc_luminance_values,YDC_HT);
    compute_Huffman_table(std_dc_chrominance_nrcodes,std_dc_chrominance_values,CbDC_HT);
    compute_Huffman_table(std_ac_luminance_nrcodes,std_ac_luminance_values,YAC_HT);
    compute_Huffman_table(std_ac_chrominance_nrcodes,std_ac_chrominance_values,CbAC_HT);
}


void set_numbers_category_and_bitcode()
{
    SDWORD nr;
    SDWORD nrlower,nrupper;
    BYTE cat;

    category_alloc=(BYTE *)malloc(65535*sizeof(BYTE));
    if (category_alloc==NULL) exitmessage("Not enough memory.");
    category=category_alloc+32767; //allow negative subscripts
    bitcode_alloc=(bitstring *)malloc(65535*sizeof(bitstring));
    if (bitcode_alloc==NULL) exitmessage("Not enough memory.");
    bitcode=bitcode_alloc+32767;
    nrlower=1;nrupper=2;
    for (cat=1;cat<=15;cat++) {
        //Positive numbers
        for (nr=nrlower;nr<nrupper;nr++)
        { category[nr]=cat;
            bitcode[nr].length=cat;
            bitcode[nr].value=(WORD)nr;
        }
        //Negative numbers
        for (nr=-(nrupper-1);nr<=-nrlower;nr++)
        { category[nr]=cat;
            bitcode[nr].length=cat;
            bitcode[nr].value=(WORD)(nrupper-1+nr);
        }
        nrlower<<=1;
        nrupper<<=1;
    }
}

void precalculate_YCbCr_tables()
{
    WORD R,G,B;
    for (R=0;R<=255;R++) {
        YRtab[R]=(SDWORD)(65536*0.299+0.5)*R;
        CbRtab[R]=(SDWORD)(65536*-0.16874+0.5)*R;
        CrRtab[R]=(SDWORD)(32768)*R;
    }
    for (G=0;G<=255;G++) {
        YGtab[G]=(SDWORD)(65536*0.587+0.5)*G;
        CbGtab[G]=(SDWORD)(65536*-0.33126+0.5)*G;
        CrGtab[G]=(SDWORD)(65536*-0.41869+0.5)*G;
    }
    for (B=0;B<=255;B++) {
        YBtab[B]=(SDWORD)(65536*0.114+0.5)*B;
        CbBtab[B]=(SDWORD)(32768)*B;
        CrBtab[B]=(SDWORD)(65536*-0.08131+0.5)*B;
    }
}

// Using a bit modified form of the FDCT routine from IJG's C source:
// Forward DCT routine idea taken from Independent JPEG Group's C source for
// JPEG encoders/decoders

// For float AA&N IDCT method, divisors are equal to quantization
//   coefficients scaled by scalefactor[row]*scalefactor[col], where
//   scalefactor[0] = 1
//   scalefactor[k] = cos(k*PI/16) * sqrt(2)    for k=1..7
//   We apply a further scale factor of 8.
//   What's actually stored is 1/divisor so that the inner loop can
//   use a multiplication rather than a division.
void prepare_quant_tables()
{
    double aanscalefactor[8] = {1.0, 1.387039845, 1.306562965, 1.175875602,
        1.0, 0.785694958, 0.541196100, 0.275899379};
    BYTE row, col;
    BYTE i = 0;
    for (row = 0; row < 8; row++)
    {
        for (col = 0; col < 8; col++)
        {
            fdtbl_Y[i] = (float) (1.0 / ((double) DQTinfo.Ytable[zigzag[i]] * aanscalefactor[row] * aanscalefactor[col] * 8.0));
            fdtbl_Cb[i] = (float) (1.0 / ((double) DQTinfo.Cbtable[zigzag[i]] * aanscalefactor[row] * aanscalefactor[col] * 8.0));

            i++;
        }
    }
}



void initNewJpegFile(int Ximage_original, int Yimage_original){

	SOF0info.width=Ximage_original;
	SOF0info.height=Yimage_original;
	writeword(0xFFD8); //SOI
	write_APP0info();

	write_DQTinfo();
	write_SOF0info();
	write_DHTinfo();
	write_SOSinfo();
}

void init_all( )
{
    set_DQTinfo();
    set_DHTinfo();
    init_Huffman_tables();
    set_numbers_category_and_bitcode();
    precalculate_YCbCr_tables();
    prepare_quant_tables();
}
//##########################################################################//
//##########################Library Code ENDS############################//
//##########################################################################//

////////////////////////////////////////////////////////////////////////
//############## Code adapted from to suit our needs ###############//
//http://www.xbdev.net/image_formats/jpeg/050_dec_enc_demo/index.php//

//////////////////LOAD Bitmap Image to Local Buffer///////////////////


void load_bitmap(char *bitmap_name, WORD *Ximage_original, WORD *Yimage_original)
{

	WORD Xdiv8,Ydiv8;
	    BYTE nr_fillingbytes;//The number of the filling bytes in the BMP file
	    // (the dimension in bytes of a BMP line on the disk is divisible by 4)
	    colorRGB lastcolor;
	    WORD column;
	    BYTE TMPBUF[256];
	    WORD nrline_up,nrline_dn,nrline;
	    WORD dimline;
	    colorRGB *tmpline;
	    FILE *fp_bitmap=fopen(bitmap_name,"r");
	    if (fp_bitmap==NULL) exitmessage("Cannot open bitmap file.File not found ?");
	    if (fread(TMPBUF,1,54,fp_bitmap)!=54)
	        exitmessage("Need a truecolor BMP to encode.");
	    if ((TMPBUF[0]!='B')||(TMPBUF[1]!='M')||(TMPBUF[28]!=24))
	        exitmessage("Need a truecolor BMP to encode.");


	    Ximage=(WORD)TMPBUF[19]*256+(WORD)TMPBUF[18]; //calculate dimensions of original image (BMP standard)
	    Yimage=(WORD)TMPBUF[23]*256+(WORD)TMPBUF[22];
	    *Ximage_original=Ximage;
	    *Yimage_original=Yimage; //Keep the old dimensions of the image

	    if (Ximage%8!=0){
	        Xdiv8=(Ximage/8)*8+8;
	    }
		else{
	        Xdiv8=Ximage;
	    }

	    if (Yimage%8!=0)
	        {Ydiv8=(Yimage/8)*8+8;
	    }
		else {
	     Ydiv8=Yimage;
	    }
	    // The image we encode shall be filled with the last line and the last column
	    // from the original bitmap, until Ximage and Yimage are divisible by 8
	    // Load BMP image from disk and complete X

	    RGB_buffer=(colorRGB *)(malloc(3*Xdiv8*Ydiv8));

	    if (RGB_buffer==NULL)
	        exitmessage("Not enough memory for the bitmap image.");

	    if (Ximage%4!=0)
	        nr_fillingbytes=4-(Ximage%4);

	    else nr_fillingbytes=0;

	    for (nrline=0;nrline<Yimage;nrline++)
	    {
	        fread(RGB_buffer+nrline*Xdiv8,1,Ximage*3,fp_bitmap);
	        fread(TMPBUF,1,nr_fillingbytes,fp_bitmap);
	        memcpy(&lastcolor,RGB_buffer+nrline*Xdiv8+Ximage-1,3);
	        for (column=Ximage;column<Xdiv8;column++)
	        {memcpy(RGB_buffer+nrline*Xdiv8+column,&lastcolor,3);}
	    }
	    Ximage=Xdiv8;
	    dimline=Ximage*3;
	    tmpline=(colorRGB *)malloc(dimline);
	    if (tmpline==NULL) exitmessage("Not enough memory.");
	    //Reorder in memory the inversed bitmap
	    for (nrline_up=Yimage-1,nrline_dn=0;nrline_up>nrline_dn;nrline_up--,nrline_dn++)
	    {
	        memcpy(tmpline,RGB_buffer+nrline_up*Ximage, dimline);
	        memcpy(RGB_buffer+nrline_up*Ximage,RGB_buffer+nrline_dn*Ximage,dimline);
	        memcpy(RGB_buffer+nrline_dn*Ximage,tmpline,dimline);
	    }
	    // Y completion:
	    memcpy(tmpline,RGB_buffer+(Yimage-1)*Ximage,dimline);
	    for (nrline=Yimage;nrline<Ydiv8;nrline++)
	    {memcpy(RGB_buffer+nrline*Ximage,tmpline,dimline);}
	    Yimage=Ydiv8;
	    free(tmpline);
	    fclose(fp_bitmap);

}


//////////////////////////////////////////////////////////////////////////////
// Main function
//////////////////////////////////////////////////////////////////////////////

int main(int argc, char** argv) {
	// Create a context


	char BMP_filename[64] = "bitmapfile.bmp";
	char JPG_filename[64] = "jpegfileCPU.jpg";
	char JPG_filename2[64] = "jpegfileGPU.jpg";



	WORD Ximage_original,Yimage_original;	//the original image dimensions,

	bitstring fillbits; //filling bitstring for the bit alignment of the EOI marker

	load_bitmap(BMP_filename, &Ximage_original, &Yimage_original);
	fp_jpeg_stream=fopen(JPG_filename,"wb");

	init_all();

	initNewJpegFile(Ximage_original, Yimage_original);

	Core::TimeSpan cpuStart = Core::getCurrentTime();
	jpegEncoderCpu();
	Core::TimeSpan cpuEnd = Core::getCurrentTime();

	Core::TimeSpan cpuTime = cpuEnd - cpuStart;

	closeJpegFile(fillbits);

	fp_jpeg_stream=fopen(JPG_filename2,"wb");
	initNewJpegFile(Ximage_original, Yimage_original);

	jpegEncoderGPU (argc, argv, cpuTime );

	closeJpegFile(fillbits);
	free(RGB_buffer);
	free(category_alloc);
	free(bitcode_alloc);

	std::cout<<"Success"<<std::endl;
	return 0;
}

