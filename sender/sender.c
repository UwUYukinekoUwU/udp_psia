#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <unistd.h>
#include "../checksum.h"
#include "../hash/sha1.h"

#pragma comment(lib, "ws2_32.lib")

#define TARGET_PORT 5200

#define BUFF_SIZE 1024
#define DFRAME_SIZE 1020
#define CRC_LEN_BYTES (32 / 8)
#define HEADER_LENGTH (4 + CRC_LEN_BYTES)
#define TIMEOUT_MS 100000
#define RESEND_TRIES 3
#define WINDOW_LEN 5

// PACKET FORMAT: (max size: 1024)
// first packet: [zero(4), crc(CRC_LEN_BYTES), file name(-)]
// mid packet:   [file index(4), crc(CRC_LEN_BYTES), content(-)]
// last packet:  [-2(4), crc(CRC_LEN_BYTES), hash(8)] ??? idfk 0 is taken

typedef struct {
    WSADATA wsa_data;
    SOCKET socket_handle;
    struct sockaddr_in target_address;
} SockWrapper;

typedef struct {
    int file_index;
    int data_len;
    char* data;
} Packet;



int sock_send(SockWrapper* s_wrapper, Packet* packet);
int gen_next_packet(FILE* file, char* result);
int init_socket(SockWrapper* sock_wrapper, char* target_ip);
int send_file(SockWrapper s_wrapper, FILE* input_file);
int try_sock_receive(SockWrapper* s_wrapper, int* received_index);
Packet* gen_first_packet(char* message);
Packet* gen_last_packet(FILE* file, char* message);
int resend_cycle(SockWrapper s_wrapper, char* message, int message_length, int file_index);
int gen_hash(FILE* file, uint8_t* hash_to_fill);
Packet* gen_packet_struct(int file_index, char* message, int message_length);
int send_batch(SockWrapper* s_wrapper);
int await_window_confirm(SockWrapper* s_wrapper);
void free_packet(Packet* p);
void clean_miss_map();
void null_miss_map(int file_index);
int miss_map_is_empty();



Packet* miss_map[WINDOW_LEN] = {0};
char* filename;


int main(int argc, char** argv) {
    SockWrapper s_wrapper;
    if (init_socket(&s_wrapper, argv[2]) != 0) return 1;

    filename = argv[1];

    FILE* input_file = fopen(argv[1], "rb");
    if (input_file == NULL){ printf("couldn't open file at location %s", argv[1]);
        return 1;
    }
    if (send_file(s_wrapper, input_file) != 0){
        return 1;
    }

    // Cleanup
    closesocket(s_wrapper.socket_handle);
    WSACleanup();
    clean_miss_map();
    fclose(input_file);
    return 0;
}

int send_file(SockWrapper s_wrapper, FILE* input_file){
    char message[BUFF_SIZE] = {0};
    int file_index = 1;
    int finished = 0;
    int base;

    miss_map[0] = gen_first_packet(message);

    while (!finished){
        base = file_index;
        for(int i = base; i < base + WINDOW_LEN; i++){
            if (miss_map[i - base] != NULL) //this packet send failed
                continue;

            int message_length = gen_next_packet(input_file, message) + HEADER_LENGTH;
            if (message_length == HEADER_LENGTH){ //last packet
                miss_map[i - base] = gen_last_packet(input_file, message);
                finished = 1;
            }
            else{
                miss_map[i - base] = gen_packet_struct(file_index, message, message_length);
            }
            file_index++;
        }
        if(send_batch(&s_wrapper)) return 1;
        if(await_window_confirm(&s_wrapper)) return 1;
    }

    //last window may fail and still wait for send
    while(!miss_map_is_empty()){
        if(send_batch(&s_wrapper)) return 1;
        if(await_window_confirm(&s_wrapper)) return 1;
    }

    return 0;
}

int miss_map_is_empty(){
    for(int i = 0; i < sizeof(miss_map) / sizeof(Packet*); i++){
        if (miss_map[i] != NULL) return 0;
    }
    return 1;
}

int send_batch(SockWrapper* s_wrapper){
    for(int i = 0; i < sizeof(miss_map) / sizeof(Packet*); i++){
        if (miss_map[i] == NULL) continue;
        if(sock_send(s_wrapper, miss_map[i])) return 1;
    }
    return 0;
}

void null_miss_map(int file_index){
    int miss_map_size = sizeof(miss_map) / sizeof(Packet*);
    if (file_index < 0 || file_index >= miss_map_size) return;

    for(int i = 0; i < miss_map_size; i++){
        if (miss_map[i]->file_index == file_index){
            free_packet(miss_map[i]);
            miss_map[i] = NULL;
        }
    }
}

int await_window_confirm(SockWrapper* s_wrapper){
    int received_index = 0;
    int listen_result;
    for(int i = 0; i < WINDOW_LEN; i++){
        listen_result = try_sock_receive(s_wrapper, &received_index);
        if (listen_result == 1) return 1;
        if (listen_result == 2) break;
        null_miss_map(received_index);
    }
    return 0;
}


int sock_send(SockWrapper* s_wrapper, Packet* packet) {
    memcpy(packet->data, &(packet->file_index), sizeof(int));
    if (sendto(s_wrapper->socket_handle, packet->data, packet->data_len, 0,
             (struct sockaddr*)&(s_wrapper->target_address), sizeof(s_wrapper->target_address)) == SOCKET_ERROR) {
        printf("sendto() failed. Error: %d\n", WSAGetLastError());
        closesocket(s_wrapper->socket_handle);
        WSACleanup();
        return 1;
        }
    return 0;
}

