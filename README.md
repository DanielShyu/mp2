# ECEN 602 MP2 — TCP Simple Broadcast Chat (SBCP)

A chat server and client implementing the Simple Broadcast Chat Protocol (SBCP).

## Team Roles

| Member | Role |
|---|---|
| Chiahsiang Shyu | Client implementation, client test cases |
| Lian Chu | Server implementation, server test cases |

## Build

```bash
make          # builds ./server and ./client
make clean    # removes binaries and object files
```

Requires `g++` on a standard Linux system.

## Usage

### Server

```bash
./server <server_ip> <server_port> <max_clients>
```

| Argument | Description |
|---|---|
| `server_ip` | Kept for the required command-line format; the server listens on **all** local IPv4 and IPv6 addresses regardless of this value |
| `server_port` | Port to listen on (1024–65535 recommended) |
| `max_clients` | Maximum number of clients in the chat room, should be an integer between 1-30|

### Client

```bash
./client <username> <server_ip> <server_port>
```

| Argument | Description |
|---|---|
| `username` | Chat name, 1–16 characters, must be unique in the room, case sensitive |
| `server_ip` | Server address: IPv4, IPv6, or hostname (e.g. `127.0.0.1`, `::1`, `localhost`) |
| `server_port` | Server port |

Type a line and press Enter to send a message. Press `Ctrl+D` to leave the chat.

### Example

```bash
./server 127.0.0.1 9000 3
./client alice 127.0.0.1 9000
./client bob   127.0.0.1 9000
```

## Architecture

**Message format.** Every SBCP message is a 4-byte header (`Version:9 | Type:7 | Length:16`) followed by zero or more TLV attributes (`Type:16 | Length:16 | Payload`). All multi-byte fields are sent in network byte order. Version and Type are packed into one `uint16_t` (Version(9b) | Type(7b)).

**Server.** Uses `getaddrinfo(NULL, port)` with `AI_PASSIVE` to create two listening sockets, one IPv4 and one IPv6, both on the given port. It watches both listening sockets and every connected client socket with `select()`. New connections are accepted; `JOIN` registers a username (duplicates are rejected); `SEND` is converted to `FWD` and broadcast to all other clients; a `read()` returning 0 means the client left, and its resources and username are released.

**Client.** Watches `stdin` and the server socket with `select()`. Sends each line from stdin to the server, and decodes and displays messages from the server. 


## Bonus Functions
- OFFLINE/ONLINE: Whenever a user joins or disconnects, the server also send an `ONLINE`/`OFFLINE` message to all the users 
- ACK: When a user joins, the server sends an `ACK` message telling current server user count(including the new user), as well as a list of id of all online user(excluding the new user).
- NAK: When a request for join is declined by the server, the server sends a `NAK` message to the declined user, including the reason why the user is declined.
- IDLE: A user is considered active for both joining and sending a message. If the last active time of an user exceeds 10 sec, an `IDLE` message is sent from the idle user to the server, and the server would then forward this information to all the other users (idle or not) in server
- IPV6/IPV4: As mentioned before, this work supports both ipv6 and ipv4.
## Errata 

- **One message per `read()`.** If the header Length does not match the number of bytes read, the message is discarded (as required by the spec). If TCP delivers two server messages in a single read, both are discarded.
- **Long input.** Input longer than 512 characters is split by `fgets()`; the remainder is treated as a separate message.
- **Pasting multiple lines.** Because `fgets()` buffers `stdin`, pasting several lines at once may delay the later lines until the next keypress. Normal typing is not affected.
- **IDLE timing.** The idle timer has 1-second resolution, so IDLE is sent 9–10 seconds after the last message. IDLE is sent once per idle period, and is only reset by sending a chat message (receiving messages does not reset it).
- **IDLE is not replayed.** A client that joins after another client became idle will not be told about it.