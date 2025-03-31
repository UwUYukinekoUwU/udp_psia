#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

#define TARGET_PORT 12345
#define TARGET_IP "127.0.0.1"

#define BUFF_SIZE 1020


typedef struct {
    WSADATA wsa_data;
    SOCKET socket_handle;
    struct sockaddr_in target_address;
} SockWrapper;



int gen_next_packet(FILE* file, char* result);
int init_socket(SockWrapper* sock_wrapper);

int main(int argc, char** argv) {
    SockWrapper s_wrapper;
    if (init_socket(&s_wrapper) != 0) return 1;

    FILE* input_file = fopen(argv[1], "rb");
    if (input_file == NULL){
        printf("couldn't open file at location %s", argv[1]);
        return 1;
    }

    int message_length = 0;

    while (1){
        char message[BUFF_SIZE];

        message_length = gen_next_packet(input_file, message);
        if (message_length == 0)
            break;

        // Send the message
        if (sendto(s_wrapper.socket_handle, message, message_length, 0,
                   (struct sockaddr*)&(s_wrapper.target_address), sizeof(s_wrapper.target_address)) == SOCKET_ERROR) {
            printf("sendto() failed. Error: %d\n", WSAGetLastError());
            closesocket(s_wrapper.socket_handle);
            WSACleanup();
            return 1;
        }

        printf("Sent message: %s\n", message);
    }


    // Cleanup
    closesocket(s_wrapper.socket_handle);
    WSACleanup();
    fclose(input_file);
    return 0;
}

/* Returns the number of bytes read, -1 on error. Fills the result array with the next BUFF_SIZE bytes of the file
 * specified.*/
int gen_next_packet(FILE* file, char* result){
    return fread(result, 1, BUFF_SIZE, file);
}

int init_socket(SockWrapper* sock_wrapper){
    WSADATA wsa_data;
    SOCKET socket_handle;
    struct sockaddr_in target_address;

    // Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        printf("WSAStartup failed. Error: %d\n", WSAGetLastError());
        return 1;
    }

    // UDP socket
    if ((socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET) {
        printf("Socket creation failed. Error: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    // Setup target address structure
    target_address.sin_family = AF_INET;
    target_address.sin_port = htons(TARGET_PORT);
    target_address.sin_addr.s_addr = inet_addr(TARGET_IP);

    sock_wrapper->wsa_data = wsa_data;
    sock_wrapper->socket_handle = socket_handle;
    sock_wrapper->target_address = target_address;

    return 0;
}


