/* Include core modules */
#include "stm32f4xx.h"
#include "defines.h"

/* Include TM libraries */
#include "tm_stm32f4_adc.h"
#include "tm_stm32f4_dac.h"
#include "tm_stm32f4_delay.h"
#include "tm_stm32f4_disco.h"
#include "tm_stm32f4_ili9341_ltdc.h"
#include "stm32f4xx_spi.h"
#include "tm_stm32f4_fonts.h"


#include <stdio.h>

/* Include mathematic functions */
#include "arm_math.h"

/* FFT configurations */
#define SAMPLES		  	2048          /* 1024 real elements and 1024 imaginary elements */
#define FFT_SIZE	    SAMPLES / 2		/* FFT size is half of samples */

#define FFT_BAR_MAX_HEIGHT		120   /* 120 px on the LCD */

/* Global variables */
float32_t Input[SAMPLES];          /* Float array with 2048 samples from audio stream */
float32_t Output[FFT_SIZE];        /* Float array of 1024 magnitudes of 1024 different frequencies */
float32_t maxMag;                  /* Max frequency magnitude */
uint32_t maxIndex;						     /* Index of max frequency magnitdue */
uint16_t i;                        /* Index for for loop */
float32_t frequency;               /* Frequency of the max magnitude */
float32_t resolution = 44.4;       /* Frequency resolution */

uint32_t adc_val;                  /* Numeric digital output from mic: ADC Supply Voltage = 3.3V with 12B */
uint32_t adc_max_value = 0;        /* Initialize max adc value */
uint32_t adc_min_value = 10000;    /* Initialize min adc value */

uint32_t digital_pp;               /* Peak to peak numeric digital value */
uint16_t digital_pp_scalar = 2000; /* Empirically determined scalar for LED brightness */
float32_t voltage_pp;              /* Peak to peak voltage */

int counter = 0;
int counter2 = 0;
int cycles = 10000;                /* Empirical determined number of iterations to accurately compute max and min ADC values */ 

char voltage_pp_array[20];
char frequency_array[20];


/* Draw bar for LCD */
/* Simple library to draw bars */
void DrawBar(uint16_t bottomX, uint16_t bottomY, uint16_t maxHeight, uint16_t maxMag, float32_t value, uint16_t foreground, uint16_t background) {
	uint16_t height;
	height = (uint16_t)((float32_t)value / (float32_t)maxMag * (float32_t)maxHeight);
	if (height == maxHeight) {
		TM_ILI9341_DrawLine(bottomX, bottomY, bottomX, bottomY - height, foreground);
	} 
	else if (height < maxHeight) {
		TM_ILI9341_DrawLine(bottomX, bottomY, bottomX, bottomY - height, foreground);
		TM_ILI9341_DrawLine(bottomX, bottomY - height, bottomX, bottomY - maxHeight, background);
	}
}


int main(void) {

	arm_cfft_radix4_instance_f32 S;	/* ARM CFFT module */

	/* Initialize system */
	SystemInit();
	
	/* Delay init */
	TM_DELAY_Init();
	
	/* Initialize LED's on board */
	TM_DISCO_LedInit();
	
	/* Initialize LCD */
	TM_ILI9341_Init();
	TM_ILI9341_Rotate(TM_ILI9341_Orientation_Landscape_1);

	/* Initialize ADC channel 0, pin PA0 */
	TM_ADC_Init(ADC1, ADC_Channel_0);
		
	/* Initialize DAC channel 2, pin PA5 */
	TM_DAC_Init(TM_DAC2);
	
	
	while (1) {

		counter2 = 0;
		
		while (counter2 != cycles){
			/* Read ADC mic output: Range 0 - 4095 */
			adc_val = TM_ADC_Read(ADC1, ADC_Channel_0);
			
			/* Store max and min ADC values */
			if (adc_val > adc_max_value){
				adc_max_value = adc_val;
			}
			if (adc_val < adc_min_value){
				adc_min_value = adc_val;
			}
			
			counter++;
			counter2++;
			
			/* Cycles to wait for at least one period of waveform */
			if (counter == cycles){
				/* Compute peak to peak numeric digital value */
				digital_pp = adc_max_value - adc_min_value;
				
				/* Compute voltage peak to peak (mV) and write to DAC */
				voltage_pp = (3.3 / 4096.) * digital_pp * 1000;
				TM_DAC_SetValue(TM_DAC2, (uint16_t)digital_pp + digital_pp_scalar);
				
				/* Display peak to peak voltage on LCD */
				sprintf(voltage_pp_array, "Voltage Pk-Pk (mV) = %.1f", voltage_pp);
				TM_ILI9341_Puts(10, 10, voltage_pp_array, &TM_Font_11x18, ILI9341_COLOR_BLACK, ILI9341_COLOR_BLUE2);

				/* Reset initial conditions for next iteration */
				adc_max_value = 0;
				adc_min_value = 10000;
				counter = 0;
			}
		}
		
		for (i = 0; i < SAMPLES; i += 2) {
			/* Each 21us ~ 45kHz sample rate */
			Delay(21);
				
			/* Real part, must be between -1 and 1 */
			Input[(uint16_t)i] = (float32_t)((float32_t)TM_ADC_Read(ADC1, ADC_Channel_0) - (float32_t)8192.0);
			
			/* Set imaginary elements (every other) to 0 */
			Input[(uint16_t)(i + 1)] = 0;
		}
				
		/* Initialize the CFFT/CIFFT module, intFlag = 0, doBitReverse = 1 */
		arm_cfft_radix4_init_f32(&S, FFT_SIZE, 0, 1);
		
		/* Process the data through the CFFT/CIFFT module */
		arm_cfft_radix4_f32(&S, Input);
		
		/* Process the data through the Complex Magniture Module for calculating the magnitude at each bin */
		arm_cmplx_mag_f32(Input, Output, FFT_SIZE);
		
		/* Set DC frequency to 0 */
		Output[0] = 0;
		
		/* Computes the maxMag and maxIndex */
		arm_max_f32(Output, FFT_SIZE, &maxMag, &maxIndex);
		
		/* Computes the frequency in accordance with the maxIndex and resolution */
		frequency = (float32_t)maxIndex * resolution;
		
		/* Display frequency onto LCD */
		sprintf(frequency_array, "FFT Frequency (Hz) = %.1f", frequency);
		TM_ILI9341_Puts(10, 30, frequency_array, &TM_Font_11x18, ILI9341_COLOR_BLACK, ILI9341_COLOR_GREEN2);

		/* Display data on LCD */
		for (i = 0; i < FFT_SIZE; i++) {
			/* Draw FFT results */
			DrawBar(30 + 2 * i, 220, FFT_BAR_MAX_HEIGHT, (uint16_t)maxMag, (float32_t)Output[(uint16_t)i], 0x1234, 0xFFFF);
		}
		
	}
}

	
	

