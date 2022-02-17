# Multiplayer-Client-Server

Simple implementation of a multiplayer client and server, created during the Multiplayer Explained workshop 
of the 3DUPB Summer School.

The server was tested using virtual machines in Frankfurt and USA. After running the clients, it was observed 
that the average delay of each client would rise or fall depending on the location of the server.

--- Client ---

If the client is not connected to the server, it will repeatedly send a connection request message until 
it will receive a confirmation from the server. The client then waits for a message from the server. If it
receives a PING message, it will reply with a PONG message.

If the client does not receive a message after 15 seconds, it will disconnect from the server.

--- Server ---

The server has a list of clients, and will PING them each second. If it has not received a response
from a client after 15 seconds, the client will be disconnected.

Every 10 seconds, the server computes the number of received PONG responses for each client and
the average delay.

The server also computes the message loss for each client, keeping track of the messages, and will resend 
them if it has not received a response.
