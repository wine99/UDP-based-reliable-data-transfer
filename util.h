#ifndef UTIL
#define UTIL

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <strings.h> 
#include <unistd.h>

#define SEQNUM 25600
#define HEADSIZE 12
#define PACKETSIZE 524
#define DATASIZE 512

typedef struct {
	uint16_t seq;
	uint16_t ack;
	int8_t ACK_FLAG;
	int8_t SYN_FLAG;
	int8_t FIN_FLAG;
	int8_t padding1;
	uint16_t len;
	int16_t padding2;
} header;

typedef struct {
	header head;
	char payload[512];
} packet;

typedef enum{
	SEND, RECV, RESEND
} action;

typedef struct {
	packet p[10];
	int number[10];
	int size;
	int initSeq;
	int startNumber;
} window;

/* packet constructor */
void createPacket(packet* p, uint16_t seq, uint16_t ack, int8_t ACK, int8_t SYN, int8_t FIN, char* payload, uint16_t size);

/* header constructor */
void createHeader(header* h, uint16_t seq, uint16_t ack, int8_t ACK, int8_t SYN, int8_t FIN); 

/* window constructor */
void createWindow(window* w, packet* packets, int packetNumber, int start);

/* window stride forward */
void moveWindow(window* w, packet* packets, int packetNumber);

/* handles error */
void errorHandler(char* msg);

/* print messages with no payload specifically */
void printLine(action a, header* p, int isResend);

/* print message with payload */
void printPacket(action a, packet* p, int isResend);

/* min function */
int min(int a, int b);

#endif