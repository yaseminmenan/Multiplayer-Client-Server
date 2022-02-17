#ifndef UNICODE
#define UNICODE
#endif

#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define PING_TIME 1000  // 1s
#define TIMEOUT_TIME 15000 // 15s
#define PONG_TIME 10000 // 10s
#define LOSS_TIME_CALCULATE 60000 // 60s

#include <winsock2.h>
#include <Ws2tcpip.h>
#include <stdio.h>
#include <iostream>
#include <vector>
#include <chrono>
#include <unordered_map>
#include "server.h"

// Link with ws2_32.lib
#pragma comment(lib, "Ws2_32.lib")

enum class MessageType
{
    HELLO = 1,
    HELLO_CONFIRMED,
    PING,
    PONG
};

int iResult = 0;

WSADATA wsaData;

SOCKET ServerSocket;
struct sockaddr_in RecvAddr;

unsigned short Port = 27015;

char message[1024];
char RecvBuf[1024];
char SendBuf[1024];
int BufLen = 1024;

struct sockaddr_in SenderAddr;
int SenderAddrSize = sizeof(SenderAddr);

std::vector<struct client> ClientList;
std::vector<struct Message> MessageList;

uint64_t TimeFinish;
uint64_t TimeStart;
uint64_t LossStart;
uint64_t LossFinish;

int TotalSentMessages;
int TotalResponseMessages;

std::unordered_map<int, uint64_t>::iterator itr;

struct Message {
    MessageType type;
    int id;
    char text[1024];
};

struct client {
    struct sockaddr_in Addr;
    uint64_t totalDelay;
    uint64_t LastTimeReceived;
    uint64_t LastTimeSent;
    uint64_t TotalSent;
    uint64_t TotalReceived;
    int totalPongs;
    float avgDelay;
    std::vector<Message> MessageList;
    std::unordered_map<int, uint64_t> SentPings;
};


uint64_t TimeNow() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

int Initialize() {
    // Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != NO_ERROR) {
        wprintf(L"WSAStartup failed with error %d\n", iResult);
        return 0;
    }

    // Create a receiver socket to receive datagrams
    ServerSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (ServerSocket == INVALID_SOCKET) {
        wprintf(L"socket failed with error %d\n", WSAGetLastError());
        return 0;
    }

    u_long iMode = 1;
    iResult = ioctlsocket(ServerSocket, FIONBIO, &iMode);
    if (iResult != NO_ERROR)
        printf("ioctlsocket failed with error: %ld\n", iResult);

    // Bind the socket to any address and the specified port.
    RecvAddr.sin_family = AF_INET;
    RecvAddr.sin_port = htons(Port);
    RecvAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    iResult = bind(ServerSocket, (SOCKADDR*)&RecvAddr, sizeof(RecvAddr));
    if (iResult != 0) {
        wprintf(L"bind failed with error %d\n", WSAGetLastError());
        return 0;
    }

    TimeStart = TimeNow();
    LossStart = TimeNow();

    return 1;
}

int GetClientID(struct sockaddr_in ClientAddr) {
    // Look for client in the list with the same address and port
    // and return the index
    for (unsigned int i = 0; i < ClientList.size(); i++) {
        if ((ClientAddr.sin_addr.s_addr == ClientList[i].Addr.sin_addr.s_addr) &&
            (ClientAddr.sin_port == ClientList[i].Addr.sin_port)) {
            return i;
        }
    }

    // Return -1 if the list doesn't contain the client
    return -1;
}

void AddClient(struct sockaddr_in ClientAddr) {
    // Add a new client to ClientList
    struct client NewClient;
    NewClient.Addr = ClientAddr;
    NewClient.LastTimeReceived = TimeNow();
    NewClient.LastTimeSent = TimeNow();
    NewClient.TotalSent = 0;
    NewClient.TotalReceived = 0;
    NewClient.totalDelay = 0;
    NewClient.totalPongs = 0;
    NewClient.avgDelay = 0;

    ClientList.push_back(NewClient);

    printf("Added new client %s:%d\n\n", inet_ntoa(NewClient.Addr.sin_addr),
        NewClient.Addr.sin_port);
}

void SendData(char* message, int ClientID) {
    // Send a datagram to the receiver with the given ID
    if (ClientList[ClientID].TotalSent == 127) {
        ClientList[ClientID].TotalSent = 0;
    }
    else {
        ClientList[ClientID].TotalSent++;
    }

    memset(SendBuf, 0, BufLen);
    memcpy(SendBuf, message, strlen(message));

    iResult = sendto(ServerSocket,
        SendBuf, BufLen, 0, (SOCKADDR*)&ClientList[ClientID].Addr, sizeof(ClientList[ClientID].Addr));
    if (iResult == SOCKET_ERROR) {
        wprintf(L"sendto failed with error: %d\n", WSAGetLastError());
        return;
    }
}