int try_sock_receive(SockWrapper* s_wrapper, int* received_index){
    int buffer[BUFF_SIZE / sizeof(int)] = {0};
    struct sockaddr sender_address;
    int sender_addr_len = sizeof(sender_address);
    int bytes_received = recvfrom(s_wrapper->socket_handle, (char*)buffer, sizeof(buffer), 0,
                                  (struct sockaddr*)&sender_address, &sender_addr_len);
    if (bytes_received == SOCKET_ERROR || bytes_received == 0) {
        if (WSAGetLastError() == WSAETIMEDOUT) return 2;
        printf("recvfrom() failed. Error: %d\n", WSAGetLastError());
        closesocket(s_wrapper->socket_handle);
        WSACleanup();
        return 1;
    }
    int crc = crc_32((char*)buffer, 4);
    printf("confirm {crc: %d file_index: %d}\n", crc, buffer[0]);
    //crc check ->

    return 0;
}

/* Returns the number of bytes read, -1 on error. Fills the result array with the next DFRAME_SIZE bytes of the file
 * specified. The array starts with the CRC code for all data that follows.*/
int gen_next_packet(FILE* file, char* result){
    int bytes_read = fread(&result[HEADER_LENGTH], 1, DFRAME_SIZE - HEADER_LENGTH, file);
    int crc = crc_32(&result[HEADER_LENGTH], bytes_read);
    for (int i = 0; i < 4; i++){
        crc = update_crc_32(crc, result[i]);
    }
    memcpy(&result[4], &crc, CRC_LEN_BYTES);
    return bytes_read;
}

Packet* gen_first_packet(char* message){
    int message_length = HEADER_LENGTH + strlen(filename);
    int crc = crc_32(filename, strlen(filename));
    for (int i = 0; i < 4; i++) {
        crc = update_crc_32(crc, message[i]);
    }
    memcpy(message + 4, &crc, CRC_LEN_BYTES);
    memcpy(message + 8, filename, strlen(filename) + 1);

    Packet* p = gen_packet_struct(0, message, message_length);
    printf("%d \n", message[0]);
    return p;
}

Packet* gen_last_packet(FILE* file, char* message){
    int last = -2;
    int message_length = HEADER_LENGTH + sizeof(uint8_t);
    uint8_t hash = 0;
    gen_hash(file, &hash);
    memcpy(message, &last, sizeof(int));

    int crc = crc_32(&hash, sizeof(uint8_t));
    for (int i = 0; i < 4; i++) {
        crc = update_crc_32(crc, message[i]);
    }

    memcpy(message + 4, &crc, CRC_LEN_BYTES);
    memcpy(message + 8, &hash, sizeof(uint8_t));
    Packet* p = gen_packet_struct(-2, message, message_length);

    printf("%d \n", message[0]);
    return p;
}

int resend_cycle(SockWrapper s_wrapper, char* message, int message_length, int file_index){
//    int resend_tries = RESEND_TRIES;
//    int success = 0;
//    while ((--resend_tries) != 0){
//        int result;
//        if ((result = try_sock_receive(&s_wrapper, file_index)) == 0) { success = 1; break; }
//        if (result == 2) {printf("Error: Socket timeout\n"); continue; }
//        if (sock_send(&s_wrapper, message, message_length)) return 1;
//    }
//    if (!success) return 1;
//    return 0;
}

/*makes a new copy in memory of the message*/
Packet* gen_packet_struct(int file_index, char* message, int message_len) {
    char* copied_message = malloc(sizeof(char) * message_len);
    memcpy(copied_message, message, message_len * sizeof(char));
    Packet* p = (Packet*)malloc(sizeof(Packet));
    *p = (Packet) {
        .file_index = file_index,
        .data = copied_message,
        .data_len = message_len
    };
    return p;
}

void free_packet(Packet* p) {
    if (p != NULL) {
        if (p->data != NULL)free(p->data);
        free(p);
    }
}

int gen_hash(FILE* file, uint8_t* hash_to_fill) {
    rewind(file);
    fseek(file, 0, SEEK_END);
    long filesize = ftell(file);
    rewind(file);

    char* buffer = malloc(filesize);
    if (!buffer) {
        printf("Memory allocation failed for file buffer.\n");
        return 1;
    }
    fread(buffer, 1, filesize, file);

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

    free(buffer);
    memcpy(hash_to_fill, results, sizeof(uint8_t));

    return 0;
}

void clean_miss_map() {
    for (int i = 0; i < WINDOW_LEN; i++) {
        if (miss_map[i] != NULL) {
            free_packet(miss_map[i]);
            miss_map[i] = NULL;
        }
    }
}


int init_socket(SockWrapper* sock_wrapper, char* target_ip){
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
    DWORD timeout = TIMEOUT_MS;
    if (setsockopt(socket_handle, SOL_SOCKET, SO_RCVTIMEO,
                   (const char*)&timeout, sizeof(timeout)) == SOCKET_ERROR) {
        printf("setsockopt failed. Error: %d\n", WSAGetLastError());
        closesocket(socket_handle);
        WSACleanup();
        return 1;
    }

    // Setup target address structure
    target_address.sin_family = AF_INET;
    target_address.sin_port = htons(TARGET_PORT);
    target_address.sin_addr.s_addr = inet_addr(target_ip);

    sock_wrapper->wsa_data = wsa_data;
    sock_wrapper->socket_handle = socket_handle;
    sock_wrapper->target_address = target_address;

    return 0;
}
