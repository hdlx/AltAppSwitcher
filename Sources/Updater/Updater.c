#include <psdk_inc/_ip_types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define MAJOR 0
#define MINOR 19

static void GetAASVersion(int* major, int* minor, SOCKET sock)
{
    *major = 0;
    *minor = 0;
    const char message[] =
       "GET /aasversion HTTP/1.1\r\nHost: www.hamtarodeluxe.com\r\n\r\n";

    if (SOCKET_ERROR == send(sock, message, strlen(message), 0))
        return;

    char response[1024];
    memset(response, 0, sizeof(response));
    if (SOCKET_ERROR == recv(sock, response, sizeof(response), 0))
        return;

    const char version[] = "\"Version\": ";
    char* at = strstr(response, version);

    if (at == NULL)
        return;

    sscanf(at, "\"Version\": %i.%i", major, minor);

    return;
}

static void DownloadLatest(SOCKET sock)
{
    const char message[] =
#if defined(ARCH_x86_64)
       "GET /aasarchives/AltAppSwitcher_x86_64.zip HTTP/1.1\r\nHost: www.hamtarodeluxe.com\r\n\r\n";
#elif defined(ARCH_aarch64)
       "GET /aasarchives/AltAppSwitcher_aarch64.zip HTTP/1.1\r\nHost: www.hamtarodeluxe.com\r\n\r\n";
#else
       "GET /aasarchives/AltAppSwitcher_x86_64.zip HTTP/1.1\r\nHost: www.hamtarodeluxe.com\r\n\r\n";
// #error
#endif

    if (SOCKET_ERROR == send(sock, message, strlen(message), 0))
    {
        close(sock);
        WSACleanup();
        return;
    }

    int fileSize = 0;
    {
        char response[1024];
        memset(response, '\0', sizeof(response));
        char* p = response + 4;
        while (1)
        {
            if (SOCKET_ERROR == recv(sock, p, 1, 0))
                return;
            if (!strncmp(p - 3, "\r\n\r\n", 4))
                break;
            p++;
        }
        p = response + 4;
        printf("%s", p);
        char* at = strstr(p, "content-length");
        if (at == NULL)
            return;
        sscanf(at, "content-length: %i", &fileSize);
    }

    int bytes = 0;
    FILE* file = fopen("toto.zip","wb");
    while (1)
    {
        static char response[1024];
        memset(response, 0, sizeof(response));
        const int bytesRecv = recv(sock, response, 1024, 0);
        if (bytesRecv == -1)
            return;
        fwrite(response, 1, bytesRecv, file);
        bytes += bytesRecv;
        if (bytes == fileSize)
            break;
    }
    fclose(file);
}

int main()
{
    SOCKET sock = 0;
    // Connects to hamtarodeluxe.com
    {
        WSADATA wsaData;
        ZeroMemory(&wsaData, sizeof(WSADATA));
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
            return 0;

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
            return 0;
        }

        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock < 0)
            return 0;

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
            return 0;
        }
    }

    int major, minor;
    GetAASVersion(&major, &minor, sock);
    if ((MAJOR >= major) && (MINOR >= minor))
    {
        close(sock);
        WSACleanup();
        return 0;
    }
    {
        char msg[256];
        sprintf(msg,
            "msg * \"A new version of AltAppSwitcher is available (%u.%u). Please check https://github.com/hdlx/AltAppSwitcher/releases\" &",
            major, minor);
        system(msg);
    }
    DownloadLatest(sock);

    close(sock);
    WSACleanup();
    return 0;
}
