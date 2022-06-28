import tempfile
import sys
import random
import subprocess
import os
import time
import datetime
import psutil

from testsupport import (
    run_project_executable,
    info,
    warn,
    find_project_executable,
    test_root,
    project_root,
    run,
    ensure_library,
)

def is_server_listening(port: int) -> bool:
    for conn in psutil.net_connections():
        if conn.laddr.port == port and conn.status == "LISTEN":
            return True
    return False

def run_client(num_threads: int, port: int, trace_file: str, messages: int):
    info(
        f"Running client with {num_threads} threads with keys from {trace_file} trace file."
    )

    with tempfile.TemporaryFile(mode="w+") as stdout:
        run_project_executable(
            "clt",
            args=[
                "-c", str(num_threads),
                "-s", "localhost",
                "-m", str(messages),
                "-p", str(port),
                "-t", trace_file,
            ],
            stdout=stdout,
        )
        
        return True


def test_server(
    num_server_threads: int,
    num_client_threads: int,
    num_client_instances: int,
    trace_file: str,
    messages: int,
    timed: bool
) -> None:
    try:
        # make sure port 1025 is not binded anymore
        while is_server_listening(1025):
            info("Waiting for port to be unbound")
            time.sleep(5)
        
        run(["docker-compose", "up", "server"])
        server = ["docker-compose", "run", "--service-ports", "-T", "server", "/app/build/dev/svr", \
                "-n", str(num_server_threads), "-s", "localhost", "-p", "1025", \
                "-c", str(num_client_threads), "-o", "1"]

        info(f"Run server with {num_server_threads} threads...")

        print(" ".join(server))
        info(f"{server}")
        with subprocess.Popen(
            server,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        ) as proc:
            # Wait for server init
            timeout = time.time() + 60*5 # 5 minutes timeout
            while not is_server_listening(1025):
                time.sleep(0.1)
                if time.time() > timeout:
                    warn(f"Server init timed-out")
                    sys.exit(1)
            info("Server is listening at port 1025")
            
            if timed:
                start = datetime.datetime.now()

            for _ in range(num_client_instances):
                run_client(num_client_threads, 1025, trace_file, messages)

            if timed:
                end = datetime.datetime.now()
                diff = end - start

            proc.terminate()
            stdout, stderr = proc.communicate()

            info("OK")

    except OSError as e:
        warn(f"Failed to run command: {e}")
        sys.exit(1)
    finally:    
        run(["docker-compose", "kill"])
        if timed:
            return diff
