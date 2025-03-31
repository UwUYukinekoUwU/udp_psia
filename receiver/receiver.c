#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 12345

int main(int argc, char** argv) {
    WSADATA wsaData;
    SOCKET socketHandle;
    struct sockaddr_in serverAddress, senderAddress;
    char buffer[1024];
    int senderAddressSize = sizeof(senderAddress);
    int bytesReceived;

    // Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup failed. Error: %d\n", WSAGetLastError());
        return 1;
    }

    // UDP socket
    if ((socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET) {
        printf("Socket creation failed. Error: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(PORT);
    serverAddress.sin_addr.s_addr = INADDR_ANY;

    // Bind
    if (bind(socketHandle, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR) {
        printf("Bind failed. Error: %d\n", WSAGetLastError());
        closesocket(socketHandle);
        WSACleanup();
        return 1;
    }

    printf("Listening for UDP messages on port %d...\n", PORT);

    int RUNNING = 1;

    // Receive message
    while (RUNNING){
        bytesReceived = recvfrom(socketHandle, buffer, sizeof(buffer), 0,
                                 (struct sockaddr*)&senderAddress, &senderAddressSize);
        if (bytesReceived == SOCKET_ERROR) {
            printf("recvfrom() failed. Error: %d\n", WSAGetLastError());
            closesocket(socketHandle);
            WSACleanup();
            return 1;
        }

        if (bytesReceived == 0) RUNNING = 0;
        // Add null
        buffer[bytesReceived] = '\0';
        printf("Received from %s:%d - %s\n",
               inet_ntoa(senderAddress.sin_addr),
               ntohs(senderAddress.sin_port),
               buffer);
    }

    // Cleanup
    closesocket(socketHandle);
    WSACleanup();
    return 0;
}