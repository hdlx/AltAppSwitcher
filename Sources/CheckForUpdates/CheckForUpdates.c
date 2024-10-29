#include <psdk_inc/_ip_types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

void GetAASVersion(int* major, int* minor)
{
    *major = 0;
    *minor = 0;

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
        res = res->ai_next;
    }

    if (connect(sock, sockAddr, sizeOfSockAddr))
    {
        WSACleanup();
        return;
    }

   const char message[] =
       "GET /aasversion HTTP/1.1\r\nHost: www.hamtarodeluxe.com\r\n\r\n";

    if (SOCKET_ERROR == send(sock, message, strlen(message), 0))
    {
        close(sock);
        WSACleanup();
        return;
    }

    char response[1024];
    memset(response, 0, sizeof(response));
    if (SOCKET_ERROR == recv(sock, response, sizeof(response), 0))
    {
        close(sock);
        WSACleanup();
        return;
    }

    close(sock);
    WSACleanup();

    const char version[] = "\"Version\": ";
    char* at = strstr(response, version);

    if (at == NULL)
        return;

    sscanf(at, "\"Version\": %i.%i", major, minor);

    return;
}

#define MAJOR 0
#define MINOR 19
int main()
{
    int major, minor;
    GetAASVersion(&major, &minor);
    if ((major > MAJOR) ||
        (major == MAJOR && minor > MINOR))
    {
        char msg[256];
        sprintf(msg,
            "msg * \"A new version of AltAppSwitcher is available (%d.%d). Please check https://github.com/hdlx/AltAppSwitcher/releases\" &",
            major, minor);
        system("msg * \"A new version of AltAppSwitcher is available (). Please check https://github.com/hdlx/AltAppSwitcher/releases\" &");
    }
    return 0;
}
