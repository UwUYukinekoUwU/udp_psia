#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include "../checksum.h"
#include "../hash/sha1.h"

#pragma comment(lib, "ws2_32.lib")

#define PORT 5200
#define CRC_LEN_BYTES 32 / 8
#define HEADER_LENGTH 4 + CRC_LEN_BYTES
#define BUFFER_SIZE 1024
#define CONFIRMATION_SIZE 8

typedef struct {
    int id;
    int crc;
    char* content;
    int content_length;
} Packet;

void toFile(FILE* out, char* buffer, int bytesReceived);
Packet parsePacket(char* buffer, int bytesReceived);
int checkCRC(Packet packet);
void sendConfirmation(SOCKET socketHandle, struct sockaddr_in* senderAddress, int id);
int checkHash(char* fileName, uint8_t* receivedHash);

int main() {
    WSADATA wsaData;
    SOCKET socketHandle;
    struct sockaddr_in serverAddress, senderAddress;
    char buffer[BUFFER_SIZE];
    int senderAddressSize = sizeof(senderAddress);
    int bytesReceived;

    // Winsock Initialization
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup failed. Error: %d\n", WSAGetLastError());
        return 1;
    }

    // Create UDP Socket
    if ((socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET) {
        printf("Socket creation failed. Error: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    // Bind Socket
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(PORT);
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    if (bind(socketHandle, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR) {
        printf("Bind failed. Error: %d\n", WSAGetLastError());
        closesocket(socketHandle);
        WSACleanup();
        return 1;
    }

    printf("Listening for UDP messages on port %d...\n", PORT);

    FILE* file = NULL;
    int received_len = 0, lastConfirmed = -1;

    // Main Loop
    while (1) {
        bytesReceived = recvfrom(socketHandle, buffer, sizeof(buffer), 0,
                                 (struct sockaddr*)&senderAddress, &senderAddressSize);
        if (bytesReceived == SOCKET_ERROR) {
            printf("recvfrom() failed. Error: %d\n", WSAGetLastError());
            break;
        }

        printf("received");

        Packet packet = parsePacket(buffer, bytesReceived);

        // First Packet: Initialize File
        if (packet.id == 0) {
            if (!checkCRC(packet)) {
                lastConfirmed = 0;
                sendConfirmation(socketHandle, &senderAddress, lastConfirmed);

                //file_len = *((int*)(packet.content));
                char* filename = packet.content + sizeof(int);

                file = fopen(filename, "wb");
                if (!file) {
                    printf("Failed to open file. Redirecting to stdout.\n");
                    file = stdout;
                }
                printf("Receiving file: %s\n", filename);
            } else {
                printf("CRC error in first packet.\n");
                sendConfirmation(socketHandle, &senderAddress, -1);
            }
        }
        // Subsequent Packets
        else if (packet.id == lastConfirmed + 1) {
            if (!checkCRC(packet)) {
                lastConfirmed = packet.id;
                sendConfirmation(socketHandle, &senderAddress, lastConfirmed);

                toFile(file, packet.content, packet.content_length);
                received_len += packet.content_length;


                if (packet.id == -2){
                    printf("File transfer complete.\n");
                    break;
                }

                /*if (received_len >= file_len) {
                    printf("File transfer complete.\n");
                    break;
                }
                */
            } else {
                printf("CRC error in packet %d.\n", packet.id);
                sendConfirmation(socketHandle, &senderAddress, lastConfirmed);
            }
        } else {
            printf("Out-of-order packet received. Expected %d, got %d.\n", lastConfirmed + 1, packet.id);
            sendConfirmation(socketHandle, &senderAddress, lastConfirmed);
        }

        free(packet.content);
    }

    if (file && file != stdout) fclose(file);
    closesocket(socketHandle);
    WSACleanup();
    return 0;
}

Packet parsePacket(char* buffer, int bytesReceived) {
    Packet packet;
    packet.id = *((int*)buffer);
    
    packet.content_length = bytesReceived - HEADER_LENGTH;
    // Subsequent packets: Content includes file data
    packet.crc = *((int*)(buffer + 4));
    packet.content = malloc(packet.content_length);
    if (!packet.content) {
        printf("Memory allocation failed for packet content.\n");
        exit(1);
    }
    memcpy(packet.content, buffer + HEADER_LENGTH, packet.content_length);
    return packet;
}

int checkCRC(Packet packet) {
    int computed_crc = crc_32(packet.content, packet.content_length);
    unsigned char* id_bytes = (unsigned char*)&packet.id;
    for (int i = 0; i < 4; i++){
        computed_crc = update_crc_32(computed_crc, id_bytes[i]);
    }
    return computed_crc != packet.crc;
}

void toFile(FILE* out, char* buffer, int bytesReceived) {
    fwrite(buffer, sizeof(char), bytesReceived, out);
}

void sendConfirmation(SOCKET socketHandle, struct sockaddr_in* senderAddress, int id) {

    char confirmation[CONFIRMATION_SIZE] = {0};
    memcpy(confirmation, &id, sizeof(id));
    int crc = crc_32(confirmation, CONFIRMATION_SIZE);
    memcpy(confirmation + sizeof(id), &crc, sizeof(crc));

    sendto(socketHandle, confirmation, CONFIRMATION_SIZE, 0, (struct sockaddr*)senderAddress, sizeof(*senderAddress));
}

int checkHash(char* fileName, uint8_t* receivedHash) {

    FILE* file = fopen(fileName, "rb");
    if (!file) {
        printf("Failed to open file for hashing.\n");
        return 0;
    }

    fseek(file, 0, SEEK_END);
    long filesize = ftell(file);
    rewind(file);

    char* buffer = malloc(filesize);
    if (!buffer) {
        printf("Memory allocation failed for file buffer.\n");
        fclose(file);
        return 0;
    }

    fread(buffer, 1, filesize, file);
    fclose(file);

    SHA1_CTX sha1;
    uint8_t results[20];
    SHA1Init(&sha1);
    SHA1Update(&sha1, (unsigned char*)buffer, filesize);
    SHA1Final(results, &sha1);

    printf("SHA-1 Hash: ");
    for (int i = 0; i < 20; i++) {
        printf("%02x", results[i]);
    }
    printf("\n");

    printf("Received Hash: ");
    for (int i = 0; i < 20; i++) {
        printf("%02x", receivedHash[i]);
    }

    if (memcmp(results, receivedHash, 20) == 0) {
        printf("Hash match!\n");
    } else {
        printf("Hash mismatch!\n");
    }
    free(buffer);
    return memcmp(results, receivedHash, 20) == 0 ? 1 : 0;
    
}