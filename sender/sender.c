#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

#define TARGET_PORT 5200
#define TARGET_IP "127.0.0.1"

#define BUFF_SIZE 1024
#define DFRAME_SIZE 1020
#define HEADER_LENGTH 4


typedef struct {
    WSADATA wsa_data;
    SOCKET socket_handle;
    struct sockaddr_in target_address;
} SockWrapper;


int sock_send(SockWrapper* s_wrapper, char* message, int message_length);
int gen_next_packet(FILE* file, char* result);
int init_socket(SockWrapper* sock_wrapper);

int main(int argc, char** argv) {
    SockWrapper s_wrapper;
    if (init_socket(&s_wrapper) != 0) return 1;

    FILE* input_file = fopen(argv[1], "rb");
    if (input_file == NULL){ printf("couldn't open file at location %s", argv[1]);
        return 1;
    }
    fseek(input_file, 0L, SEEK_END);
    int file_len = ftell(input_file);
    fseek(input_file, 0L, SEEK_SET);

    char message[BUFF_SIZE] = {0};
    int message_length = HEADER_LENGTH + 4 + strlen(argv[1]);
    //first_packet
    memcpy(message + 4, &file_len, sizeof(int));
    memcpy(message + 8, argv[1], strlen(argv[1]) + 1);
    if (sock_send(&s_wrapper, message, message_length)) return 1;
    printf("%d ", message[4]);
    printf("%s\n", &message[8]);

    int file_index = 1;
    while (1){
        memcpy(message, &file_index, sizeof(int));
        message_length = gen_next_packet(input_file, &message[4]) + HEADER_LENGTH;
        if (message_length == HEADER_LENGTH)
            break;

        // Send the message
        if(sock_send(&s_wrapper, message, message_length)) return 1;

        printf("%d ", message[0]);
        printf("%s\n", &message[4]);
        file_index++;
    }


    // Cleanup
    closesocket(s_wrapper.socket_handle);
    WSACleanup();
    fclose(input_file);
    return 0;
}

int sock_send(SockWrapper* s_wrapper, char* message, int message_length) {
    if (sendto(s_wrapper->socket_handle, message, message_length, 0,
             (struct sockaddr*)&(s_wrapper->target_address), sizeof(s_wrapper->target_address)) == SOCKET_ERROR) {
        printf("sendto() failed. Error: %d\n", WSAGetLastError());
        closesocket(s_wrapper->socket_handle);
        WSACleanup();
        return 1;
        }
    return 0;
}


/* Returns the number of bytes read, -1 on error. Fills the result array with the next DFRAME_SIZE bytes of the file
 * specified.*/
int gen_next_packet(FILE* file, char* result){
    return fread(result, 1, DFRAME_SIZE, file);
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


