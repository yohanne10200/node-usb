/* 
 * isoc_ep_onein_oneout_ep.c 
 */

#include <usb.h>
#include <linux/usbdevice_fs.h>
#include <asm/byteorder.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>


#define ISOC_BUFFERSIZE      375
#define ISOC_OUT_ENDPOINT    0x06
#define ISOC_IN_ENDPOINT     0x83

// hack of a var for output niceness
int okprint=0;

#define ISOC_OUTPUT_PACKET_COUNT 1
#define ISOC_INPUT_PACKET_COUNT  1
#define URB_ARR_COUNT            2

struct   usbdevfs_urb             *myOutURB = NULL;
char     isocOutputBuffer[ISOC_BUFFERSIZE];
int      isocOutputUniqueID                 = 1;
uint32_t totalIsocOutBytesSent              = 0;

//Variables for ISOC input
struct   usbdevfs_urb             *myInputURBArray[URB_ARR_COUNT];
int      urbIdArray[URB_ARR_COUNT];

int isocInputStartFrame = 0;

unsigned char isocInputDestinationBuffer[16024000];

uint32_t      totalIsocInBytesReceived = 0;

//Misc variables
struct usbdevfs_urb *mostRecentReapedURBPtr = NULL;

// Find device 
int findDevice(char *destinationDevicePathLocation, int *deviceNumber) {
	DIR *pDIR;
	int i;
	struct dirent *pDirEnt, *pDirEnt2;
	char currentBusDirectory[2048];
	char deviceOfInterest[2048];
	unsigned char deviceConfigData[2048];

	char rootDir[] = "/dev/bus/usb/"; 
	
	/* Open the current directory */

	pDIR = opendir(rootDir);

	if (pDIR == NULL) {
		fprintf( stderr, "%s %d: opendir() failed (%s)\n",
		__FILE__, __LINE__, strerror( errno ));
		exit( -1);
	}

	/* Get each directory entry from pDIR and print its name */

	pDirEnt = readdir(pDIR);
	while (pDirEnt != NULL) {
		//printf("%s\n", pDirEnt->d_name);
		
		if( pDirEnt->d_name[0] == '0' ) {
			strncpy(currentBusDirectory, rootDir, 2048);
			strcat(currentBusDirectory, pDirEnt->d_name);
			DIR *pDIR2 = opendir(currentBusDirectory);
			if (pDIR2 == NULL) {
				fprintf( stderr, "%s %d: opendir() failed (%s)\n",
				__FILE__, __LINE__, strerror( errno ));
				exit( -1);
			}	
			
			pDirEnt2 = readdir(pDIR2);
			while (pDirEnt2 != NULL) {	
				strncpy(deviceOfInterest, currentBusDirectory, 2048);
				strcat(deviceOfInterest, "/");
				strcat(deviceOfInterest, pDirEnt2->d_name);

				if (pDirEnt2->d_name[0] >= '0' && pDirEnt2->d_name[0] <= '9') {
					int fd2 = open(deviceOfInterest, O_ASYNC | O_RDWR);
					int readCount = read(fd2, deviceConfigData, 18);
					if (readCount != 18) {
						printf("ERROR: Unable to read from device %s\n", deviceOfInterest);
					} else {
						printf("Opened device %s\n", deviceOfInterest);
					}

					/*
					for(i = 8; i <= 13; i++ ) {
						printf("%X ", deviceConfigData[i]);
					}
					printf("\n");
					*/
					
					if (deviceConfigData[8] == 0xFF && deviceConfigData[9] == 0xFF) {
						if (deviceConfigData[10] == 0x05 && deviceConfigData[11] == 0x00) {
							if (deviceConfigData[12] == 0x00 && deviceConfigData[13] == 0x01) {
								printf("Found LPCUSB device at %s\n", deviceOfInterest);
								
								strcpy(destinationDevicePathLocation, deviceOfInterest);
								*deviceNumber = atoi(pDirEnt2->d_name);
								
								closedir(pDIR2);
								close(fd2);
								closedir(pDIR);
								
								
								return(0);
							}
						}
					}

					close(fd2);
				}
				
				pDirEnt2 = readdir(pDIR2);
			}

			
			closedir(pDIR2);
		}
			
		pDirEnt = readdir(pDIR);
	}

	/* Release the open directory */

	closedir(pDIR);
	
	return(-1);
}