void SendPing(int ClientID, int MessageID) {
    // Send a PING message with the given ID to the client
    memset(message, 0, BufLen);
    message[0] = (char)MessageType::PING;
    message[1] = MessageID;
    memcpy(message + 2, "Ping", sizeof("Ping"));

    SendData(message, ClientID);
}

void SendConfirm(int ClientID) {
    // Create a HELLO_CONFIRMED message and send it to the client with ClientID
    memset(message, 0, BufLen);
    message[0] = (char)MessageType::HELLO_CONFIRMED;
    message[1] = 0;
    memcpy(message + 2, "Client has been added.", sizeof("Client has been added."));

    SendData(message, ClientID);
}

void ComputeReceivedData() {
    // Extract the message from the buffer and inserts it 
    // in the client's MessageList to be handled later

    // Get the client id
    int id = GetClientID(SenderAddr);

    // Create the message from buffer
    Message receivedMessage;
    receivedMessage.type = (MessageType)RecvBuf[0];
    receivedMessage.id = (int)RecvBuf[1];
    memcpy(receivedMessage.text, RecvBuf + 2, BufLen);
   
    // If the client doesn't exist in the list, add it
    if (id == -1) {
        id = ClientList.size();
        AddClient(SenderAddr);
    }

    // Add the message to client's MessageList
    ClientList[id].MessageList.push_back(receivedMessage);
}

void ReceiveData() {
    // Call the recvfrom function to receive datagrams
    // on the bound socket.
    while (true) {
        iResult = recvfrom(ServerSocket,
            RecvBuf, BufLen, 0, (SOCKADDR*)&SenderAddr, &SenderAddrSize);

        if (iResult == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAEWOULDBLOCK)
                break;
            else {
                wprintf(L"recvfrom failed with error %d\n", WSAGetLastError());
                break;
            }
        }

        ComputeReceivedData();
    }
}

void ClientMessageReceived(int ClientID, int MessageID) {
    int id;

    // Parse the SentPings map of the client and check for a key equal to MessageID
    for (itr = ClientList[ClientID].SentPings.begin(); itr != ClientList[ClientID].SentPings.end(); itr++) {
        id = itr->first;
       
        if (id == MessageID) {
            // Increment the number of received messages
            ClientList[ClientID].TotalReceived++;

            // Increment the number of received pongs
            ClientList[ClientID].totalPongs++;

            // Update the time when the client has received a message
            ClientList[ClientID].LastTimeReceived = TimeNow();

            // Update the total delay
            ClientList[ClientID].totalDelay += ClientList[ClientID].LastTimeReceived - ClientList[ClientID].LastTimeSent;

            // Erase the message from map because it has received a response
            ClientList[ClientID].SentPings.erase(MessageID);
            break;
        }
    }
}

void HandleMessages() {
    // Parse every message from the MessageList of each connected client
    for (unsigned int i = 0; i < ClientList.size(); i++) {
        for (unsigned int j = 0; j < ClientList[i].MessageList.size(); j++) {
            // Check the type of the message
            switch (ClientList[i].MessageList[j].type) {
                case MessageType::HELLO:
                    // If the message was HELLO, send HELLO_CONFIRMED to client
                    ClientList[i].TotalReceived++;
                    SendConfirm(i);
                    break;
                case MessageType::PONG:
                    // If the message was PONG, remove the ping with the 
                    // corresponding id from map and update client data
                    ClientMessageReceived(i, ClientList[i].MessageList[j].id);
                    break;
                default:
                    break;
            }
        }
        // Erase all messages from client's list
        ClientList[i].MessageList.clear();
    }
}

void CalculateAverageDelay() {
    // Calculate average delay for each client
    for (unsigned int i = 0; i < ClientList.size(); i++) {
        if (ClientList[i].totalPongs != 0) {

            ClientList[i].avgDelay = ((float)ClientList[i].totalDelay) / ClientList[i].totalPongs;

            printf("Client %s:%d sent %d Pong messages with average delay of %fms\n\n",
                inet_ntoa(ClientList[i].Addr.sin_addr), ClientList[i].Addr.sin_port,
                ClientList[i].totalPongs, ClientList[i].avgDelay);

            // Reset values for next calculation
            ClientList[i].totalDelay = 0;
            ClientList[i].totalPongs = 0;
        }
    }
}

void CalculatePongs() {
    // Calculate the number of pongs and average delay 
    // if the required time has passed

    TimeFinish = TimeNow();

    if (TimeFinish - TimeStart >= PONG_TIME) {

        CalculateAverageDelay();

        TimeStart = TimeNow();
    }
}

