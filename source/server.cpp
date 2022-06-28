#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <linux/sockios.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <iostream>
#include <sys/select.h>

#include <thread>
#include <mutex>
#include <tuple>

#include "kv_store.h"
#include "message.h"
#include "shared.h"
#include "workload_traces/generate_traces.h"

#include <cxxopts.hpp>
#include <fmt/printf.h>

#include "rocksdb/db.h"
#include "rocksdb/slice.h"
#include "rocksdb/options.h"

std::mutex m;

int no_threads, server_port, no_clients;
std::string server_address;
std::vector<std::tuple<int, int>> socket_fds;
bool startProcessing = false;

bool recv_message(int sockfd, sockets::client_msg *message);
void send_message(int sockfd, server::server_response::reply message);

class ServerThread
{
	int thread_id = 0;
	struct sockaddr_in srv_addr;

public:
	ServerThread() = delete;
	explicit ServerThread(int tid) : thread_id(tid) {}
};

std::atomic<int> threads_ids{0};

class ServerOP
{
	std::shared_ptr<KvStore> local_kv;

public:
	ServerOP()
	{
		local_kv = KvStore::init();
	}

	void local_kv_init_it() { local_kv->init_it(); }

	void local_kv_put(int key, std::string_view value)
	{
		local_kv->put(key, value);
	}

	std::string local_kv_get(int key)
	{
		std::optional<std::string_view> strvw = local_kv->get(key);
		if (strvw == std::nullopt)
		{
			return std::string("------Null------");
		}
		return std::string(strvw->data());
	}
};

std::string get_db(rocksdb::DB &rock_db, int key)
{
	std::string value;
	rocksdb::Status rock_s = rock_db.Get(rocksdb::ReadOptions(), std::to_string(key), &value);
	if (!rock_s.ok())
	{
		// printf("### %s: %d  ###\n", rock_s.ToString().c_str(), key);
	}
	return value;
}

std::string put_db(rocksdb::DB &rock_db, int key, std::string value)
{
	rocksdb::Status rock_s = rock_db.Put(rocksdb::WriteOptions(), std::to_string(key), value);
	if (!rock_s.ok())
	{
		printf("\nPUT Errrrrrrrrrrrrrrrrrrror:\n");
		std::cerr << rock_s.ToString() << std::endl;
	}
	return value;
}

std::string get_db(rocksdb::DB &rock_db, sockets::client_msg message)
{
	std::string value;
	rocksdb::Status rock_s = rock_db.Get(rocksdb::ReadOptions(), std::to_string(message.ops(0).key()), &value);
	if (!rock_s.ok())
	{
		// printf("### %s: %d  ###\n", rock_s.ToString().c_str(), key);
	}
	return value;
}

void put_db(rocksdb::DB &rock_db, sockets::client_msg message)
{
	rocksdb::Status rock_s = rock_db.Put(rocksdb::WriteOptions(), std::to_string(message.ops(0).key()), message.ops(0).value());
	if (!rock_s.ok())
	{
		printf("\nPUT Errrrrrrrrrrrrrrrrrrror:\n");
		std::cerr << rock_s.ToString() << std::endl;
	}
}

void close_sockets(int recv_sockfd, int send_sockfd)
{
	printf("Closing %d and %d\n", recv_sockfd, send_sockfd);
	close(recv_sockfd);
	close(send_sockfd);
	sleep(2);
}

void server_worker(ServerOP *server_op, rocksdb::DB &rock_db, int no_clients)
{
	while (socket_fds.size() < no_clients) {
		if(startProcessing)
			break;
		usleep(((std::rand()) % 6 + 5) * 1000 * 100);
		}

	startProcessing = true;
	while (!socket_fds.empty())
	{
		printf("Backlog size: %ld\n", socket_fds.size());
		auto id = threads_ids.fetch_add(1);
		ServerThread s_thread(id);

		m.lock();
		auto [work_recv_sock, work_send_sock] = socket_fds.back();
		socket_fds.pop_back();
		m.unlock();

		sockets::client_msg message;
		server::server_response::reply server_response;
		bool keep_running = true;
		while (keep_running)
		{
			auto op_id = 0;
			char tmp[1] = "";
			usleep(5);
			if (recv(work_recv_sock, tmp, 1, MSG_PEEK) <= 0)
			{
				// printf("Break!\n");
				break;
			}
			keep_running = recv_message(work_recv_sock, &message);
			// printf("%s", message.DebugString().c_str());

			std::string value = "------Null------";
			int key = message.ops(0).key();
			switch (message.ops(0).type())
			{
			case sockets::client_msg::OperationType::client_msg_OperationType_GET:
				// value = get_db(rock_db, message);
				value = server_op->local_kv_get(key);
				server_response.set_value(value); // s_thread.local_kv_get(message.ops(0).key())->data()
				server_response.set_op_id(1);
				break;
			case sockets::client_msg::OperationType::client_msg_OperationType_PUT:
				value = message.ops(0).value();
				server_op->local_kv_put(key, value);
				// put_db(rock_db, key, value);
				put_db(rock_db, message);
				server_response.set_value(value);
				server_response.set_op_id(0);
				break;
			}
			server_response.set_success(keep_running);

			send_message(work_send_sock, server_response);
		}
		close_sockets(work_recv_sock, work_send_sock);
	}
}