void writeURBInISOC(int fd, struct usbdevfs_urb *myInputURB, void *requestUniqueIDOut, const int structSize ) {
	int ret,i;
	
	memset(myInputURB, 0, structSize);
    myInputURB->type = USBDEVFS_URB_TYPE_ISO;
    myInputURB->flags |= USBDEVFS_URB_ISO_ASAP;
    myInputURB->endpoint = ISOC_IN_ENDPOINT;
    myInputURB->buffer = isocInputDestinationBuffer;
    myInputURB->buffer_length = ISOC_BUFFERSIZE * ISOC_INPUT_PACKET_COUNT;
    myInputURB->actual_length = 0;
    myInputURB->usercontext = &requestUniqueIDOut;
    myInputURB->start_frame = isocInputStartFrame;
    
    myInputURB->number_of_packets = ISOC_INPUT_PACKET_COUNT;
    for(i = 0; i < ISOC_INPUT_PACKET_COUNT; i++ ) {
    	myInputURB->iso_frame_desc[i].length = ISOC_BUFFERSIZE;
    }
    
    ret = ioctl(fd, USBDEVFS_SUBMITURB, myInputURB);
    if (ret == -1) {
		printf("Error %d while trying to write string to usb device, string is: %s\n", errno, strerror(errno));
	}  else {
//		printf("ret %d ", ret);
//
//		printf("status %d ", myInputURB->status);
//		printf("flag %d ", myInputURB->flags);
//		printf("error_count %d ", myInputURB->error_count);
//		printf("number_of_packets %d ", myInputURB->number_of_packets);
//		printf("actual_length %d ", myInputURB->actual_length);
//		printf("start_frame %d ", myInputURB->start_frame);
//		printf("usercontext %d ", myInputURB->usercontext);
//		printf("iso_frame_desc[0].actual_length %d ", myInputURB->iso_frame_desc[0].actual_length);
//		printf("iso_frame_desc[0].length %d ", myInputURB->iso_frame_desc[0].length);
//		printf("iso_frame_desc[0].status %d\n", myInputURB->iso_frame_desc[0].status);
		
		//for (i = 0; i < myOutURB->iso_frame_desc[0].actual_length; i++) {
			//printf(" 0x%X", ((unsigned char*)myOutURB->buffer)[i]);
		//}
		//printf("\n");
	}
//    printf("\n");
    //sleep(1);
}



int reapISOC_URB(int fd) {
	int ret, i;
	//printf("-----------------------------------\n");
	//printf("Reaping URB...\n");
	mostRecentReapedURBPtr = NULL;
	
	ret = ioctl(fd, USBDEVFS_REAPURBNDELAY, &mostRecentReapedURBPtr);
	if (ret == -1) {
		//printf("Error %d while reaping URB: %s\n", errno, strerror(errno));
	} else {
		if (mostRecentReapedURBPtr == NULL) {
//			printf("myURB2 is null...\n");
		} else {
			
			if (mostRecentReapedURBPtr->endpoint == ISOC_IN_ENDPOINT ) {
				isocInputStartFrame += mostRecentReapedURBPtr->number_of_packets;
                                totalIsocInBytesReceived += mostRecentReapedURBPtr->actual_length;
                                uint32_t val;
                                if(okprint) {
                                    printf("Input Length %d\t ",mostRecentReapedURBPtr->actual_length);
                                    memcpy(&val, mostRecentReapedURBPtr->buffer, 4);
                                    printf("ADC %3X\t ", val);
                                }

				memcpy(&val, mostRecentReapedURBPtr->buffer + 4, 4);
//				printf("Button 1 %X\t\t ", val);
				
				memcpy(&val, mostRecentReapedURBPtr->buffer + 8, 4);
//				printf("Button 2 %X\r", val);
//				printf("Button 2 %X\t\t  ", val);
				
				
				/*
				ret = ioctl(fd, USBDEVFS_DISCARDURB, mostRecentReapedURBPtr);
				printf("Ret after discarding URB is %d", ret);
				ret = 0;
				printf("\n");
				*/
			}
			
			
			
		
		}
		
	}
	
	return(ret);
}



