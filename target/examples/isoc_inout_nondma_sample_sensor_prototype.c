/*
	LPCUSB, an USB device driver for LPC microcontrollers	
	Copyright (C) 2006 Bertrik Sikken (bertrik@sikken.nl)

	Redistribution and use in source and binary forms, with or without
	modification, are permitted provided that the following conditions are met:

	1. Redistributions of source code must retain the above copyright
	   notice, this list of conditions and the following disclaimer.
	2. Redistributions in binary form must reproduce the above copyright
	   notice, this list of conditions and the following disclaimer in the
	   documentation and/or other materials provided with the distribution.
	3. The name of the author may not be used to endorse or promote products
	   derived from this software without specific prior written permission.

	THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
	IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
	OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
	IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, 
	INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
	NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
	DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
	THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
	(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
	THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <string.h>			// memcpy
#include "type.h"
#include "debug.h"
#ifdef LPC214x
#include "lpc214x.h"
#endif
#ifdef LPC23xx
#include "lpc23xx.h"
#endif

#include "usbisoc.h"
#include "armVIC.h"
#include "hal.h"
#include "console.h"
#include "usbapi.h"
#include "usbhw_lpc.h"
#include "adc.h"

#define DEBUG_LED_ON(x)     IOCLR0 = (1 << x);
#define DEBUG_LED_OFF(x)    IOSET0 = (1 << x);

#define BAUD_RATE	115200

#define INT_IN_EP		0x81

#define ISOC_OUT_EP     0x06
#define ISOC_IN_EP      0x83

#define MAX_PACKET_SIZE	1023

#define LE_WORD(x)		((x)&0xFF),((x)>>8)

//#define BYTES_PER_ISOC_FRAME 1023

#define ADC_INPUT_INDEX 0
#define BUTTON1_INPUT_INDEX 1
#define BUTTON2_INPUT_INDEX 2

#define NUMBER_OF_SENSOR_READINGS 3
#define BYTES_PER_ISOC_FRAME (NUMBER_OF_SENSOR_READINGS * 4)

__attribute__ ((aligned(4))) U32 inputIsocDataBuffer[(BYTES_PER_ISOC_FRAME/4)];


#define ISOC_OUTPUT_DATA_BUFFER_SIZE 1024
volatile U8 outputIsocDataBuffer[ISOC_OUTPUT_DATA_BUFFER_SIZE];



int isConnectedFlag = 0;


U8 bDevStat = 0;

#define	INT_VECT_NUM	0

#define IRQ_MASK 0x00000080


//static U8 abBulkBuf[64];
static U8 abClassReqData[8];

// forward declaration of interrupt handler
static void USBIntHandler(void) __attribute__ ((interrupt(IRQ), naked));

static const U8 abDescriptors[] = {

// device descriptor
	0x12,
	DESC_DEVICE,
	LE_WORD(0x0101),			// bcdUSB
	0x02,						// bDeviceClass
	0x00,						// bDeviceSubClass
	0x00,						// bDeviceProtocol
	MAX_PACKET_SIZE0,			// bMaxPacketSize
	LE_WORD(0xFFFF),			// idVendor
	LE_WORD(0x0005),			// idProduct
	LE_WORD(0x0100),			// bcdDevice
	0x01,						// iManufacturer
	0x02,						// iProduct
	0x03,						// iSerialNumber
	0x01,						// bNumConfigurations

// configuration descriptor
	0x09,
	DESC_CONFIGURATION,
	LE_WORD(32), //sizeof(this configuration descriptor) + sizeof(all interfaces defined)   //LE_WORD(67),				// wTotalLength
	0x01, //0x02,				// bNumInterfaces
	0x01,						// bConfigurationValue
	0x00,						// iConfiguration
	0xC0,						// bmAttributes
	0x32,						// bMaxPower
	
// data class interface descriptor   9+7+7=23
	0x09,
	DESC_INTERFACE,
	0x00,						// bInterfaceNumber
	0x00,						// bAlternateSetting
	0x02,//DC				    // bNumEndPoints
	0xFF,// 0x0A,				// bInterfaceClass = data
	0x00,						// bInterfaceSubClass
	0x00,						// bInterfaceProtocol
	0x00,						// iInterface
	
// data EP OUT
	0x07,
	DESC_ENDPOINT,
	ISOC_OUT_EP,				// bEndpointAddress
	0x0D,					    // bmAttributes = isoc, syncronous, data endpoint
	LE_WORD(MAX_PACKET_SIZE),	// wMaxPacketSize
	0x01,						// bInterval	
	
	// data EP OUT
	0x07,
	DESC_ENDPOINT,
	ISOC_IN_EP,				    // bEndpointAddress
	0x0D,					    // bmAttributes = isoc, syncronous, data endpoint
	LE_WORD(MAX_PACKET_SIZE),	// wMaxPacketSize
	0x01,						// bInterval	
	
	// string descriptors
	0x04,
	DESC_STRING,
	LE_WORD(0x0409),

	0x0E,
	DESC_STRING,
	'L', 0, 'P', 0, 'C', 0, 'U', 0, 'S', 0, 'B', 0,

	0x14,
	DESC_STRING,
	'U', 0, 'S', 0, 'B', 0, 'S', 0, 'e', 0, 'r', 0, 'i', 0, 'a', 0, 'l', 0,

	0x12,
	DESC_STRING,
	'D', 0, 'E', 0, 'A', 0, 'D', 0, 'C', 0, '0', 0, 'D', 0, 'E', 0,

// terminating zero
	0
};


/**
	Local function to handle the USB-CDC class requests
		
	@param [in] pSetup
	@param [out] piLen
	@param [out] ppbData
 */