void init_connections()
{
	int recv_sockfd;
	// init recv_sockfd -------------------------------------
	// Create Socket
	if ((recv_sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		fmt::print("recv socket\n");
		exit(1);
	}

	// Make address reusable, if used shortly before
	int opt = 1;
	if ((setsockopt(recv_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int))) == -1)
	{
		printf("recv setsockopt\n");
		std::cout << "Errno: " << errno << std::endl;
		exit(1);
	}

	if ((setsockopt(recv_sockfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(int))) == -1)
	{
		printf("recv setsockopt\n");
		std::cout << "Errno: " << errno << std::endl;
		exit(1);
	}

	struct timeval timeout;
	timeout.tv_sec = 15;
	timeout.tv_usec = 0;

	setsockopt(recv_sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);

	// Setting server byte order; declare (local)host IP address;
	// set and convert port number (into network byte order)
	struct sockaddr_in srv_addr;
	srv_addr.sin_family = AF_INET;
	srv_addr.sin_port = htons(server_port);
	srv_addr.sin_addr.s_addr = inet_addr(server_address.c_str()); // INADDR_ANY;
	memset(&(srv_addr.sin_zero), 0, sizeof(srv_addr.sin_zero));

	// passing file descriptor, address structure and the lenght
	// of the address structure to bind current IP on the port
	if ((bind(recv_sockfd, (struct sockaddr *)&srv_addr, sizeof(srv_addr))) == -1)
	{
		fmt::print("recv bind\n");
		std::cout << "Errno: " << errno << std::endl;
		exit(1);
	}

	// lets the socket listen for upto 5 connections
	if ((listen(recv_sockfd, 1)) == -1)
	{
		fmt::printf("recv listen\n");
		exit(1);
	}
	// fmt::print("waiting for new connections ..\n");

	int addrlen = sizeof(srv_addr);
	int new_sockfd;

	if ((new_sockfd =
			 accept(recv_sockfd,
					(struct sockaddr *)&srv_addr,
					(socklen_t *)&addrlen)) < 0)
	{
		// fmt::print("recv accect\n");
		return;
	}

	fmt::print("accept succeeded on recv_sockfd {} {} ..\n", new_sockfd, recv_sockfd);

	recv_sockfd = new_sockfd;

	setsockopt(recv_sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(struct timeval));

	sockets::client_msg message;
	recv_message(recv_sockfd, &message);

	int send_port = message.ops(0).port();
	// printf("Port: %d\n", anws_port);

	// init send_sockfd -------------------------------------
	int send_sockfd;
	if ((send_sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		fmt::print("send socket\n");
		exit(1);
	}

	hostent *he = hostip;
	sockaddr_in their_addr{};
	their_addr.sin_family = AF_INET;
	their_addr.sin_port = htons(send_port);
	their_addr.sin_addr.s_addr = htonl(INADDR_ANY); // = *(reinterpret_cast<in_addr *>(he->h_addr)); // .s_addr = inet_addr(server_address.c_str());
	memset(&(their_addr.sin_zero), 0, sizeof(their_addr.sin_zero));

	sleep(1);
	if ((connect(send_sockfd, reinterpret_cast<sockaddr *>(&their_addr),
				 sizeof(struct sockaddr))) < 0)
	{
		std::cout << "send connect Errno: " << errno << std::endl;
		exit(1);
	}

	fmt::print("connect succeeded on send_sockfd {} ..\n", send_sockfd);
	m.lock();
	socket_fds.push_back(std::tuple<int, int>(recv_sockfd, send_sockfd));
	m.unlock();
}

auto main(int argc, char *argv[]) -> int
{

	cxxopts::Options options(argv[0], "Server for the sockets benchmark");
	options.allow_unrecognised_options().add_options()("n,no_threads", "Number of server's process threads", cxxopts::value<size_t>())("s,server_address", "The address of the server's node", cxxopts::value<std::string>())("p,server_port", "The listenting port", cxxopts::value<size_t>())("c,no_clients", "The number of clients to be connected", cxxopts::value<size_t>())("h,help", "Print help");

	auto args = options.parse(argc, argv);

	if (args.count("help"))
	{
		fmt::print("{}\n", options.help());
		return 0;
	}

	if (!args.count("no_threads"))
	{
		fmt::print(stderr, "The number of threads is required\n{}\n", options.help());
		return 0;
	}

	if (!args.count("server_address"))
	{
		fmt::print(stderr, "The server address is required\n{}\n", options.help());
		return 0;
	}

	if (!args.count("server_port"))
	{
		fmt::print(stderr, "The port is required\n{}\n", options.help());
		return 0;
	}

	if (!args.count("no_clients"))
	{
		fmt::print(stderr, "The number of clients is required\n{}\n", options.help());
		return 0;
	}

	no_threads = args["no_threads"].as<size_t>();
	server_port = args["server_port"].as<size_t>();
	no_clients = args["no_clients"].as<size_t>();
	server_address = args["server_address"].as<std::string>();

	if ((strcmp(server_address.c_str(), "localhost") == 0))
	{
		server_address = "127.0.0.1";
	}

	auto id = threads_ids.fetch_add(1);
	ServerThread m_thread(id);

	rocksdb::DB *rock_db;
	rocksdb::Options rock_options;
	rocksdb::Status rock_s;
	std::string kDBPath = "/rocksdb_temp";

	rock_options.IncreaseParallelism(no_threads);
	rock_options.OptimizeLevelStyleCompaction();
	rock_options.compression_per_level.resize(rock_options.num_levels);
	rock_options.create_if_missing = true;

	for (int i = 0;
		 i < rock_options.num_levels; i++)
	{
		rock_options.compression_per_level[i] = rocksdb::kNoCompression;
	}

	rock_options.compression = rocksdb::kNoCompression;
	// rock_options.error_if_exists = true;
	// open DB
	rock_s = rocksdb::DB::Open(rock_options, kDBPath, &rock_db);
	printf("rock_s is %s\n", rock_s.ToString().c_str());
	assert(rock_s.ok());
	printf("RocksDB opened!\n");

	std::vector<std::thread> threads;

	ServerOP server_op;
	server_op.local_kv_init_it();

	for (int i = 0; i < no_clients * 20; i++)
	{
		threads.emplace_back(init_connections);
	}

	for (size_t i = 0; i < no_threads; i++)
	{
		threads.emplace_back(server_worker, &server_op, std::ref(*rock_db), no_clients);
	}

	for (auto &thread : threads)
	{
		thread.join();
	}

	fmt::print("** all threads joined **\n");
}

bool recv_message(int sockfd, sockets::client_msg *message)
{
	auto [bytecount, result] = secure_recv(sockfd);

	if (static_cast<int>(bytecount) <= 0 || result == nullptr)
	{
		return false;
	}

	sockets::client_msg temp_msg;
	auto payload_sz = bytecount;
	std::string buffer(result.get(), payload_sz);

	if (!temp_msg.ParseFromString(buffer))
	{
		fmt::print("ParseFromString\n");
		exit(1);
	}

	if (!temp_msg.IsInitialized())
	{
		fmt::print("IsInitialized\n");
		exit(1);
	}

	*message = temp_msg;
	return true;
}

void send_message(int sockfd, server::server_response::reply message)
{
	std::string msg_str;
	message.SerializeToString(&msg_str);

	auto msg_size_payload = msg_str.size();
	auto buf = std::make_unique<char[]>(msg_size_payload + length_size_field);
	construct_message(buf.get(), msg_str.c_str(), msg_size_payload);
	// convert_int_to_byte_array(buf.get(), msg_size);
	// memcpy(buf.get() + length_size_field, msg_str.data(), msg_size);

	secure_send(sockfd, buf.get(), msg_size_payload + length_size_field);
}
