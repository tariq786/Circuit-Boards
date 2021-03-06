/*********************************************************************************

 Copyright(c) 2012 Analog Devices, Inc. All Rights Reserved.

 This software is proprietary and confidential.  By using this software you agree
 to the terms of the associated Analog Devices License Agreement.

 *********************************************************************************/

/*****************************************************************************
 * 512_Point_FFT.c
 *****************************************************************************/
/*include files */

#include <stdio.h>     /* Get declaration of puts and definition of NULL. */
#include <builtins.h>  /* Get definitions of compiler builtin functions */
#include <platform_include.h>      /* System and IOP register bit and address definitions. */
#include <processor_include.h>
#include <services/int/adi_int.h>  /* Interrupt Handler API header. */
#include "adi_initialize.h"
#define PCI 0x80000
#define OFFSET_MASK 0x7FFFF
#define COEFFDATA 0x100000

extern void initPLL(void);
extern void initExternalMemory(void);

static float CoeffdataV[64] = {
						#include "twiddle32complx.dat"
						};  /*2V values*/
static float CoeffdataH[32] = {
						#include "twiddle16complx.dat"
						};  /*2H values*/

volatile float input[1024] = {
						#include"inputcomplx512.dat"
						}; /* The input must have real and complex packed for large FFTs.*/

volatile float VFFTBuffer[1024];    /* Buffer to hold vertical FFT output*/

volatile float SpeclProdBuffer[1024]; /* Buffer to hold product of special Twiddles and VFFT output*/

volatile float output[1024];      /*Final FFT output*/

static float SpeclCoeff[2048] = {
							#include "SpecialTwiddles2048.dat"
						}; /*special Coeff*/

/* VFFT TCBs */

int coeffTCB_V[6] = {0,0,0,64,1,(int)CoeffdataV};   /* Vertical coeff TCB*/


int inputTCB[6]   = {0,(int)input,1023,1023,32,(int)input};      /* input TCB modify is 2H*/
int inputTCBChain[6] = {0,0,0,1,1,((int)input+1023)}; /* input TCB chain*/

int VFFTRecvTCB[6]  = {0,0,0,1024,1,(int)VFFTBuffer};  /* Vertical FFT receive buffer.*/

/*Special Prod TCBs*/
/* First TCB */
int SpeclCoeffTCB1[6] = {0,0,0,512,1,(int)SpeclCoeff}; /* SpecialCoeffTCB of 256 complex words*/
int VFFTTxTCB1[6] =  {0,0,0,256,1,(int)VFFTBuffer};       /* VerticalFFT transmit TCB of 128 complex words*/

int SpeclProdRecv1[6] = {0,0,0,256,1,(int)SpeclProdBuffer};  /* Special product (VFFT* special twiddles)receive TCB*/

/* Second TCB */
int SpeclCoeffTCB2[6] = {0,0,0,512,1,(int)SpeclCoeff+512}; /* SpecialCoeffTCB of 256 complex words*/
int VFFTTxTCB2[6] =  {0,0,0,256,1,((int)VFFTBuffer+256)};       /* VerticalFFT transmit TCB of 128 complex words*/

int SpeclProdRecv2[6] = {0,0,0,256,1,((int)SpeclProdBuffer+256)};  /* Special product (VFFT* special twiddles)receive TCB*/

/* Third TCB */
int SpeclCoeffTCB3[6] = {0,0,0,512,1,((int)SpeclCoeff+1024)}; /* SpecialCoeffTCB of 256 complex words*/
int VFFTTxTCB3[6] =  {0,0,0,256,1,((int)VFFTBuffer+512)};       /* VerticalFFT transmit TCB of 128 complex words*/

int SpeclProdRecv3[6] = {0,0,0,256,1,((int)SpeclProdBuffer+512)};  /* Special product (VFFT* special twiddles)receive TCB*/

/* Fourth TCB */
int SpeclCoeffTCB4[6] = {0,0,0,512,1,((int)SpeclCoeff+1536)}; /* SpecialCoeffTCB of 256 complex words*/
int VFFTTxTCB4[6] =  {0,0,0,256,1,((int)VFFTBuffer+768)};       /* VerticalFFT transmit TCB of 128 complex words*/