static BOOL HandleClassRequest(TSetupPacket *pSetup, int *piLen, U8 **ppbData)
{
	return TRUE;
}




/******************************************************************************
 *
 * MACRO Name: ISR_ENTRY()
 *
 * Description:
 *    This MACRO is used upon entry to an ISR.  The current version of
 *    the gcc compiler for ARM does not produce correct code for
 *    interrupt routines to operate properly with THUMB code.  The MACRO
 *    performs the following steps:
 *
 *    1 - Adjust address at which execution should resume after servicing
 *        ISR to compensate for IRQ entry
 *    2 - Save the non-banked registers r0-r12 and lr onto the IRQ stack.
 *    3 - Get the status of the interrupted program is in SPSR.
 *    4 - Push it onto the IRQ stack as well.
 *
 *****************************************************************************/
#define ISR_ENTRY() asm volatile(" sub   lr, lr,#4\n" \
                                 " stmfd sp!,{r0-r8,lr}\n" \
                                 " mrs   r1, spsr\n" \
                                 " stmfd sp!,{r1}")


/******************************************************************************
 *
 * MACRO Name: ISR_EXIT()
 *
 * Description:
 *    This MACRO is used to exit an ISR.  The current version of the gcc
 *    compiler for ARM does not produce correct code for interrupt
 *    routines to operate properly with THUMB code.  The MACRO performs
 *    the following steps:
 *
 *    1 - Recover SPSR value from stack       
 *    2 - and restore  its value                   
 *    3 - Pop the return address & the saved general registers from
 *        the IRQ stack & return
 *
 *****************************************************************************/
#define ISR_EXIT()  asm volatile(" ldmfd sp!,{r1}\n" \
                                 " msr   spsr_c,r1\n" \
                                 " ldmfd sp!,{r0-r8,pc}^")


/**
	Interrupt handler
	
	Simply calls the USB ISR, then signals end of interrupt to VIC
 */
void USBIntHandler(void) 
{
	 ISR_ENTRY(); 
	USBHwISR();
	
	VICVectAddr = 0x00;    // dummy write to VIC to signal end of ISR
	ISR_EXIT();
}

/**
	USB frame interrupt handler
	
	Called every milisecond by the hardware driver.
	
	This function is responsible for sending the first of a chain of packets
	to the host. A chain is always terminated by a short packet, either a
	packet shorter than the maximum packet size or a zero-length packet
	(as required by the windows usbser.sys driver).

 */

