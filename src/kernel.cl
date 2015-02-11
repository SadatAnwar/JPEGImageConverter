void fdct_and_quantization(__local float dataptr[4][8][8], int Ximage,__global short* outdata,__constant float* fdtbl);
__constant int zigzag[64]={
	    0, 1, 5, 6,14,15,27,28,
	    2, 4, 7,13,16,26,29,42,
	    3, 8,12,17,25,30,41,43,
	    9,11,18,24,31,40,44,53,
	    10,19,23,32,39,45,52,54,
	    20,22,33,38,46,51,55,60,
	    21,34,37,47,50,56,59,61,
	    35,36,48,49,57,58,62,63
};

struct colorRGB { 
	uchar B,G,R; 
};

__kernel void load_data_units_from_RGB (
			__global struct colorRGB* d_RGBBuffer,
			__constant float* d_fdtbl_YGpu,
			__constant float* d_fdtbl_CGpu, 
			int Ximage,
			__global float* YDuDct,
			__global float* CrDuDct, 
			__global float* CbDuDct,
			int Yimage) {
	
 
	int id = get_global_id(0);
	int local_id = get_local_id(0);
	int block_id = local_id/8;
	int id_in_block = local_id%8;
	
	uchar R,G,B;
	float YRtab;               
	float CbRtab;
	float CrRtab;
	
	float YGtab;
	float CbGtab;
	float CrGtab;
	
	float YBtab;
	float CbBtab;
	float CrBtab;	
			
	__local float YDUGpu[4][8][8];
	__local float CrDUGpu[4][8][8];
	__local float CbDUGpu[4][8][8];
	
	int pos = (id%Ximage)+((id/Ximage)*8*Ximage);
	
	if(pos<(Ximage*Yimage)){
		for(int i= 0 ; i<8 ; i++){
				/* Read RGB data  */
				struct colorRGB color = d_RGBBuffer[pos+i*Ximage];		
				R=color.R;
				G=color.G;
				B=color.B;
				/*Color space transformation*/
				YRtab=(float)(0.299)*R;               
				YGtab=(float)(0.587)*G;
				YBtab=(float)(0.114)*B;
				YDUGpu[block_id][i][id_in_block] = (( (YRtab+YGtab+YBtab))-128);
				
				CbRtab=(-0.16874)*R;
				CbGtab=(-0.33126)*G;
				CbBtab=(0.5)*B;
				CbDUGpu[block_id][i][id_in_block] = (( (CbRtab+CbGtab+CbBtab) ) );
				
				CrRtab=(0.5)*R;
				CrGtab=(-0.41869)*G;
				CrBtab=(-0.08131)*B;
				CrDUGpu[block_id][i][id_in_block] = (( (CrRtab+CrGtab+CrBtab) ) );
	
		}
		barrier(CLK_LOCAL_MEM_FENCE);	
		//DCT, Quantization and Zig Zaging for each color space
		fdct_and_quantization(YDUGpu,Ximage,YDuDct,d_fdtbl_YGpu);
		fdct_and_quantization(CrDUGpu,Ximage,CrDuDct ,d_fdtbl_CGpu);
		fdct_and_quantization(CbDUGpu,Ximage,CbDuDct, d_fdtbl_CGpu);
	}
	
	
}
 void fdct_and_quantization(__local float dataptr[4][8][8], int Ximage,__global short* outdata,__constant float* fdtbl )
{
     float tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
     float tmp10, tmp11, tmp12, tmp13;
     float z1, z2, z3, z4, z5, z11, z13;     
     __local float tempOutdata[4][8][8] ;     
     int id = get_global_id(0);
	 int local_id = get_local_id(0);
	 int block_id = local_id/8;
	 int id_in_block = local_id%8;
     float temp;     
     uchar i;
     int pos ; 
	
     
     /*DCT starts here (Formula adapted from JPEG standard 9a www.ijg.org)*/
     /*Changes made to formula to allow loop unrolling and parallel execution*/
     
     // Pass 1: process rows.
        tmp0 = dataptr[block_id][id_in_block][0] + dataptr[block_id][id_in_block][7];
        tmp7 = dataptr[block_id][id_in_block][0] - dataptr[block_id][id_in_block][7];
        tmp1 = dataptr[block_id][id_in_block][1] + dataptr[block_id][id_in_block][6];
        tmp6 = dataptr[block_id][id_in_block][1] - dataptr[block_id][id_in_block][6];
        tmp2 = dataptr[block_id][id_in_block][2] + dataptr[block_id][id_in_block][5];
        tmp5 = dataptr[block_id][id_in_block][2] - dataptr[block_id][id_in_block][5];
        tmp3 = dataptr[block_id][id_in_block][3] + dataptr[block_id][id_in_block][4];
        tmp4 = dataptr[block_id][id_in_block][3] - dataptr[block_id][id_in_block][4];

        // Even part

        tmp10 = tmp0 + tmp3;	// phase 2
        tmp13 = tmp0 - tmp3;
        tmp11 = tmp1 + tmp2;
        tmp12 = tmp1 - tmp2;
        
        barrier(CLK_LOCAL_MEM_FENCE);
        
        dataptr[block_id][id_in_block][0] = tmp10 + tmp11; // phase 3
        dataptr[block_id][id_in_block][4] = tmp10 - tmp11;

        z1 = (tmp12 + tmp13) * (0.707106781); // c4
        dataptr[block_id][id_in_block][2] = tmp13 + z1;	// phase 5
        dataptr[block_id][id_in_block][6] = tmp13 - z1;

        // Odd part

        tmp10 = tmp4 + tmp5;	// phase 2
        tmp11 = tmp5 + tmp6;
        tmp12 = tmp6 + tmp7;

        // The rotator is modified from fig 4-8 to avoid extra negations
        z5 = (tmp10 - tmp12) * (0.382683433); // c6
        z2 = (0.541196100) * tmp10 + z5; // c2-c6
        z4 = (1.306562965) * tmp12 + z5; // c2+c6
        z3 = tmp11 * (0.707106781); // c4

        z11 = tmp7 + z3;		// phase 5
        z13 = tmp7 - z3;

        dataptr[block_id][id_in_block][5] = z13 + z2;	// phase 6
        dataptr[block_id][id_in_block][3] = z13 - z2;
        dataptr[block_id][id_in_block][1] = z11 + z4;
        dataptr[block_id][id_in_block][7] = z11 - z4;

        barrier(CLK_LOCAL_MEM_FENCE);
       // Pass 2: Process Columns
        
	
        tmp0 = dataptr[block_id][0][id_in_block] + dataptr[block_id][7][id_in_block];
		tmp7 = dataptr[block_id][0][id_in_block] - dataptr[block_id][7][id_in_block];
		tmp1 = dataptr[block_id][1][id_in_block] + dataptr[block_id][6][id_in_block];
		tmp6 = dataptr[block_id][1][id_in_block] - dataptr[block_id][6][id_in_block];
		tmp2 = dataptr[block_id][2][id_in_block] + dataptr[block_id][5][id_in_block];
		tmp5 = dataptr[block_id][2][id_in_block] - dataptr[block_id][5][id_in_block];
		tmp3 = dataptr[block_id][3][id_in_block] + dataptr[block_id][4][id_in_block];
		tmp4 = dataptr[block_id][3][id_in_block] - dataptr[block_id][4][id_in_block];
		
		 //Even part/
		 tmp10 = tmp0 + tmp3;	//phase 2
		 tmp13 = tmp0 - tmp3;
		 tmp11 = tmp1 + tmp2;
		 tmp12 = tmp1 - tmp2;
		
		 barrier(CLK_LOCAL_MEM_FENCE);
		 
		 dataptr[block_id][0][id_in_block] = tmp10 + tmp11; // phase 3
		 dataptr[block_id][4][id_in_block] = tmp10 - tmp11;
		
		 z1 = (tmp12 + tmp13) * (0.707106781); // c4
		 dataptr[block_id][2][id_in_block] = tmp13 + z1; // phase 5
		 dataptr[block_id][6][id_in_block] = tmp13 - z1;
		
		 // Odd part
		 tmp10 = tmp4 + tmp5;	// phase 2
		 tmp11 = tmp5 + tmp6;
		 tmp12 = tmp6 + tmp7;
		
		 // The rotator is modified from fig 4-8 to avoid extra negations.
		 z5 = (tmp10 - tmp12) * (0.382683433); // c6
		 z2 = (0.541196100) * tmp10 + z5; // c2-c6
		 z4 = (1.306562965) * tmp12 + z5; // c2+c6
		 z3 = tmp11 * (0.707106781); // c4
		
		 z11 = tmp7 + z3;		// phase 5
		 z13 = tmp7 - z3;
		 dataptr[block_id][5][id_in_block] = z13 + z2; // phase 6
		 dataptr[block_id][3][id_in_block] = z13 - z2;
		 dataptr[block_id][1][id_in_block] = z11 + z4;
		 dataptr[block_id][7][id_in_block] = z11 - z4;	
			
		 barrier(CLK_LOCAL_MEM_FENCE);
	
		/* id%Ximage = Columns & id/Ximage = Rows & (id/Ximage)*8*Ximage = every 8th row */
		pos = (id%Ximage)+((id/Ximage)*8*Ximage); // pos is the start point of this thread (thread will work on one col
												  // inside current block (of 64 px)
	
		 
		 for (i = 0; i < 8; i++) {
			 // Apply the quantization and scaling factor
			 /*fdtbl[pos%8 + 8*i] is used to calculate the value corresponding to current element in block*/
			 dataptr[block_id][i][id_in_block] = dataptr[block_id][i][id_in_block] * fdtbl[pos%8 + 8*i];
	   }     
		 /*Zig Zaging of the data*/
		 for (i=0;i<8;i++) {
			 int zigzagpos = zigzag[(pos%8) + (8*i)];
			 int col = zigzagpos%8;
			 int row = zigzagpos/8;
			 int newpos =pos%8;
			 tempOutdata[block_id][row][col]=dataptr[block_id][i][id_in_block];
			}
		 
		 for (i=0;i<8;i++) {
			 //Rounding of data +ve numbers rounded to +inf and -ve numbers to -inf
			 //Moving data to global memory to be read by CPU
			 outdata[pos+i*Ximage] =floor(tempOutdata[block_id][i][id_in_block]+0.5);
				
		 }
}



