// old code for FINISH of client.

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>

#include "util.h"

struct sockaddr_in servaddr;
socklen_t len = sizeof(servaddr);
int sockfd;

void finish(int seq, int ack) {
    int n;
    header fin1, fin2, fin3, fin4;
    createHeader(&fin1, seq, 0, 0, 0, 1);
    createHeader(&fin4, seq + 1, ack + 1, 1, 0, 0);

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
    
    time_t finTime = time(0);
    while (1) {
        if ((time(0) - finTime) > 0.5) {
            printf("TIMEOUT %i\n", seq);
            n = sendto(sockfd, &fin1, HEADSIZE, 0, (struct sockaddr*)&servaddr, len);
            if (n < 0) {
                errorHandler("Failed to send to server.");
            } else {
                printLine(RESEND, &fin1, 0);
                finTime = time(0);
            }
        } else {
            n = recvfrom(sockfd, &fin2, HEADSIZE, 0, (struct sockaddr*)&servaddr, &len);
            if (n != -1) {
                printLine(RECV, &fin2, 0);
                if (fin2.FIN_FLAG == 1) {
                    n = sendto(sockfd, &fin4, HEADSIZE, 0, (struct sockaddr*)&servaddr, len);
                    if (n <= 0) {
                        errorHandler("Failed to send to server.");
                    } else {
                        printLine(SEND, &fin4, 0);
                    }
                }
                if (fin2.ACK_FLAG == 1) {
                    break;
                }
            }
        }
    }

    header fin3;
    while (1) {
        n = recvfrom(sockfd, &fin3, HEADSIZE, 0, (struct sockaddr*)&servaddr,
                     &len);
        if (n != -1) {
            printLine(RECV, &fin3, 0);
            break;
        }
    }

    n = sendto(sockfd, &fin4, HEADSIZE, 0, (struct sockaddr*)&servaddr, len);
    if (n <= 0) {
        errorHandler("Failed to send to server.");
    } else {
        printLine(SEND, &fin4, 0);
    }

    time_t finalCheck = time(0);
    while (1) {
        if (time(0) - finalCheck > 0.5) {
            break;
        } else {
            n = recvfrom(sockfd, &fin3, HEADSIZE, 0,
                         (struct sockaddr*)&servaddr, &len);
            if (n != -1) {
                printLine(RECV, &fin3, 0);
                n = sendto(sockfd, &fin4, HEADSIZE, 0,
                           (struct sockaddr*)&servaddr, len);
                if (n <= 0) {
                    errorHandler("Failed to send to server.");
                } else {
                    printLine(SEND, &fin4, 1);
                }
            }
        }
    }
}
