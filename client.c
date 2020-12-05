#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>

#include "util.h"

/* control the send losing probability,
range from 0 to 9, 9 or above means always lost. */
int loseProb = 2;

struct sockaddr_in servaddr;
socklen_t len = sizeof(servaddr);
int sockfd;

void sendWindow(window w, int isResend); /* send all packets in window to server */
void sendLastPacket(window w);           /* send the last packet when window moves forward */
void finish(int seq, int ack);           /* 4-way finishing */

int main(int argc, char** argv) {

    /*------   validate arguments  --------*/
    if (argc != 4) {
        perror("Missing arguments.");
        exit(1);
    }

    int portno = atoi(argv[2]);
    if (portno == 0) {
        perror("Invalid port number.");
        exit(1);
    }

    /*------   establish UDP  --------*/
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        exit(1);
    }

    memset(&servaddr, 0, sizeof(servaddr));

    // Filling server information
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(portno);
    servaddr.sin_addr.s_addr = inet_addr(argv[1]);

    /*------   3-way handshaking  --------*/
    srand(time(0));
    /* send SYN */
    uint16_t initSeq = rand() % SEQNUM;
    // uint16_t initSeq = 16850;
    header sp;
    createHeader(&sp, initSeq, 0, 0, 1, 0);

    int n = sendto(sockfd, &sp, HEADSIZE, 0, (struct sockaddr*)&servaddr, len);
    if (n <= 0) {
        errorHandler("Failed to send to server");
    } else {
        printLine(SEND, &sp, 0);
    }

    n = fcntl(sockfd, F_SETFL, O_NONBLOCK);
    if (n < 0) {
        errorHandler("Can't set file descriptor to non-blocking.");
    }

    time_t synTime = time(0);
    header sa;
    while (1) {
        if ((time(0) - synTime) > 0.5) {
            printf("TIMEOUT %u\n", (unsigned int)initSeq);
            n = sendto(sockfd, &sp, HEADSIZE, 0, (struct sockaddr*)&servaddr, len);
            if (n < 0) {
                errorHandler("Failed to send to server.");
            } else {
                printLine(RESEND, &sp, 0);
                synTime = time(0);
            }
        } else {
            n = recvfrom(sockfd, &sa, HEADSIZE, 0, (struct sockaddr*)&servaddr, &len);
            if (n != -1) {
                if (sa.ACK_FLAG != 1 || sa.SYN_FLAG != 1) {
                    errorHandler("Wrong packet.");
                } else {
                    printLine(RECV, &sa, 0);
                    break;
                }
            }
        }
    }

    /*------   creating packets  --------*/
    uint16_t seq = sa.ack;
    uint16_t ack = sa.seq + 1;
    packet msg;

    FILE* filefd = fopen(argv[3], "rb");
    struct stat st;
    if (filefd == NULL) {
        errorHandler("Unable to open file.");
    }
    stat(argv[3], &st);
    unsigned long fileSize = st.st_size;
    int fileNumber = fileSize / DATASIZE;
    int remainBytes = fileSize % DATASIZE;
    char* buffer = malloc(fileSize * sizeof(char));
    fread(buffer, sizeof(char), fileSize, filefd);
    fclose(filefd);

    packet* p = (packet*)calloc(fileNumber + 1, PACKETSIZE);
    int i = 0;
    for (i = 0; i < fileNumber; i++) {
        char temp[DATASIZE];
        memset(temp, 0, DATASIZE);
        memcpy(temp, buffer + i * DATASIZE, DATASIZE);
        createPacket(&p[i], seq, 0, 0, 0, 0, temp, DATASIZE);
        seq += DATASIZE;
        seq = (seq > SEQNUM) ? (seq % (SEQNUM + 1)) : seq;
    }

    int packetNumber = fileNumber;
    if (remainBytes > 0) {
        packetNumber = fileNumber + 1;
        char* temp = (char*)malloc(DATASIZE * sizeof(char));
        memset(temp, 0, DATASIZE);
        memcpy(temp, buffer + fileNumber * DATASIZE, remainBytes);
        createPacket(&p[fileNumber], seq, 0, 0, 0, 0, temp, remainBytes);

        seq += remainBytes;
        seq = (seq > SEQNUM) ? (seq % (SEQNUM + 1)) : seq;
    }

    p[0].head.ack = ack;
    p[0].head.ACK_FLAG = 1;

    /*------   start transmitting file  --------*/
    window w;
    createWindow(&w, p, packetNumber, 0);

    sendWindow(w, 0);

    time_t startTime = time(0);
    while (1) {
        if (time(0) - startTime >= 0.5) {
            startTime = time(0);
            sendWindow(w, 1);
        } else {
            header tempAck;
            n = recvfrom(sockfd, &tempAck, HEADSIZE, 0, (struct sockaddr*)&servaddr, &len);
            if (n != -1) {
                printLine(RECV, &tempAck, 0);
                int expectedAck = w.initSeq + w.p[0].head.len;
                expectedAck = (expectedAck > SEQNUM) ? (expectedAck % (SEQNUM + 1)) : expectedAck;
                if (tempAck.ACK_FLAG != 1) {
                    errorHandler("Wrong ACK packet.");
                } else if (isOldAck(&w, &tempAck) == 0) {
                    while (expectedAck != tempAck.ack) {
                        // printf("expected ACK: %d\n", expectedAck);
                        moveWindow(&w, p, packetNumber);
                        printf("move window, size after move: %d\n", w.size);
                        if (w.size == 10)
                            sendLastPacket(w);
                        if (w.size == 0)
                            break;
                        expectedAck += w.p[0].head.len;
                        expectedAck = (expectedAck > SEQNUM) ? (expectedAck % (SEQNUM + 1)) : expectedAck;
                    }
                    // printf("expected ACK: %d\n", expectedAck);
                    moveWindow(&w, p, packetNumber);
                    printf("move window, size after move: %d\n", w.size);
                    if (w.size == 10)
                        sendLastPacket(w);
                    if (w.size == 0)
                        break;
                    startTime = time(0);
                }
            }
        }
    }

    finish(seq, ack);

    return 0;
}

