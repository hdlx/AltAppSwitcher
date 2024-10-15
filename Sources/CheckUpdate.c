#include <psdk_inc/_ip_types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

void GetVersionStr(char* outStr)
{
    WSADATA wsaData;
    ZeroMemory(&wsaData, sizeof(WSADATA));
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return;

    struct addrinfo *res = NULL, hints;

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;
    #define DEFAULT_PORT "80"

    // Resolve the local address and port to be used by the server
    int iResult = getaddrinfo("hamtarodeluxe.com", "http", &hints, &res);
    if (iResult != 0)
    {
        WSACleanup();
        return;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0)
        return;

    void* sockAddr;
    int sizeOfSockAddr = 0;
    while (res)
    {
        char addressStr[64];
        inet_ntop(res->ai_family, res->ai_addr->sa_data, addressStr, 100);
        switch (res->ai_family)
        {
        case AF_INET:
            sockAddr = (struct sockaddr_in*)res->ai_addr;
            sizeOfSockAddr = sizeof(struct sockaddr_in);
            break;
        case AF_INET6:
            sockAddr = (struct sockaddr_in6*) res->ai_addr;
            sizeOfSockAddr = sizeof(struct sockaddr_in6);
            break;
        }
        // inet_ntop(res->ai_family, sockAddr, addressStr, 100);
        // printf ("IPv%d address: %s (%s)\n", res->ai_family == PF_INET6 ? 6 : 4,
        //         addressStr, res->ai_canonname);
        res = res->ai_next;
    }

    struct sockaddr_in clientService; 
    clientService.sin_family = AF_INET;
    clientService.sin_addr.s_addr = inet_addr("87.98.154.146");
    clientService.sin_port = htons(80);

   // if (connect(sock, sockAddr, sizeOfSockAddr))
   (void)sockAddr; (void)sizeOfSockAddr;
    if (connect(sock, (SOCKADDR*) &clientService, sizeof(clientService)))
    {
        int toto = WSAGetLastError();
        (void)toto;
        return;
    }

   //const char message[] =
   //    "GET /aasversion HTTP/1.1\r\n"
   //    "Host: www.hamtarodeluxe.com\r\n"
   //    "Accept: text/html\r\n";
   //    
   const char message[] = "GET / HTTP/1.1\r\n";
        
    if (SOCKET_ERROR == send(sock, message, strlen(message), 0))
        return;

    char response[1024];
    memset(response, 0, sizeof(response));
    if (SOCKET_ERROR == recv(sock, response, strlen(response), 0))
        return;

    close(sock);

    printf("Response:\n%s\n",response);

    strcpy(outStr, response);

    WSACleanup();
    return;
}