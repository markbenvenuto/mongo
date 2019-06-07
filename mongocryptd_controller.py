import subprocess
import json
import os.path
import threading
import sys
import socket
import platform
import time

from pymongo import MongoClient

MONGOCRYPTD_PATH="./mongocryptd"
if platform.system() == "Windows":
    MONGOCRYPTD_PATH += ".exe"

def read_pid(pid_file):
    """Read the pid file from disk."""
    with open(pid_file, 'r', encoding='utf-8') as fh:
        return fh.read()

def get_port(pid_file):
    """
    Get the port by reading the pid file.

    The pid file has 3 possible formats:

    1. missing
    - the pid file does not exist if mongocryptd was never started
    2. empty
    - the pid file is empty when mongocryptd ran at least once but is not running now
    3. a json file
    - the pid file is a json file when mongocryptd is running
    {
        port : <int>
        domainSocket: <string> # Not present on Windows
        pid : <int>
    }
    port - is the port that mongocryptd is running on
    pid - is the process number of mongocryptd
    domainSocket - path to unix domain socket, not present on Windows
    """
    # Check if one is already running
    if os.path.exists(pid_file):
        pid_str = read_pid(pid_file)

        # Pid file exists but mongocrypt cleanly shutdown
        if not pid_str:
            return -1

        # Check if we have a valid json pid file
        try:
            pid_str = pid_str.strip()
            pid_json = json.loads(pid_str)
        except:
            # If we hit an exception, either mongocrypt has been shutdown or
            # something bad has happened. mongocryptd writes the json blurb atomically
            raise "Huh?"

        port = int(pid_json["port"])

        return port

    return -1

def start(pid_file, port):
    # The startup procedure is as follows:
    # 1. Try to find an existing mongocryptd
    # 2. Try to start a new one

    running_port = get_port(pid_file)

    while running_port == -1:
        if port == 0:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.bind(("0.0.0.0", 0))
            port = sock.getsockname()[1]

        # Since the pid file was not found or we did not find a valid port, start mongocryptd
        cmd_str = [MONGOCRYPTD_PATH, "--port=" + str(port), "--pidfilepath="+pid_file]
        print("Starting mongocryptd: " + str(cmd_str))

        flags = 0
        if platform.system() == "Windows":
            flags = subprocess.CREATE_NEW_CONSOLE;

        subprocess.Popen(cmd_str, creationflags=flags, close_fds=True)

        # Sleep for a little bit to give mongocryptd a chance to start
        time.sleep(0.1)

        running_port = get_port(pid_file)


    print("Found mongocryptd running on port {}".format(running_port))
    port = running_port

    client = MongoClient('localhost:{}'.format(port))

    print("Verifiying MongoCryptd successfully running")
    client.admin.command("buildinfo", 1)

    print("MongoCryptd successfully running!")

    sys.exit(1)

def main():
    # type: () -> None
    """Execute Main Entry point."""

    # Start with a fix port
    start("foo.pid", 1234)

    # Start with dynamically allocated port
    #start("foo.pid", 0)

if __name__ == '__main__':
    main()
