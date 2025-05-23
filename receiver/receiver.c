#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 5200

void toFile(FILE* out, char* buffer, int bytesReceived);
int getId(char* buffer);
int getLength(char* buffer);
char* getFileName(char* buffer, int bytesReceived);

int main() {
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

    FILE* file;
    int opened = 0;
    int file_len = 0;
    int received_len = 0;

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

        //first packet, must create new file
        if(getId(buffer) == 0 && RUNNING == 1){

            //get message length and name of file
            file_len = getLength(buffer);
            char* filename = getFileName(buffer, bytesReceived);

            if (filename != NULL){
                file = fopen(filename,"wb");
                if (file != NULL){
                    opened = 1;
                    printf("Receiving file: %s\n",filename);
                    free(filename);
                }
            }
            //if file could not be opened redirect to stdout
            if (file == NULL){
                printf("Output file could not be opened/created\n");
                printf("redirecting to stdout\n");
                file = stdout;
                opened = 1;
            }
        }
        //if other than first then write to file if file is opened
        else if (opened && RUNNING == 1){
            toFile(file, buffer, bytesReceived);
            received_len += bytesReceived - 4;
        }

        printf("Received from %s:%d - %s\n",
               inet_ntoa(senderAddress.sin_addr),
               ntohs(senderAddress.sin_port),
               buffer);

        if (received_len == file_len)
        {
            RUNNING = 0;
        }
    }

    printf("Bytes received: %d\nBytes to be received: %d\n", received_len, file_len);

    if (opened == 1){
        fclose(file);
    }

    // Cleanup
    closesocket(socketHandle);
    WSACleanup();
    return 0;
}


/* Writes contents of buffer to specified output file
 * @param out     = output stream (file)
 * @param buffer  = char array of incomming data (last char must be '\0')
 */
void toFile(FILE* out, char* buffer, int bytesReceived){
    int i = 4;
    while (i < bytesReceived){
        fwrite(&buffer[i], sizeof(char), 1, out);
        i++;
    }
}

/* Reads first 4 bytes as int value of index of current packet
 * @param buffer  = char array of incomming data (last char must be '\0')
 * return index of current packet
 */
int getId(char* buffer){
    int id =0;
    id = ((unsigned char)buffer[0] | (unsigned char)buffer[1] << 8 | (unsigned char)buffer[2] << 16 | (unsigned char)buffer[3] << 24);
    printf("message id: %d\n", id);
    return id;
}

/* Reads second 4 bytes as int value of lenght of incomming data
 * @param buffer  = char array of incomming data (last char must be '\0')
 * return file length
 */
int getLength(char* buffer){
    int length = 0;
    length = ((unsigned char)buffer[4] | (unsigned char)buffer[5] << 8 | (unsigned char)buffer[6] << 16 | (unsigned char)buffer[7] << 24);
    return length;
}

/* Reads file name from packet
 * @param buffer         = char array of incomming data (last char must be '\0')
 * @param bytesReceived  = number of bytes received
 * return name of file
 */
char* getFileName(char* buffer, int bytesReceived){
    char* fileName = malloc(bytesReceived-8);
    if (fileName == NULL){
        return NULL;
    }

    int i;
    for (i = 8; i < bytesReceived && buffer[i] != '\0'; i++){
        fileName[i-8] = buffer[i]; 
    }
    fileName[i-8] = '\0';
    return fileName;
}
