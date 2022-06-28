#!/usr/bin/env python3

from testsupport import subtest
from socketsupport import test_server


def main() -> None:
    with subtest("Testing 2-threaded server with 8-threaded client"):
        test_server(num_server_threads=2, num_client_threads=8, num_client_instances=1, 
                    trace_file="./source/workload_traces/counter.txt", messages=312500, timed=False)

if __name__ == "__main__":
    main()
