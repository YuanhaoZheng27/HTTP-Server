#include <iostream>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>
#include <string>

using namespace std;

int main()
{
    // Create a socket
    /*
    int listening = socket(AF_INET, SOCK_STREAM, 0);
    if (listening == -1)
    {
        cerr << "Can't create socket!";
        return -1;
    }
    */
    int status;
    struct addrinfo hints;
    struct addrinfo *servinfo;
    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    status = getaddrinfo(
        nullptr,
        "54000",
        &hints,
        &servinfo
        );

    if (status != 0)
    {
        cerr << "getaddrinfo error: " << gai_strerror(status) << endl;
        return -1;
    }

    int listening = socket(
        servinfo->ai_family,
        servinfo->ai_socktype,
        servinfo->ai_protocol
        );

    if (listening == -1)
    {
        cerr << "Can't create socket!" << endl;
        freeaddrinfo(servinfo);
        return -1;
    }

    // Bind the socket to a IP / port
    /*sockaddr_in hint;
    hint.sin_family = AF_INET;
    hint.sin_port = htons(54000);
    inet_pton(AF_INET, "0.0.0.0", &hint.sin_addr);

    if (::bind(
        listening,
        (sockaddr*)&hint,
        sizeof(hint)
        ) == -1)
    {
        cerr << "Can't bind to IP/port";
        return -2;
    }
    */
    if (::bind(listening,
        servinfo->ai_addr,
        servinfo->ai_addrlen
        ) == -1)
    {
        cerr << "Can't bind to IP/port" << endl;
        close(listening);
        freeaddrinfo(servinfo);
        return -2;
    }

    freeaddrinfo(servinfo);

    // Mark the socket for listening in
    if (listen(listening, SOMAXCONN) == -1)
    {
        cerr << "Can't listen!";
        return -3;
    }

    // Accept a call
    sockaddr_in client;
    socklen_t clientSize = sizeof(client);
    char host[NI_MAXHOST];
    char svc[NI_MAXSERV];

    int clientSocket = accept(listening,
        (sockaddr*)&client,
        &clientSize
        );

    if (clientSocket == -1)
    {
        cerr << "Problem with client connecting!";
        return -4;
    }

    // Close the listening socket
    close(listening);

    memset(host, 0, NI_MAXHOST);
    memset(svc, 0, NI_MAXSERV);

    int result = getnameinfo((sockaddr*)&client,
        sizeof(client),
        host,
        NI_MAXHOST,
        svc,
        NI_MAXSERV,
        0);

    if (result)
    {
        cout << host << " connected on " << svc << endl;
    }
    else
    {
        inet_ntop(AF_INET, &client.sin_addr, host, NI_MAXHOST);
        cout << host << " connected on " << ntohs(client.sin_port) << endl;
    }

    // While receiving- display message, echo message
    char buf[4096];
    while (true)
    {
        // Clear the buffer
        memset(buf, 0, 4096);
        // Wait for a message
        int bytesRecv = recv(clientSocket, buf, 4096, 0);
        if (bytesRecv == -1)
        {
            cerr << "There  was a connection issue" << endl;
        }

        if (bytesRecv == 0)
        {
            cout << "The Client disconnected" << endl;
            break;
        }

        // Display message
        cout << "Received: " << string(buf, 0, bytesRecv) << endl;

        // Resend message
        send (clientSocket, buf, bytesRecv + 1, 0);
    }

    // Close socket
    close(clientSocket);

    return 0;
}