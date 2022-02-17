#ifndef UNICODE
#define UNICODE
#endif

#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <winsock2.h>
#include <Ws2tcpip.h>
#include <stdio.h>
#include <stdint.h>
#include <chrono>
#include <iostream>
#include <vector>

// Link with ws2_32.lib
#pragma comment(lib, "Ws2_32.lib")

#define TIMEOUT_TIME 15000

enum MessageType
{
    HELLO = 1,
    HELLO_CONFIRMED,
    PING,
    PONG
};

struct Message {
    MessageType type;
    uint32_t id;
    char text[1024];
};

int iResult;
WSADATA wsaData;

SOCKET ClientSocket = INVALID_SOCKET;
sockaddr_in RecvAddr;

unsigned short Port = 27015;

char message[1024];
char RecvBuf[1024];
char SendBuf[1024];
int BufLen = 1024;

uint64_t LastTimeReceived;
bool IsConnected;

struct sockaddr_in SenderAddr;
int SenderAddrSize = sizeof(SenderAddr);

std::vector<Message> MessageList;

int TimeNow() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

int Initialize() {
    // Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != NO_ERROR) {
        wprintf(L"WSAStartup failed with error: %d\n", iResult);
        return 0;
    }

    // Create a socket for sending data
    ClientSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (ClientSocket == INVALID_SOCKET) {
        wprintf(L"socket failed with error: %ld\n", WSAGetLastError());
        WSACleanup();
        return 0;
    }

    u_long iMode = 1;
    iResult = ioctlsocket(ClientSocket, FIONBIO, &iMode);
    if (iResult != NO_ERROR)
        printf("ioctlsocket failed with error: %ld\n", iResult);

    // Set up the RecvAddr structure with the IP address of
    // the receiver (in this example case "192.168.1.1")
    // and the specified port number.
    RecvAddr.sin_family = AF_INET;
    RecvAddr.sin_port = htons(Port);
    RecvAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    IsConnected = false;
    LastTimeReceived = TimeNow();

    return 1;
}

void SendData(char *message) {
    // Send a datagram to the server
    memset(SendBuf, 0, BufLen);
    memcpy(SendBuf, message, strlen(message));

    iResult = sendto(ClientSocket,
        SendBuf, BufLen, 0, (SOCKADDR*)&RecvAddr, sizeof(RecvAddr));
    if (iResult == SOCKET_ERROR) {
        wprintf(L"sendto failed with error: %d\n", WSAGetLastError());
        closesocket(ClientSocket);
        WSACleanup();
    }
}

void ComputeReceivedData() {
    // Extract the message from the buffer and inserts it 
    // in the MessageList to be handled later

    Message receivedMessage;
    receivedMessage.type = (MessageType) RecvBuf[0];
    receivedMessage.id = (int)RecvBuf[1];
    memcpy(receivedMessage.text, RecvBuf + 2, BufLen);

    MessageList.push_back(receivedMessage);
}

void ReceiveData() {
    // Call the recvfrom function to receive datagrams
    // on the bound socket.
    while (true) {
        iResult = recvfrom(ClientSocket,
            RecvBuf, BufLen, 0, (SOCKADDR*)&SenderAddr, &SenderAddrSize);

        if (iResult == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAEWOULDBLOCK) {
                break;
            }
            else {
                wprintf(L"recvfrom failed with error %d\n", WSAGetLastError());
                break;
            }
        }

        ComputeReceivedData();
    }

}

void CheckConnected() {
    // Check if the client is connected to server
    // If not, send a HELLO message to initiate 
    // the connection

    if (!IsConnected) {
        printf("Sending Hello to server.\n\n");

        memset(message, 0, BufLen);
        message[0] = (char) MessageType::HELLO;
        message[1] = 0;
        memcpy(message + 2, "Hello!", sizeof("Hello!"));

        SendData(message);
    }
}

void SendPong(int MessageID) {
    // Send a pong message with the given ID to server

    memset(message, 0, BufLen);
    message[0] = (char)MessageType::PONG;
    message[1] = MessageID;
    memcpy(message + 2, "Pong", sizeof("Pong"));

    SendData(message);
}

void HandleMessages() {
    // Parse every message from MessageList

    for (unsigned int i = 0; i < MessageList.size(); i++) {
        // Check the type of message
        switch (MessageList[i].type) {
            case MessageType::HELLO_CONFIRMED:
                // Received confirmation of connection to server
                printf("Received confirmation from server. Client is now connected.\n\n");
                IsConnected = true;
                break;
            case MessageType::PING:
                // Received a PING and sent a PONG to server
                printf("Received Ping with id %d. Sending Pong.\n\n", MessageList[i].id);
                LastTimeReceived = TimeNow();
                SendPong(MessageList[i].id);
                break;
            default:
                break;
        }
    }
    // Erase all messages from client's list
    MessageList.clear();
}

void CheckTimeout() {
    uint64_t timeNow = TimeNow();

    // If the client has not received a message in the appropriate time
    // the client will disconnect
    if (IsConnected && (timeNow - LastTimeReceived >= TIMEOUT_TIME)) {
        printf("Client has disconnected from server.\n\n");
        IsConnected = false;
    }
}

void Update() {
    
    CheckConnected();

    ReceiveData();

    HandleMessages();

    CheckTimeout();
    
}

void Cleanup() {
    // When the application is finished sending, close the socket.
    wprintf(L"Finished sending. Closing socket.\n");
    iResult = closesocket(ClientSocket);
    if (iResult == SOCKET_ERROR) {
        wprintf(L"closesocket failed with error: %d\n", WSAGetLastError());
        WSACleanup();
        return;
    }

    // Clean up and quit.
    wprintf(L"Exiting.\n");
    WSACleanup();
}

int main()
{
    iResult = Initialize();
    uint64_t time = TimeNow();
    while (iResult) {
        Update();
        Sleep(30);
    }

    Cleanup();

    return 0;
}