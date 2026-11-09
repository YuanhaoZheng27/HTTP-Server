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
#include <unordered_map>

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

    // Store per-client connection state
    struct ClientState
    {
        string pendingSend;
        bool readEOF = false;
    };

    unordered_map<int, ClientState> clients;

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

                while (true)
                {
                    sockaddr_in client;
                    socklen_t clientSize = sizeof(client);

                    int clientSocket = accept(
                        listening,
                        (sockaddr*)&client,
                        &clientSize
                        );

                    if (clientSocket == -1)
                    {
                        if (errno == EINTR )
                        {
                            continue;
                        }

                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                        {
                            break;
                        }

                        cerr << "Accept failed: " << strerror(errno) << endl;
                        break;
                    }

                    int clientFlags = fcntl(clientSocket, F_GETFL, 0);

                    if (clientFlags == -1 ||
                        fcntl(clientSocket, F_SETFL, clientFlags | O_NONBLOCK) == -1)
                    {
                        cerr << "Failed to set client socket non-blocking" << endl;
                        close(clientSocket);
                        continue;
                    }

                    epoll_event clientEvent{};

                    clientEvent.events = EPOLLIN | EPOLLRDHUP;
                    clientEvent.data.fd = clientSocket;

                    if (epoll_ctl(
                            epollFD,
                            EPOLL_CTL_ADD,
                            clientSocket,
                            &clientEvent
                        ) == -1)
                    {
                        cerr << "Failed to register client socket" << endl;
                        close(clientSocket);
                        continue;
                    }

                    clients.emplace(clientSocket, ClientState{});
                    cout << "Client registered: FD " << clientSocket << endl;

                    char host[NI_MAXHOST];
                    char svc[NI_MAXSERV];
                    memset(host, 0, NI_MAXHOST);
                    memset(svc, 0, NI_MAXSERV);

                    int result = getnameinfo(
                        (sockaddr*)&client,
                        sizeof(client),
                        host,
                        NI_MAXHOST,
                        svc,
                        NI_MAXSERV,
                        0
                        );

                    if (result != 0)
                    {
                        inet_ntop(AF_INET, &client.sin_addr, host, NI_MAXHOST);
                        cout << host << " connected on " << ntohs(client.sin_port) << endl;
                    }
                    else
                    {
                        cout << host << " connected on " << svc << endl;
                    }
                }
            }
            else
            {
                auto it = clients.find(currentFD);

                if (it == clients.end())
                {
                    continue;
                }

                ClientState& state = it->second;
                bool shouldClose = false;

                // Handle client socket readable events
                if (!state.readEOF &&
                    (events[i].events & (EPOLLIN | EPOLLHUP | EPOLLRDHUP)))
                {
                    char buf[4096];

                    while (true)
                    {
                        ssize_t bytesRecv = recv(
                            currentFD,
                            buf,
                            sizeof(buf),
                            0
                        );

                        if (bytesRecv > 0)
                        {
                            cout << "Received: " << string(buf, bytesRecv) << endl;

                            // Save received data for echo
                            state.pendingSend.append(buf, bytesRecv);
                        }
                        else if (bytesRecv == 0)
                        {
                            cout << "Client disconnected: FD " << currentFD << endl;

                            state.readEOF = true;
                            break;
                        }
                        else
                        {
                            if (errno == EINTR)
                            {
                                continue;
                            }

                            if (errno == EAGAIN || errno == EWOULDBLOCK)
                            {
                                break;
                            }

                            cerr << "recv failed: " << strerror(errno) << endl;

                            shouldClose = true;
                            break;
                        }
                    }
                }

                // Handle socket errors
                if (events[i].events & EPOLLERR)
                {
                    cerr << "Socket error: FD " << currentFD << endl;
                    shouldClose = true;
                }

                // Send echo data
                if (!shouldClose)
                {
                    while (!state.pendingSend.empty())
                    {
                        ssize_t bytesSent = send(
                            currentFD,
                            state.pendingSend.data(),
                            state.pendingSend.size(),
                            MSG_NOSIGNAL
                        );

                        if (bytesSent > 0)
                        {
                            state.pendingSend.erase(0, bytesSent);
                        }
                        else if (bytesSent == 0)
                        {
                            cerr << "No data was sent" << endl;
                            shouldClose = true;
                            break;
                        }
                        else
                        {
                            if (errno == EINTR)
                            {
                                continue;
                            }

                            if (errno == EAGAIN || errno == EWOULDBLOCK)
                            {
                                break;
                            }

                            cerr << "There was a sending issue" << endl;
                            shouldClose = true;
                            break;
                        }
                    }
                }

                // Close after sending all remaining data
                if (state.readEOF && state.pendingSend.empty())
                {
                    shouldClose = true;
                }

                // Update epoll events
                if (!shouldClose)
                {
                    epoll_event clientEvent{};
                    clientEvent.data.fd = currentFD;

                    if (!state.readEOF)
                    {
                        clientEvent.events = EPOLLIN | EPOLLRDHUP;
                    }

                    if (!state.pendingSend.empty())
                    {
                        clientEvent.events |= EPOLLOUT;
                    }

                    if (epoll_ctl(
                            epollFD,
                            EPOLL_CTL_MOD,
                            currentFD,
                            &clientEvent
                        ) == -1)
                    {
                        cerr << "Failed to update client events" << endl;
                        shouldClose = true;
                    }
                }

                // Close socket
                if (shouldClose)
                {
                    epoll_ctl(epollFD, EPOLL_CTL_DEL, currentFD, nullptr);
                    close(currentFD);
                    clients.erase(currentFD);
                }
            }
        }
    }

    // Close listening
    close(listening);

    // Close epoll instance
    close(epollFD);

    return 0;
}