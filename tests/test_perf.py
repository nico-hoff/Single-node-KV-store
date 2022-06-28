#!/usr/bin/env python3

from testsupport import subtest, info, warn
from socketsupport import test_server


def main() -> None:
    scale_factor = 1.6
    times =[]
    with subtest("Testing for performance"):
        times.append(test_server(num_server_threads=1, num_client_threads=4, num_client_instances=1,
                                trace_file="./source/workload_traces/counter.txt", messages=312500, timed=True))
        times.append(test_server(num_server_threads=2, num_client_threads=4, num_client_instances=1,
                                trace_file="./source/workload_traces/counter.txt", messages=312500, timed=True))
        times.append(test_server(num_server_threads=4, num_client_threads=4, num_client_instances=1,
                                trace_file="./source/workload_traces/counter.txt", messages=312500, timed=True))
    
    f1 = times[0] / times[1]
    f2 = times[1] / times[2]
    if f1 < scale_factor or f2 < scale_factor:
        warn("Server is not scaling properly: " + str(times))
        exit(1)
    else:
        info(f"Achieved scaling 1->2 {f1} and 2->4 {f2}.")
if __name__ == "__main__":
    main()
