#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <string.h>

#include "util.h"

/* control the send losing probability,
range from 0 to 9, 9 or above means always lost. */
int loseProb = 2;

struct sockaddr_in my_addr;    /* my address */
struct sockaddr_in their_addr; /* connector addr */
socklen_t len = sizeof(their_addr);
int sin_size;
int sockfd;

void finish(int seq, int ack); /* 4-way closing of socket */

int main(int argc, char **argv) {

    /*------   validate arguments  --------*/
    if (argc != 2) {
        perror("Missing arguments");
        exit(1);
    }

    int portno = atoi(argv[1]);
    if (portno == 0) {
        perror("Invalid port number.");
        exit(1);
    }

    /*------   establish UDP  --------*/

    /* create a socket */
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("ERROR: opening socket");
        exit(1);
    }

    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(portno); /* short, network byte order */
    /* INADDR_ANY allows clients to connect to any one of the host’s IP address.
    Optionally, use this line if you know the IP to use: */
    my_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    // my_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    memset(my_addr.sin_zero, '\0', sizeof(my_addr.sin_zero));
    /* bind the socket */
    if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) == -1) {
        perror("ERROR: binding.");
        exit(1);
    }

    /*------   enter loop to hear from client  --------*/
    srand(time(0));
    printf("Waiting for request from client.\n");

    int fileNum = 0;
    while (1) {
        /*------   3-way handshaking  --------*/
        header sp;
        uint16_t seq, ack;
        int n;
        n = fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL) & ~O_NONBLOCK);
        if (n < 0)
            errorHandler("Can't set file descriptor to blocking.");

        n = recvfrom(sockfd, &sp, HEADSIZE, 0, (struct sockaddr *)&their_addr, &len);
        if (n < 0) {
            errorHandler("Could not read from client.");
        } else if (sp.SYN_FLAG != 1) {
            continue;
        } else {
            /* received SYN */
            fileNum += 1;
            char fileName[64];
            sprintf(fileName, "%i.file", fileNum);
            FILE *log = fopen(fileName, "w");

            printLine(RECV, &sp, 0);
            /* send SYN + ACK */
            seq = rand() % SEQNUM;
            ack = sp.seq + 1;
            header sa;
            createHeader(&sa, seq, ack, 1, 1, 0);
            n = sendto(sockfd, &sa, HEADSIZE, 0, (struct sockaddr *)&their_addr, len);
            if (n <= 0) {
                errorHandler("Could not send to client.");
            } else {
                printLine(SEND, &sa, 0);
            }

            seq += 1;
            while (1) {
                packet temp;
                int n = recvfrom(sockfd, &temp, PACKETSIZE, 0, (struct sockaddr *)&their_addr, &len);
                if (n <= 0) {
                    errorHandler("Could not receive from client.");
                } else if (temp.head.FIN_FLAG == 1) {
                    /* receive FIN */
                    printPacket(RECV, &temp, 0);
                    fclose(log);
                    break;
                } else if (temp.head.SYN_FLAG == 1) {
                    /* receive duplicated SYN, resend ACK */
                    printPacket(RECV, &temp, 0);
                    printLine(RESEND, &sa, 0);
                    if (rand() % 10 >= loseProb) {
                        n = sendto(sockfd, &sa, HEADSIZE, 0, (struct sockaddr *)&their_addr, len);
                        if (n <= 0);
                            errorHandler("Could not send to client.");
                    } else {
                        printf("DUP ACK: %d lost.\n", sa.ack);
                    }
                } else if (temp.head.seq != ack) {
                    printPacket(RECV, &temp, 0);
                    header dupAck;
                    createHeader(&dupAck, seq, ack, 1, 0, 0);
                    printLine(SEND, &dupAck, 1);
                    if (rand() % 10 >= loseProb) {
                        n = sendto(sockfd, &dupAck, HEADSIZE, 0, (struct sockaddr *)&their_addr, len);
                        if (n < 0)
                            errorHandler("Failed to send to client");
                    } else {
                        printf("DUP ACK: %d lost.\n", dupAck.ack);
                    }
                } else {
                    printPacket(RECV, &temp, 0);
                    ack += temp.head.len;
                    ack = (ack > SEQNUM) ? (ack % (SEQNUM + 1)) : ack;
                    fwrite(temp.payload, 1, temp.head.len, log);
                    header tempAck;
                    createHeader(&tempAck, seq, ack, 1, 0, 0);
                    printLine(SEND, &tempAck, 0);
                    if (rand() % 10 >= loseProb) {
                        n = sendto(sockfd, &tempAck, HEADSIZE, 0, (struct sockaddr *)&their_addr, len);
                        if (n < 0)
                            errorHandler("Failed to send to the client.");
                    } else {
                        printf("ACK: %d lost.\n", tempAck.ack);
                    }
                }
            }
        }

        finish(seq, ack);
    }

    close(sockfd);
    return 0;
}

void finish(int seq, int ack) {
    /* received FIN, send ACK (fin1), then send FIN (fin2),
    and finally should receive another ACK(fin3) */
    int n;
    header fin1;
    ack += 1;
    createHeader(&fin1, seq, ack, 1, 0, 0);
    n = sendto(sockfd, &fin1, HEADSIZE, 0, (struct sockaddr *)&their_addr, len);
    if (n < 0) {
        errorHandler("Can't send to client.");
    } else {
        printLine(SEND, &fin1, 0);
    }

    header fin2;
    createHeader(&fin2, seq, 0, 0, 0, 1);
    n = sendto(sockfd, &fin2, HEADSIZE, 0, (struct sockaddr *)&their_addr, len);
    if (n < 0) {
        errorHandler("Can't send to client.");
    } else {
        printLine(SEND, &fin2, 0);
    }

    n = fcntl(sockfd, F_SETFL, O_NONBLOCK);
    if (n < 0)
        errorHandler("Can't set file descriptor to non-blocking.");
    header fin3;
    time_t finTime = time(0);
    while (1) {
        if ((time(0) - finTime) > 2.0) {
            printf("TIMEOUT for receiving the final ACK(seq: %i), closed.\n", seq);
            printf("connection closed.\n");
            break;
        } else {
            n = recvfrom(sockfd, &fin3, HEADSIZE, 0, (struct sockaddr *)&their_addr, &len);
            if (n != -1) {
                printLine(RECV, &fin3, 0);
                printf("connection closed.\n");
                break;
            }
        }
    }
}