void finish(int seq, int ack) {
    int n;
    header fin1, fin2, finalAck;
    createHeader(&fin1, seq, 0, 0, 0, 1);
    createHeader(&finalAck, seq + 1, ack + 1, 1, 0, 0);

    /* send FIN */
    n = sendto(sockfd, &fin1, HEADSIZE, 0, (struct sockaddr*)&servaddr, len);
    if (n < 0) {
        errorHandler("Failed to send to server.");
    } else {
        printLine(SEND, &fin1, 0);
    }

    /* if receive FIN, send ACK, then either
        1. wait for 2 sec to close; or
        2. receive ACK and immediately close.

       if receive ACK, wait for FIN.
        1. if receive FIN, send ACK and immediately close.
        2. if timedout, resend FIN. */
    
    int finReceived = 0;
    int ackReceived = 0;
    time_t startTime = time(0);
    while (1) {
        if (finReceived == 1) {
            n = sendto(sockfd, &finalAck, HEADSIZE, 0, (struct sockaddr*)&servaddr, len);
            if (n <= 0) errorHandler("Filed to send to server.");
            else printLine(SEND, &finalAck, 0);
            time_t finTime = time(0);
            while (1) {
                if (time(0) - finTime > 2.0)
                    break;
                else {
                    header tempFin;
                    n = recvfrom(sockfd, &tempFin, HEADSIZE, 0, (struct sockaddr*)&servaddr, &len);
                    if (n != -1) {
                        printLine(RECV, &tempFin, 0);
                        if (tempFin.ACK_FLAG == 1)
                            break;
                    }
                }
            }
            printf("connection closed.\n");
            break;
        }

        if (ackReceived == 1) {
            time_t finTime = time(0);
            while (1) {
                if (time(0) - finTime > 0.5) {
                    printf("TIMEOUT %i\n", seq);
                    n = sendto(sockfd, &fin1, HEADSIZE, 0, (struct sockaddr*)&servaddr, len);
                    if (n < 0) {
                        errorHandler("Failed to send to server.");
                    } else {
                        printLine(RESEND, &fin1, 0);
                        finTime = time(0);
                    }
                    ackReceived = 0;
                    break;
                } else {
                    header tempFin;
                    n = recvfrom(sockfd, &tempFin, HEADSIZE, 0, (struct sockaddr*)&servaddr, &len);
                    if (n != -1) {
                        printLine(RECV, &tempFin, 0);
                        if (tempFin.FIN_FLAG == 1) {
                            n = sendto(sockfd, &finalAck, HEADSIZE, 0, (struct sockaddr*)&servaddr, len);
                            if (n <= 0) errorHandler("Filed to send to server.");
                            else printLine(SEND, &finalAck, 0);
                            break;
                        }
                    }
                }
            }
            if (ackReceived == 0) continue;
            else {
                printf("connection closed.\n");
                break;
            }
        }

        if ((time(0) - startTime) > 0.5) {
            printf("TIMEOUT %i\n", seq);
            n = sendto(sockfd, &fin1, HEADSIZE, 0, (struct sockaddr*)&servaddr, len);
            if (n < 0) {
                errorHandler("Failed to send to server.");
            } else {
                printLine(RESEND, &fin1, 0);
                startTime = time(0);
            }
        } else {
            n = recvfrom(sockfd, &fin2, HEADSIZE, 0, (struct sockaddr*)&servaddr, &len);
            if (n != -1) {
                printLine(RECV, &fin2, 0);
                if (fin2.FIN_FLAG == 1)
                    finReceived = 1;
                if (fin2.ACK_FLAG == 1)
                    ackReceived = 1;
            }
        }
    }
}

void sendWindow(window w, int isResend) {
    int i = 0;
    for (i = 0; i < w.size; i++) {
        if (isResend)
            printClientResend(&(w.p[i]));
        else
            printPacket(SEND, &(w.p[i]), 0);
        if (rand() % 10 >= loseProb) {
            int n = sendto(sockfd, &(w.p[i]), PACKETSIZE, 0, (struct sockaddr*)&servaddr, len);
            if (n < 0)
                errorHandler("Failed to send data to server.");
        } else {
            printf("seq: %d lost.\n", w.p[i].head.seq);
        }
    }
}

void sendLastPacket(window w) {
    int n = sendto(sockfd, &(w.p[9]), PACKETSIZE, 0, (struct sockaddr*)&servaddr, len);
    if (n < 0) {
        errorHandler("Failed to send data to server.");
    } else {
        printPacket(SEND, &(w.p[9]), 0);
    }
}
