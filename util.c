#include <string.h>

#include "util.h"

void createPacket(packet* p, uint16_t seq, uint16_t ack, int8_t ACK, int8_t SYN, int8_t FIN, char* payload, uint16_t size) {
    p->head.seq = seq;
    p->head.ack = ack;
    p->head.ACK_FLAG = ACK;
    p->head.SYN_FLAG = SYN;
    p->head.FIN_FLAG = FIN;
    memset(p->payload, 0, 512);
    p->head.len = size;
    memcpy(p->payload, payload, size);
}

void createHeader(header* h, uint16_t seq, uint16_t ack, int8_t ACK, int8_t SYN, int8_t FIN) {
    h->seq = seq;
    h->ack = ack;
    h->ACK_FLAG = ACK;
    h->SYN_FLAG = SYN;
    h->FIN_FLAG = FIN;
    h->len = 1;
}

void createWindow(window* w, packet* packets, int packetNumber, int start) {
    if (start == packetNumber) {
        w->size = 0;
        return;
    }

    w->size = ((packetNumber - start) >= 10) ? 10 : (packetNumber - start);
    int i = 0;
    for (i = 0; i < w->size; i++) {
        w->p[i] = packets[i + start];
        w->number[i] = start + i;
    }
    w->initSeq = w->p[0].head.seq;
    w->startNumber = start;
}

void moveWindow(window* w, packet* packets, int packetNumber) {
    int start = w->number[0] + 1;
    createWindow(w, packets, packetNumber, start);
}

void errorHandler(char* msg) {
    perror(msg);
    exit(1);
}

void printLine(action a, header* p, int isResend) {
    char line[128];
    memset(line, 0, 128);

    if (a == SEND) {
        snprintf(line, sizeof(line), "SEND seq:%u ack:%u", (unsigned int)p->seq, (unsigned int)p->ack);
    } else {
        snprintf(line, sizeof(line), "RECV seq:%u ack:%u", (unsigned int)p->seq, (unsigned int)p->ack);
    }

    if (p->SYN_FLAG) {
        strcat(line, " SYN");
    }
    if (p->FIN_FLAG) {
        strcat(line, " FIN");
    }
    if (p->ACK_FLAG) {
        strcat(line, " ACK");
    }
    if (isResend) {
        strcat(line, " DUP-ACK");
    }

    strcat(line, "\n");
    printf("%s", line);
}

void printPacket(action a, packet* p, int isResend) {
    printLine(a, &(p->head), isResend);
}

void printClientResend(packet* p) {
    header* h = &p->head;
    char line[128];
    memset(line, 0, 128);
    snprintf(line, sizeof(line), "RESEND seq:%u ack:%u\n", (unsigned int)h->seq, (unsigned int)h->ack);
    printf("%s", line);
}

int isOldAck(window* w, header* p) {
    int expectedAck = w->initSeq;
    for (int i = 0; i < w->size; ++i) {
        expectedAck += w->p[i].head.len;
        expectedAck = (expectedAck > SEQNUM) ? (expectedAck % (SEQNUM + 1)) : expectedAck;
        if (expectedAck == p->ack)
            return 0;
    }
    return 1;
}

int min(int a, int b) { return (a > b) ? b : a; }
