#include <iostream>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>
#include <string>
#include <sys/epoll.h>
#include <fcntl.h>
#include <cerrno>

using namespace std;

int main()
{
    // Create a socket
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
        close(listening);
        return -3;
    }

    // Create epoll instance
    int epollFD = epoll_create1(0);

    if (epollFD == -1)
    {
        cerr << "Failed to create epoll instance!" << endl;
        close(listening);
        return -5;
    }

    // Set listening socket to non-blocking
    int flags = fcntl(listening, F_GETFL, 0);

    if (flags == -1)
    {
        cerr << "Failed to get socket flags" << endl;
        close(epollFD);
        close(listening);
        return -6;
    }

    if (fcntl(listening, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        cerr << "Failed to set socket flags!" << endl;
        close(epollFD);
        close(listening);
        return -7;
    }

    // Register listening socket with epoll
    struct epoll_event event{};

    event.events = EPOLLIN;
    event.data.fd = listening;

    if (epoll_ctl(epollFD, EPOLL_CTL_ADD, listening, &event) == -1)
    {
        cerr << "Failed to register listening socket with epoll!" << endl;
        close(epollFD);
        close(listening);
        return -8;
    }

    // Create an array to store ready events
    constexpr int MAX_EVENTS = 10;
    epoll_event events[MAX_EVENTS];

    // Accept sequential connections
    while (true)
    {
        int eventCount = epoll_wait(
            epollFD,
            events,
            MAX_EVENTS,
            -1
            );

        if (eventCount == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }

            cerr << "epoll_wait failed!" << endl;
            break;
        }

        for (int i = 0; i < eventCount; i++)
        {
            int currentFD = events[i].data.fd;

            if (currentFD == listening)
            {
                if (!(events[i].events & EPOLLIN))
                {
                    cerr << "Unexpected listening socket event" << endl;
                    continue;
                }

                sockaddr_in client;
                socklen_t clientSize = sizeof(client);
                char host[NI_MAXHOST];
                char svc[NI_MAXSERV];

                int clientSocket = accept(
                    listening,
                    (sockaddr*)&client,
                    &clientSize
                    );

                if (clientSocket == -1)
                {
                    if (errno == EINTR ||
                        errno == EAGAIN ||
                        errno == EWOULDBLOCK)
                    {
                        continue;
                    }

                    cerr << "Problem with client connecting!";
                    return -4;
                }

                memset(host, 0, NI_MAXHOST);
                memset(svc, 0, NI_MAXSERV);

                int result = getnameinfo((sockaddr*)&client,
                    sizeof(client),
                    host,
                    NI_MAXHOST,
                    svc,
                    NI_MAXSERV,
                    0);

                if (result != 0)
                {
                    inet_ntop(AF_INET, &client.sin_addr, host, NI_MAXHOST);
                    cout << host << " connected on " << ntohs(client.sin_port) << endl;
                }
                else
                {
                    cout << host << " connected on " << svc << endl;
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
                        if (errno == EINTR)
                        {
                            continue;
                        }

                        cerr << "There  was a connection issue" << endl;
                        break;
                    }

                    if (bytesRecv == 0)
                    {
                        cout << "The Client disconnected" << endl;
                        break;
                    }

                    // Display message
                    cout << "Received: " << string(buf, 0, bytesRecv) << endl;

                    // Resend message
                    ssize_t totalSent = 0;

                    while (totalSent < bytesRecv)
                    {
                        ssize_t bytesSent = send(
                            clientSocket,
                            buf + totalSent,
                            bytesRecv - totalSent,
                            MSG_NOSIGNAL
                            );

                        if (bytesSent == -1)
                        {
                            if (errno == EINTR)
                            {
                                continue;
                            }

                            cerr << "There was a sending issue" << endl;
                            break;
                        }

                        if (bytesSent == 0)
                        {
                            cout << "No data was sent" << endl;
                            break;
                        }

                        totalSent += bytesSent;
                    }

                    if (totalSent < bytesRecv)
                    {
                        break;
                    }
                }

                // Close socket
                close(clientSocket);

            }
        }


    }

    // Close listening
    close(listening);

    // Close epoll instance
    close(epollFD);

    return 0;
}