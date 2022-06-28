#!/usr/bin/env python3

from testsupport import subtest
from socketsupport import test_server


def main() -> None:
    with subtest("Testing single-threaded server with single-threaded client"):
        test_server(num_server_threads=1, num_client_threads=1, num_client_instances=1,
                    trace_file="./source/workload_traces/12K_traces.txt", messages=11900, timed=False)


if __name__ == "__main__":
    main()