void CheckTimeouts() {
    // Check every client and disconnect them
    // if the timeout time has passed

    std::vector<int> IndexList;
    uint64_t timeNow;

    for (unsigned int i = 0; i < ClientList.size(); i++) {
        timeNow = TimeNow();

        if (timeNow - ClientList[i].LastTimeReceived >= TIMEOUT_TIME) {
            printf("Client %s:%d has been disconnected.\n\n", 
                inet_ntoa(ClientList[i].Addr.sin_addr), ClientList[i].Addr.sin_port);

            // Add client's index to list
            IndexList.push_back(i);
        }
    }

    // Remove all clients from ClientList with the indexes in IndexList
    for (unsigned int i = 0; i < IndexList.size(); i++) {
        ClientList.erase(ClientList.begin() + IndexList[i]);
    }
}

void PingClients() {
    // Ping all connected clients once per second

    uint64_t timeNow;
    int id;

    for (unsigned int i = 0; i < ClientList.size(); i++) {
        timeNow = TimeNow();

        // If a second has passed since the server sent a message to the client
        if (timeNow - ClientList[i].LastTimeSent >= PING_TIME) {

            if (ClientList[i].TotalSent == 127) {
                id = 0;
            }
            else {
                id = ClientList[i].TotalSent + 1;
            }

            // Send ping message with message id
            SendPing(i, id);
            
            // Add ping to SentPings map with the time when it was sent
            ClientList[i].SentPings[id] = timeNow;
            
            // Update the time when server last sent a message
            ClientList[i].LastTimeSent = timeNow;
        }
    }
}

int ClientTotalNoResponse(int ClientID) {
    // Calculate the number of messages that the server 
    // has not received a response to in the appropriate time

    int totalNoResponse = 0, MessageID, timeSent;
    uint64_t timeNow = TimeNow();

    // Iterate through every element in the SentPings map
    for (itr = ClientList[ClientID].SentPings.begin(); itr != ClientList[ClientID].SentPings.end(); itr++) {
        MessageID = itr->first;
        timeSent = itr->second;

        if (ClientList[ClientID].avgDelay > 0 && (timeNow - timeSent > 2 * ClientList[ClientID].avgDelay)) {
            printf("loss packet id %d\n\n", MessageID);
            totalNoResponse++;
        }
    }

    return totalNoResponse;
}

void CalculateLoss() {
    // Calculate message loss for each connected client

    int totalSent;
    float loss;
    uint64_t timeNow;

    LossFinish = TimeNow();

    if (LossFinish - LossStart >= LOSS_TIME_CALCULATE) {

        for (unsigned int i = 0; i < ClientList.size(); i++) {
            loss = 0;

            // Calculate the total number of messages sent by the server
            totalSent = ClientList[i].TotalReceived + ClientTotalNoResponse(i);

            // Calculate message loss
            if (totalSent != 0) {
                loss = (1.f - ((float)ClientList[i].TotalReceived / totalSent)) * 100.f;
            }

            printf("Client %s:%d has a message loss rate of %f%%.\n\n", 
                inet_ntoa(ClientList[i].Addr.sin_addr), ClientList[i].Addr.sin_port, loss);

            // Reset the value of received messages from server
            ClientList[i].TotalReceived = 0;
        }
        
        LossStart = TimeNow();
    }
}

void ResendMessages() {
    // Resend messages for each client that has
    // not sent a response in the appropriate time

    uint64_t timeNow, timeSent;
    int MessageID;

    for (unsigned int i = 0; i < ClientList.size(); i++) {
        timeNow = TimeNow();
        
        for (itr = ClientList[i].SentPings.begin(); itr != ClientList[i].SentPings.end(); itr++) {
            MessageID = itr->first;
            timeSent = itr->second;

            if (ClientList[i].avgDelay > 0 && (timeNow - timeSent > 2 * ClientList[i].avgDelay)){
                // Resend ping message
                SendPing(i, MessageID);

                // Update the time when the message was last sent
                ClientList[i].SentPings[MessageID] = timeNow;
            }
        }
    }
}

void Update() {

    ReceiveData();

    HandleMessages();

    PingClients();

    CalculatePongs();

    CalculateLoss();

    ResendMessages();

    CheckTimeouts();
}

void Cleanup() {
    // Close the socket when finished receiving datagrams
    wprintf(L"Finished receiving. Closing socket.\n");
    iResult = closesocket(ServerSocket);
    if (iResult == SOCKET_ERROR) {
        wprintf(L"closesocket failed with error %d\n", WSAGetLastError());
        return;
    }

    // Clean up and exit.
    wprintf(L"Exiting.\n");
    WSACleanup();
}

int main()
{
    iResult = Initialize();

    while (iResult) {
        Update();
        Sleep(30);
    }

    Cleanup();

    return 0;
}