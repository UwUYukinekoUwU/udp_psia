#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

#define TARGET_PORT 12345
#define TARGET_IP "127.0.0.1"

int main() {
    WSADATA wsaData;
    SOCKET socketHandle;
    struct sockaddr_in targetAddress;
    char message[] = "Hello, UDP Server!";
    int messageLength = sizeof(message);

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

    // Setup target address structure
    targetAddress.sin_family = AF_INET;
    targetAddress.sin_port = htons(TARGET_PORT);
    targetAddress.sin_addr.s_addr = inet_addr(TARGET_IP);

    // Send the message
    if (sendto(socketHandle, message, messageLength, 0,
               (struct sockaddr*)&targetAddress, sizeof(targetAddress)) == SOCKET_ERROR) {
        printf("sendto() failed. Error: %d\n", WSAGetLastError());
        closesocket(socketHandle);
        WSACleanup();
        return 1;
    }

    printf("Sent message: %s\n", message);

    // Cleanup
    closesocket(socketHandle);
    WSACleanup();
    return 0;
}