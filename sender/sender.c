#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include "../checksum.h"

#pragma comment(lib, "ws2_32.lib")

#define TARGET_PORT 5555
#define TARGET_IP "127.0.0.1"

#define BUFF_SIZE 1024
#define DFRAME_SIZE 1020
#define CRC_LEN_BYTES 32 / 8
#define HEADER_LENGTH 4 + CRC_LEN_BYTES

// PACKET FORMAT: (max size: 1024)
// first packet: [zero(4), file len(4), crc(CRC_LEN_BYTES), file name(-)]
// mid packet:   [file index(4), crc(CRC_LEN_BYTES), content(-)]
// last packet:  [...]

typedef struct {
    WSADATA wsa_data;
    SOCKET socket_handle;
    struct sockaddr_in target_address;
} SockWrapper;


int sock_send(SockWrapper* s_wrapper, char* message, int message_length);
int gen_next_packet(FILE* file, char* result);
int init_socket(SockWrapper* sock_wrapper);
int send_file(SockWrapper s_wrapper, FILE* input_file, char* filename);

int main(int argc, char** argv) {
    SockWrapper s_wrapper;
    if (init_socket(&s_wrapper) != 0) return 1;

    FILE* input_file = fopen(argv[1], "rb");
    if (input_file == NULL){ printf("couldn't open file at location %s", argv[1]);
        return 1;
    }
    if (send_file(s_wrapper, input_file, argv[1]) != 0){
        return 1;
    }

    // Cleanup
    closesocket(s_wrapper.socket_handle);
    WSACleanup();
    fclose(input_file);
    return 0;
}

int send_file(SockWrapper s_wrapper, FILE* input_file, char* filename){
    fseek(input_file, 0L, SEEK_END);
    int file_len = ftell(input_file);
    fseek(input_file, 0L, SEEK_SET);

    char message[BUFF_SIZE] = {0};
    int message_length = HEADER_LENGTH + 4 + strlen(filename);
    //first_packet
    memcpy(message + 4, &file_len, sizeof(int));
    memcpy(message + 8, filename, strlen(filename) + 1);
    if (sock_send(&s_wrapper, message, message_length)) return 1;
    printf("%d ", message[4]);
    printf("%s\n", &message[8]);

    int file_index = 1;
    while (1){
        memcpy(message, &file_index, sizeof(int));
        message_length = gen_next_packet(input_file, message) + HEADER_LENGTH;
        if (message_length == HEADER_LENGTH)
            break;

        // Send the message
        if(sock_send(&s_wrapper, message, message_length)) return 1;

        printf("%d ", message[0]);
        printf("%s\n", &message[4]);
        file_index++;
    }
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
 * specified. The array starts with the CRC code for all data that follows.*/
int gen_next_packet(FILE* file, char* result){
    int bytes_read = fread(&(result[HEADER_LENGTH]), 1, DFRAME_SIZE - HEADER_LENGTH, file);
    int crc = crc_32(&(result[HEADER_LENGTH]), bytes_read);
    for (int i = 0; i < 4; i++){
        update_crc_32(crc, result[i]);
    }
    memcpy(&result[4], &crc, CRC_LEN_BYTES);
    return bytes_read;
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


