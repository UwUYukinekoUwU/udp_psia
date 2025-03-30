#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 12345

int main() {
    WSADATA wsaData;
    SOCKET socketHandle;
    struct sockaddr_in serverAddress, senderAddress;
    char buffer[1024];
    int senderAddressSize = sizeof(senderAddress);
    int bytesReceived;

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup failed. Error: %d\n", WSAGetLastError());
        return 1;
    }

    // Create UDP socket
    if ((socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET) {
        printf("Socket creation failed. Error: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    // Setup server address structure
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(PORT);
    serverAddress.sin_addr.s_addr = INADDR_ANY;

    // Bind socket
    if (bind(socketHandle, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR) {
        printf("Bind failed. Error: %d\n", WSAGetLastError());
        closesocket(socketHandle);
        WSACleanup();
        return 1;
    }

    printf("Listening for UDP messages on port %d...\n", PORT);

    // Receive message
    bytesReceived = recvfrom(socketHandle, buffer, sizeof(buffer), 0,
                            (struct sockaddr*)&senderAddress, &senderAddressSize);
    if (bytesReceived == SOCKET_ERROR) {
        printf("recvfrom() failed. Error: %d\n", WSAGetLastError());
        closesocket(socketHandle);
        WSACleanup();
        return 1;
    }

    // Add null terminator to received data
    buffer[bytesReceived] = '\0';
    printf("Received from %s:%d - %s\n",
           inet_ntoa(senderAddress.sin_addr),
           ntohs(senderAddress.sin_port),
           buffer);

    // Cleanup
    closesocket(socketHandle);
    WSACleanup();
    return 0;
}