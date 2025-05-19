#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <winsock2.h>
#include "../checksum.h"
#include "../hash/sha1.h"

#pragma comment(lib, "ws2_32.lib")

#define PORT 5200
#define CRC_LEN_BYTES (32 / 8)
#define HEADER_LENGTH (4 + CRC_LEN_BYTES)
#define BUFFER_SIZE 1024
#define CONFIRMATION_SIZE 8
#define MAX_PACKETS 2048
#define TIMEOUT_MS 100000
#define CORRECT -1
#define TIMEOUT_ID -9999

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
int checkHash(uint8_t* receivedHash);
Packet receive(SOCKET socketHandle);

WSADATA wsaData;
SOCKET socketHandle;
struct sockaddr_in serverAddress, senderAddress;
int senderAddressSize;
char *filename;

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

    Packet packetBuffer[MAX_PACKETS];
    int received[MAX_PACKETS] = {0};
    int nextExpected = 0;
    int endReceived = 0;
    FILE *file = NULL;

    while (!endReceived) {
        Packet packet = receive(socketHandle);

        if (packet.id == TIMEOUT_ID) continue;
        if (checkCRC(packet)) {
            printf("CRC mismatch on packet ID: %d\n", packet.id);
            continue;
        }

        if (packet.id == -2) {
            sendConfirmation(socketHandle, &senderAddress, -2);
            endReceived = 1;
            continue;
        }

        if (packet.id < 0 || packet.id >= MAX_PACKETS) continue;

        if (received[packet.id]) {
            sendConfirmation(socketHandle, &senderAddress, packet.id);
            continue;
        }
        packetBuffer[packet.id] = packet;
        received[packet.id] = 1;
        printf("Received packet ID: %d\n", packet.id);
        sendConfirmation(socketHandle, &senderAddress, packet.id);

        while (received[nextExpected]) {
            if (nextExpected == 0) {
                filename = malloc(packetBuffer[0].content_length + 1);
                memcpy(filename, packetBuffer[0].content, packetBuffer[0].content_length);
                filename[packetBuffer[0].content_length] = '\0';
                file = fopen(filename, "wb");
            } else {
                toFile(file, packetBuffer[nextExpected].content, packetBuffer[nextExpected].content_length);
            }

            free(packetBuffer[nextExpected].content);
            nextExpected++;
        }
    }

    fclose(file);

    uint8_t hash[20];
    memcpy(hash, packetBuffer[nextExpected].content, 20);
    checkHash(hash);

    free(packetBuffer[nextExpected].content);
    free(filename);

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
            errorPacket.id = TIMEOUT_ID; // special error value
            errorPacket.crc = 0;
            errorPacket.content = NULL;
            errorPacket.content_length = 0;
            return errorPacket;
        }
    }

    Packet packet = parsePacket(buffer, bytesReceived);
    return packet;

}

Packet parsePacket(char* buffer, int bytesReceived) {
    Packet packet;
    packet.id = *((int*)buffer);

    packet.content_length = bytesReceived - HEADER_LENGTH;
    packet.content = NULL;
    // Subsequent packets: Content includes file data
    packet.crc = *((int*)(buffer + 4));

    if (packet.content_length <= 0){
        return packet;
    }

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

    printf("Sending confirmation for packet ID: %d\n", id);
    char confirmation[CONFIRMATION_SIZE] = {0};
    memcpy(confirmation, &id, sizeof(id));
    int crc = crc_32(confirmation, CONFIRMATION_SIZE);
    memcpy(confirmation + sizeof(id), &crc, sizeof(crc));

    senderAddress->sin_port = htons(5202);
    sendto(socketHandle, confirmation, CONFIRMATION_SIZE, 0, (struct sockaddr*)senderAddress, sizeof(*senderAddress));
}

int checkHash(uint8_t* receivedHash) {

    FILE* file = fopen(filename, "rb");
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
    printf("\n");

    if (memcmp(results, receivedHash, 20) == 0) {
        printf("Hash match!\n");
    } else {
        printf("Hash mismatch!\n");
    }
    free(buffer);
    return memcmp(results, receivedHash, 20) == 0 ? 1 : 0;

}