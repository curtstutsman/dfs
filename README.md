# dfs

A distributed file system built in C++ over gRPC. Files in a local mount directory sync to a server automatically — inotify watches for local changes and pushes them, and an async callback loop pulls server-side updates back to connected clients.

## Dependencies

- C++23
- gRPC + Protocol Buffers

```bash
sudo apt install -y protobuf-compiler libprotobuf-dev protobuf-compiler-grpc libgrpc++-dev
```

## Build

```bash
make dirs      # create bin/, tmp/, mnt/client, mnt/server
make protos    # generate proto-src/ (once, or after editing proto/dfs-service.proto)
make           # build bin/dfs-client and bin/dfs-server
```

Disable AddressSanitizer if needed: `make ASAN=`

## Usage

```bash
# Server
./bin/dfs-server --address 0.0.0.0:53552 --mount_path mnt/server/

# Client — mount mode (inotify + async sync)
./bin/dfs-client --address 0.0.0.0:53552 --mount_path mnt/client/ mount

# One-shot commands
./bin/dfs-client store <file>
./bin/dfs-client fetch <file>
./bin/dfs-client list
./bin/dfs-client stat <file>
./bin/dfs-client delete <file>
```

`--debug_level 1-3` for verbose output.

## How it works

Files are streamed in 4096-byte chunks. A CRC32 checksum is sent with every request so the server can skip files that haven't changed. Store and Delete require a write lease from the server's lock manager. One writer per file at a time.

`mount` starts two threads: an inotify watcher that calls Store or Delete on local filesystem events, and a gRPC completion queue loop that processes async server callbacks whenever the server's file list changes. After each callback the client fetches any file newer than its local copy.

## Layout

```
proto/           gRPC service definition
proto-src/       generated code (do not edit)
src/
  common/        logging, CRC32, inotify types, path util, constants
  client/        ClientBase, ClientNode, Client (inotify driver)
  server/        ServiceImpl, ServerNode, LockManager
    async/       async call state machine and service runner
  vendor/        CRCPP
client.cpp       client entry point
server.cpp       server entry point
```