int frameNumber = 1;
void writeURBOutISOC(int fd, struct usbdevfs_urb *myOutURB, int urbStructSize, 
		char *stringBuff, void *requestUniqueIDOut, int bufferLength) 
{
	int ret,i;
	
	memset(myOutURB, 0, urbStructSize);
    myOutURB->type = USBDEVFS_URB_TYPE_ISO;
    myOutURB->flags |= USBDEVFS_URB_ISO_ASAP;
    myOutURB->endpoint = ISOC_OUT_ENDPOINT;
    myOutURB->buffer = stringBuff;
    myOutURB->buffer_length = bufferLength;
    myOutURB->actual_length = 0;
    myOutURB->usercontext = requestUniqueIDOut;
    myOutURB->start_frame = frameNumber;
    frameNumber++;
    myOutURB->number_of_packets = ISOC_OUTPUT_PACKET_COUNT;
    myOutURB->iso_frame_desc[0].length = myOutURB->buffer_length;
    
    
    
//	printf("-----------------------------------\n");
//	printf("Writing URB to isoc out...\n");
	
    ret = ioctl(fd, USBDEVFS_SUBMITURB, myOutURB);
    if (ret == -1) {
		printf("Error %d while trying to write string to usb device, string is: %s\n", errno, strerror(errno));
	}  else {
//		printf("Wrote string to device, ret = %d\n", ret);
		totalIsocOutBytesSent += bufferLength;
	}
 //   printf("\n");
}


int main(void) {
	int i, q, ret, fd = -1;
	int interfaceToClaim = 0;
	int deviceNumber = 0;
	char deviceToOpen[4096];
	int r = findDevice(deviceToOpen, &deviceNumber);
	if( r != 0 ) {
		printf("ERROR: Unable to locate LPCUSB device attached to system (or permissions to /dev/bus/usb are wrong)\n");
		exit(-1);
	} else {
		printf("Found LPCUSB device at %s with device number %d\n", deviceToOpen, deviceNumber);
	}
	
	printf("Trying to open device %s\n", deviceToOpen);
	
    fd = open(deviceToOpen, O_ASYNC | O_RDWR);
    if( fd == -1 ) {
    	printf("An error occured dirng open of device, errno=%d\n", errno);
    	printf("%s\n", strerror(errno));
    	exit(1);
    }
    
    //Claim interface 1 of the USB device
    printf("Claiming interface...\n");
    ret = ioctl(fd, USBDEVFS_CLAIMINTERFACE, &interfaceToClaim);
    if( ret != 0 ) {
		printf("Error %d while claiming interface, string is: %s\n",errno, strerror(errno));
    }
    
    
    
    int isocOutputURBStructSize = sizeof(struct usbdevfs_urb) + (ISOC_OUTPUT_PACKET_COUNT * sizeof(struct usbdevfs_iso_packet_desc));
    myOutURB = (struct usbdevfs_urb*) malloc(isocOutputURBStructSize);	
      
    
    int isocInputURBStructSize = sizeof(struct usbdevfs_urb) + (ISOC_INPUT_PACKET_COUNT * sizeof(struct usbdevfs_iso_packet_desc));

    for(i = 0; i < URB_ARR_COUNT; i++ ) {
    	myInputURBArray[i] = (struct usbdevfs_urb*) malloc(isocInputURBStructSize);
    	urbIdArray[i] = i;
    }
    
	time_t startTime = time(NULL);
	time_t timeDelta;
	
	for (i = 0; i < URB_ARR_COUNT; i++) {
		printf("===============================================================================\n");
		writeURBInISOC(fd, myInputURBArray[i], &urbIdArray[i], isocInputURBStructSize);
	}


        for (q = 0; q < 100000; q++) {

            for (i = 0; i < URB_ARR_COUNT; i++) {
                while (reapISOC_URB(fd) < 0) {
                }

                switch (mostRecentReapedURBPtr->endpoint) {
                    case ISOC_IN_ENDPOINT:
                        writeURBInISOC(fd, myInputURBArray[i], &urbIdArray[i], isocInputURBStructSize);
                        break;
                }
                time_t now = time(NULL);

                uint8_t ledVal = now % 2;
                if( ledVal != isocOutputBuffer[0] ) {
                    isocOutputBuffer[0] = ledVal;
                    writeURBOutISOC(fd, myOutURB, isocOutputURBStructSize, isocOutputBuffer, &isocOutputUniqueID, 1);
                }

                timeDelta = now - startTime;
//                printf("Time delta is: %d\n",timeDelta);
                if (timeDelta > 0) {
                    okprint = 1;
                    long isocInBytesPerSecond = totalIsocInBytesReceived / timeDelta;
                    printf("Bytes/second %7d\r", isocInBytesPerSecond);
                }
            }
        }

	//Release the interface
	printf("Releasing interface...\n");
    ret = ioctl(fd, USBDEVFS_RELEASEINTERFACE, &interfaceToClaim);
    if( ret != 0 ) {
		printf("Error %d string is: %s\n",errno, strerror(errno));
    }

	close(fd);
	return(0);
}