int SpeclProdRecv4[6] = {0,0,0,256,1,((int)SpeclProdBuffer+768)};  /* Special product (VFFT* special twiddles)receive TCB*/

/* HFFT TCBs */

int coeffTCB_H[6] = {0,0,0,32,1,(int)CoeffdataH};   /*  Horizontal coeff TCB*/

int SpeclProdTxTCB[6] = {0,(int)SpeclProdBuffer,1023,1023,64,(int)SpeclProdBuffer}; /* Special product transmit buffer. modify is 2V*/
int SpeclProdTxTCBChain[6] = {0,0,0,1,1,((int)SpeclProdBuffer+1023)}; /* Special product transmit buffer.*/

int FinalFFTRcvTCB[6] = {0,(int)output,1023,1023,64, (int)output};
int FinalFFTRcvTCBChain[6] = {0,0,0,1,1, ((int)output+1023)};


int main(void)
{
	int temp;
	/* Initialize managed drivers and/or services at the start of main(). */
	adi_initComponents();

	/* Initialize SHARC PLL*/
	initPLL();

	/* Initialize DDR2 SDRAM controller to access memory */
	//initExternalMemory();

	/* Selecting FFT accelerator */
	*pPMCTL1&=~(BIT_17|BIT_18);
	*pPMCTL1|=FFTACCSEL;

	/*PMCTL1 effect latency*/
	asm("nop;nop;nop;nop;");

	*pFFTCTL2=VDIMDIV16_2|FFT_LOG2VDIM_5|HDIMDIV16_1|FFT_LOG2HDIM_4|NOVER256_2; /*Program VDIM and HDIM*/
 
	
	*pFFTCTL1=FFT_EN|FFT_START; /* Enable FFT.*/



	/* Step 4 and 5 dataTCB(inputTCB) chained to coeff TCB (coeffTCB_V)*/
	temp = ((int)inputTCB +5) & OFFSET_MASK;
	coeffTCB_V[0] = temp;  /* chain pointer of coeff TCB points to data TCB.*/

	temp = ((int)inputTCBChain +5) & OFFSET_MASK;
	inputTCB[0] = temp; /* Chain the last imaginary data to the input data.*/

	temp = (int)coeffTCB_V+5 + COEFFDATA ; /* set bit 20 in the CP register if next TCB is coeff TCB and bit 19 to interrupt after coeff DMA.*/
	*pCPIFFT = temp; /* CP pointing to the CoeffTCB_V.*/

	/* Step 6 Enable VFFT receive DMA */
	temp =  (((int)VFFTRecvTCB+5) & OFFSET_MASK);  /* starts VFFT receive DMA.*/
	*pCPOFFT = temp;

/*********** Special Product DMA (first 256 data)****************/



	/* Step 7 and 8 dataTCB(VFFTTxTCB) chained to special coefficient TCB (SpeclCoeffTCB1)*/

	temp = ((int)VFFTTxTCB1 + 5) & OFFSET_MASK;  /* Chain pointer of coeff TCB point to VFFTTxTCB1*/
	SpeclCoeffTCB1[0] = temp;


	temp = (int)SpeclCoeffTCB1 + 5 + COEFFDATA ;/* set bit 20 in the CP register if next TCB is coeff TCB and bit 19 to interrupt after coeff DMA.*/
	*pCPIFFT = temp; /* Start coeff DMA(SpeclCoeffTCB1) followed by VFFT DMA(VFFTTxTCB1)*/



	while((*pFFTDMASTAT & ODMACHIRPT)==0); /* Tests if VFFT receive DMA is completed or not */

	/* Step 9 Enable Special product receive DMA */

	temp = (int)SpeclProdRecv1 +5;     /* starts special product receive DMA.*/
	*pCPOFFT = temp;



/******************************* Special Product DMA (second 256 data) (step 10) *************************/



	/* Step 7 and 8 dataTCB(VFFTTxTCB) chained to special coefficient TCB (SpeclCoeffTCB1)*/
	temp = ((int)VFFTTxTCB2 + 5) & OFFSET_MASK;  /* Chain pointer of coeff TCB point to VFFTTxTCB1*/
	SpeclCoeffTCB2[0] = temp;

	temp = (int)SpeclCoeffTCB2 + 5 + COEFFDATA; /* set bit 20 in the CP register if next TCB is coeff TCB and bit 19 to interrupt after coeff DMA.*/
	*pCPIFFT = temp ;/* Start coeff DMA(SpeclCoeffTCB1) followed by VFFT DMA(VFFTTxTCB1)*/


	while((*pFFTDMASTAT & ODMACHIRPT)==0); /* Tests if special product receive DMA is completed or not */
	/* Step 9 Enable Special product receive DMA */


	temp = (int)SpeclProdRecv2 +5;     /* starts special product receive DMA.*/
	*pCPOFFT = temp;


/******************************* Special Product DMA (third 256 data) (step 10) *************************/



	/* Step 7 and 8 dataTCB(VFFTTxTCB) chained to special coefficient TCB (SpeclCoeffTCB1)*/
	temp = ((int)VFFTTxTCB3 + 5) & OFFSET_MASK;  /* Chain pointer of coeff TCB point to VFFTTxTCB1*/
	SpeclCoeffTCB3[0] = temp;

	temp = (int)SpeclCoeffTCB3 + 5 + COEFFDATA; /*set bit 20 in the CP register if next TCB is coeff TCB and bit 19 to interrupt after coeff DMA.*/
	*pCPIFFT = temp ;/* Start coeff DMA(SpeclCoeffTCB1) followed by VFFT DMA(VFFTTxTCB1)*/


	while((*pFFTDMASTAT & ODMACHIRPT)==0); /*Tests if special product receive DMA is completed or not*/
	/* Step 9 Enable Special product receive DMA */


	temp = (int)SpeclProdRecv3 +5;     /* starts special product receive DMA.*/
	*pCPOFFT = temp;

/******************************* Special Product DMA (fourth 256 data) (step 10) *************************/



	/* Step 7 and 8 dataTCB(VFFTTxTCB) chained to special coefficient TCB (SpeclCoeffTCB1)*/
	temp = ((int)VFFTTxTCB4 + 5) & OFFSET_MASK;  /* Chain pointer of coeff TCB point to VFFTTxTCB1*/
	SpeclCoeffTCB4[0] = temp;

	temp = (int)SpeclCoeffTCB4 + 5 + COEFFDATA; /* set bit 20 in the CP register if next TCB is coeff TCB and bit 19 to interrupt after coeff DMA.*/
	*pCPIFFT = temp ;/* Start coeff DMA(SpeclCoeffTCB1) followed by VFFT DMA(VFFTTxTCB1)*/


	while((*pFFTDMASTAT & ODMACHIRPT)==0); /* Tests if special product receive DMA is completed or not*/
	/* Step 9 Enable Special product receive DMA */


	temp = (int)SpeclProdRecv4 +5;     /* starts special product receive DMA.*/
	*pCPOFFT = temp;

/****************************** HFFT Receive DMA ***********************************************/


	/* Step 11 and 12 dataTCB(SpeclProdTxTCB) chained to coeff TCB (coeffTCB_H) */

	temp = ((int)SpeclProdTxTCB +5) & OFFSET_MASK;
	coeffTCB_H[0] = temp;  /* chain pointer of coeff TCB points to data TCB.*/

	temp = ((int)SpeclProdTxTCBChain +5) & OFFSET_MASK;
	SpeclProdTxTCB[0] = temp; /* Chain the last imaginary data to the input data.*/

	temp = (int)coeffTCB_H+5 + COEFFDATA ; /* set bit 20 in the CP register if next TCB is coeff TCB and bit 19 to interrupt after coeff DMA.*/
	*pCPIFFT = temp; /* CP pointing to the CoeffTCB_V.*/



	/* Step 13 Enable HFFT receive DMA */

	temp = ((int)FinalFFTRcvTCBChain +5) & OFFSET_MASK;
	FinalFFTRcvTCB[0] = temp;

	while((*pFFTDMASTAT & ODMACHIRPT)==0); /*Tests if special product receive DMA is completed or not */

	temp =  (((int)FinalFFTRcvTCB+5) & OFFSET_MASK)|PCI ;  /* starts VFFT receive DMA.*/
	*pCPOFFT = temp;

	/* Step 14 Wait for DMA of step 13 to complete */

	
	while((*pFFTDMASTAT & ODMACHIRPT)==0); /*Tests if special product receive DMA is completed or not */
	puts("512 Point FFT done");
	return 0;


}


