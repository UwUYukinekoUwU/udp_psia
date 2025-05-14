#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include "../checksum.h"
#include "../hash/sha1.h"

#pragma comment(lib, "ws2_32.lib")

#define PORT 5200
#define CRC_LEN_BYTES (32 / 8)
#define HEADER_LENGTH (4 + CRC_LEN_BYTES)
#define BUFFER_SIZE 1024
#define CONFIRMATION_SIZE 8
#define WINDOWSIZE 5
#define TIMEOUT_MS 100000

typedef struct {
    int id;
    int crc;
    char* content;
    int content_length;
} Packet;

int writebuffer(Packet *packets, FILE **out);
void toFile(FILE* out, char* buffer, int bytesReceived);
Packet parsePacket(char* buffer, int bytesReceived);
int checkCRC(Packet packet);
void sendConfirmation(SOCKET socketHandle, struct sockaddr_in* senderAddress, int id);
int checkHash(char* fileName, uint8_t* receivedHash);
Packet receive(SOCKET socketHandle);

WSADATA wsaData;
SOCKET socketHandle;
struct sockaddr_in serverAddress, senderAddress;
int senderAddressSize;

int main() {

    senderAddressSize = sizeof(senderAddress);

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

     DWORD timeout = TIMEOUT_MS;
    if (setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO,
                   (const char*)&timeout, sizeof(timeout)) == SOCKET_ERROR) {
        printf("setsockopt failed. Error: %d\n", WSAGetLastError());
        closesocket(socketHandle);
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
    int received_len = 0, lastReceived = -1;
    int window_complete = 0;
    Packet packets[WINDOWSIZE];
    int invalid_packets[WINDOWSIZE] = {0};

    // Main Loop
    while (1) {
        for (int i = 0; i < WINDOWSIZE; i++){
            Packet packet = receive(socketHandle);
            if (packet.id == -9999){
                invalid_packets[i] = lastReceived+1;
                lastReceived++;
            }
            else{
                lastReceived++;
                packets[i] = packet;
                if(!checkCRC(packet)){
                    invalid_packets[i] = -1;
                    if (packet.id == -2){
                        while (i < WINDOWSIZE){
                            invalid_packets[i] = -1;
                            i++;
                        }
                        break;
                    }
                }
                else{
                    invalid_packets[i] = packet.id;
                }
            }
        }

        window_complete = 0;

        while(!window_complete){

            received_len = 0;
            //send confirmations (positive only)
            for (int i= 0; i < WINDOWSIZE; i++){
                if (invalid_packets[i] == -1){
                    sendConfirmation(socketHandle, &senderAddress, packets[i].id);
                    received_len++;
                }
            }

            //todo: if podminka ktera checkuje jestli se id prichoziho packetu shoduje s missing packetem
            //kontrola crc pro ten prichozi packet
            //if rec_len == winsize -> win_compl. = 1

            for (int i = 0; i <WINDOWSIZE; i++){
                if (invalid_packets[i] == -1) continue;

                Packet new = receive(socketHandle);

                if(new.id == -9999){
                    continue;
                }

                if (!checkCRC(new)){
                    if (new.id == invalid_packets[i]){
                        packets[i] = new;
                        invalid_packets[i] = -1;
                        sendConfirmation(socketHandle, &senderAddress, new.id);
                    }
                }
            }
            if (received_len == WINDOWSIZE){
                window_complete = 1;
            }

        }
        //wait for new packets


        writebuffer(packets, &file);
        for (int i = 0; i <WINDOWSIZE;i++){
            free(packets[i].content);
        }
    }

    if (file != stdout) fclose(file);
    closesocket(socketHandle);
    WSACleanup();
    return 0;
}

Packet receive(SOCKET socketHandle){
    char buffer[BUFFER_SIZE];
    int bytesReceived;
    bytesReceived = recvfrom(socketHandle, buffer, sizeof(buffer), 0,
                                (struct sockaddr*)&senderAddress, &senderAddressSize);
    if (bytesReceived == SOCKET_ERROR || bytesReceived == 0) {
        if (WSAGetLastError() == WSAETIMEDOUT){
            printf("recvfrom() failed. Error: %d\n", WSAGetLastError());
            Packet errorPacket;
            errorPacket.id = -9999; // special error value
            errorPacket.crc = 0;
            errorPacket.content = NULL;
            errorPacket.content_length = 0;
            return errorPacket;
        }
    }

    Packet packet = parsePacket(buffer, bytesReceived);
    return packet;

}

int writebuffer(Packet *packets, FILE **out){
    for (int i = 0; i < WINDOWSIZE; i++){
        if (packets[i].id == 0){
            char *filename = packets[i].content;
            *out = fopen(filename, "wb");
        }
        else if(packets[i].id == -2){
            toFile(*out, packets[i].content, packets[i].content_length);
            fclose(*out);
            return 1;
        }
        else{
            toFile(*out, packets[i].content, packets[i].content_length);
        }
    }
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