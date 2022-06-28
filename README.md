# Task 1 - Single-node KV store

In the previous, task you implemented a client/server application over TCP/IP
sockets. In this task you will have to implement a single-node Key-Value (KV) store using
(and possibly extending) your client/server application.

### Client
The client for this task executes the workload (`PUT`/`GET` requests). 
The client process initializes one (or more) connection(s) to the server and then it executes the workload by replaying a trace file.
At the end of the execution the client verifies that the state of the server is as expected. 
The code and the trace files for the client and the workloads respectively are provided in the source directory.
You should **not** modify them. 

The client is a single process that can spawn multiple threads to simulate multiple clients.
You can run the client process as follows:
```
./build/dev/clt -c <no_clients> -s <server_address> -m <no_messages> -p <port> -t <trace_file>
``` 
The `no_clients` parameter depicts the number of client threads.
The `no_messages` parameter shows the number of requests that **each** client should execute.


### Task 1.1 - Server
Your task is to implement the server process. 
The server should be able to handle multiple clients' requests in parallel and efficiently (**hint**: you may not assume that the server-node has unlimited cores!).
The server listens for incoming connections and executes the requests by applying them to its local KV store. 
For every executed request, the server needs to reply back to the client. 
You should use `google::protobufs` for serializing the messages/requests over the network. 
We have provided you with the `.proto` files that describe the request and the response message.
Specifically, the client sends requests of the form: `source/client_message.proto` and expects replies of the form `source/server_message.proto`.

Additionally, the server needs to manage an instance of RocksDB, a well-established database. 
All incoming requests **must** be executed on RocksDB.
The code for rocksdb library is given in the `rocksdb` directory.

The server process should take the following arguments:
1. `-n <no_threads>`: the number of server's process threads
2. `-s <server_address>`: the address of the server's node
3. `-p <server_port>`: the listening port
4. `-c <no_clients>`: the number of clients to be connected

For example, you should be able to run the server as:
```
./build/dev/svr -n <no_threads> -s <server_address> -p <server_port> -c <no_clients>
```

For the CI, the addresses should **always** be set to `localhost` (the server and the client processes run on the same machine).

### Task 1.2 - Dockerfile

Lastly, to complete the exercise you need to complete the given `Dockerfile`.
Note that, the given code is copied into the directory `/app/` where it should be built.

## Build the code

You can build the code using the following instructions which will place the
binaries into the `build/dev` directory. To pass the tests you should not move
the binaries to another directory. 
Similarly, inside the container, the executables are expected to be found in the `/app/build/dev` directory.

```
make -C rocksdb static_lib -j$(nproc)
cmake -S . -B build/dev -D CMAKE_BUILD_TYPE=Release
cmake --build build/dev -j$(nproc)
```

We provide you with an empty file `build.sh` which is expected by the CI to perform the build of the task.
Therefore, you are also requested to complete this script.

## Example

Run the server:
```
./build/dev/svr -n 1 -s localhost -p 31001 -c 1 -o 1
```
Run the client:
```
./build/dev/clt -c 1 -s localhost -m 11900 -p 31001 -t <path>/cloud-lab/source/workload_traces/12K_traces.txt
```


## Tests

### Test 1.0 - Correctness with 1 client
This test checks the correctness of your implementation for single-threaded server and client processes.
The client executes the workload and checks whether the server is at the expected state at the end of the experiment.

### Test 1.1 - Correctness with multiple clients
This test checks the correctness of your implementation for multi-threaded server and client processes 
in the same fashion that it is performed in Task 1.0.

### Test 1.2 - Performance 
The last test is a performance test which calculates the execution time for different server configurations. 
To pass the test, your code should be scalable with an increasing number of threads.

### Important note:
In every test, the server application is executed inside a docker container (similarly to Task 0).

## References
- [Protobufs](https://developers.google.com/protocol-buffers/docs/cpptutorial)
- [RocksDB](http://rocksdb.org/docs/getting-started.html)
- [Dockerfile](https://docs.docker.com/engine/reference/builder/)
