/*
 * Minimal ADC driver implementation for the PIC18F47K42.
 *
 * The MCC generated ADC header and types are present in this project, but the
 * generated source was missing. Keep these APIs compatible with adc.h and use
 * the ADC registers directly for basic single conversion operation.
 */

#include "../adc.h"
#include <xc.h>

void ADC_Initialize(void)
{
    /* ANA0 (RA0), VDD reference, right justified 12-bit result. */
    ADPCH = 0x00;
    ADREF = 0x00;
    ADCLK = 0x3F;
    ADACQ = 0x1F;
    ADCON2 = 0x00;
    ADCON1 = 0x00;
    ADCON0 = 0x84;
}

void ADC_Deinitialize(void)
{
    ADCON0bits.ADON = 0;
}

void ADC_Enable(void)
{
    ADCON0bits.ADON = 1;
}

void ADC_Disable(void)
{
    ADCON0bits.ADON = 0;
}

void ADC_ChannelSelect(adc_channel_t channel)
{
    ADPCH = (uint8_t)channel;
}

void ADC_ConversionStart(void)
{
    ADCON0bits.ADGO = 1;
}

bool ADC_IsConversionDone(void)
{
    return (bool)(ADCON0bits.ADGO == 0);
}

void ADC_ConversionStop(void)
{
    ADCON0bits.ADGO = 0;
}

adc_result_t ADC_ConversionResultGet(void)
{
    return (adc_result_t)(((uint16_t)ADRESH << 8) | ADRESL);
}

void ADC_ISR(void)
{
    PIR1bits.ADIF = 0;
}

void ADC_ThresholdISR(void)
{
    PIR1bits.ADIF = 0;
}