int delay = 0;
void USBFrameHandler(U16 wFrame)
{
    // send over USB
	if( isConnectedFlag ) {
		if( delay < 4000 ) {
			delay++;
		} else {
			USBHwEPWrite(ISOC_IN_EP, inputIsocDataBuffer, BYTES_PER_ISOC_FRAME);
				
			
			
			int iLen = USBHwISOCEPRead(ISOC_OUT_EP, outputIsocDataBuffer, sizeof(outputIsocDataBuffer));
			if (iLen > 0) {
				//FIXME parse data and control somthing
				
				U8 b1 = outputIsocDataBuffer[0];
				if( b1 ) {
					IOSET0 |= (1<<10);
				} else {
					IOCLR0 |= (1<<10);
				}
				DBG("z%d", b1);
			}
			
		}
	}
}


/**
	USB device status handler
	
	Resets state machine when a USB reset is received.
 */
static void USBDevIntHandler(U8 bDevStatus)
{
	if ((bDevStatus & DEV_STATUS_RESET) != 0) {
	}
	
	bDevStat = bDevStatus;
	
	//FIXME not sure if this is the right way to detect being connected???
	switch(bDevStatus ) {
	case DEV_STATUS_CONNECT:
		isConnectedFlag= 1;
		break;
	case DEV_STATUS_RESET:
	case DEV_STATUS_SUSPEND:
		isConnectedFlag= 0;
		break;
	}
}




/*************************************************************************
	main
	====
**************************************************************************/
int main(void)
{
	int c, i;
	
	IODIR0 |= (1<<10);
	
	
	// PLL and MAM
	HalSysInit();

#ifdef LPC214x
	// init DBG
	ConsoleInit(60000000 / (16 * BAUD_RATE));
#else
	// init DBG
	ConsoleInit(72000000 / (16 * BAUD_RATE));
#endif

	DBG("Initialising USB stack\n");

	// initialise stack
	USBInit();

	configureADCPort(AD0_3);
	
	// register descriptors
	USBRegisterDescriptors(abDescriptors);

	// register class request handler
	USBRegisterRequestHandler(REQTYPE_TYPE_CLASS, HandleClassRequest, abClassReqData);

	// register endpoint handlers
	USBHwRegisterEPIntHandler(INT_IN_EP, NULL);
		
	// register frame handler
	USBHwRegisterFrameHandler(USBFrameHandler);
	
	// register device event handler
	USBHwRegisterDevIntHandler(USBDevIntHandler);
	
	
	char ch = 'a';
	for (i = 0; i < BYTES_PER_ISOC_FRAME; i++) {
		inputIsocDataBuffer[i] = ch;
		ch = (ch == 'z' ? 'a' : (ch + 1));
	}
	
	DBG("Starting USB communication\n");

#ifdef LPC214x
	(*(&VICVectCntl0+INT_VECT_NUM)) = 0x20 | 22; // choose highest priority ISR slot 	
	(*(&VICVectAddr0+INT_VECT_NUM)) = (int)USBIntHandler;
#else
  VICVectCntl22 = 0x01;
  VICVectAddr22 = (int)USBIntHandler;
#endif
  
	// set up USB interrupt
	VICIntSelect &= ~(1<<22);               // select IRQ for USB
	VICIntEnable |= (1<<22);
	
	enableIRQ();

	// connect to bus
	USBHwConnect(TRUE);
	
	int x = 0;
		
	const int interval = 100000;
	// echo any character received (do USB stuff in interrupt)
	
	for(;;) {
		x++;
		U32 temp = cADC_Result(AD0_3);
		if( temp >= 0 && temp <= 1024 )
			inputIsocDataBuffer[ADC_INPUT_INDEX] = temp;
		
		
		inputIsocDataBuffer[BUTTON1_INPUT_INDEX] = ((IOPIN0 & (1<<15)) ? 1 : 0);
		inputIsocDataBuffer[BUTTON2_INPUT_INDEX] = ((IOPIN0 & (1<<16)) ? 1 : 0);
		
		if (x == interval) {
			
			IOSET0 = (1<<11);
			//turn on led	
		    
		} else if (x >= (interval*2)) {
			IOCLR0 = (1<<11);
			//turn off led
			x = 0;
		}
	}

	return 0;
}

