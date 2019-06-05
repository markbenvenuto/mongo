import subprocess
import json
import os.path
import threading
import sys
import socket

from pymongo import MongoClient

MONGOCRYPTD_PATH=r"d:\m2\mongo\mongocryptd.exe"

def read_pid(pid_file):
    with open(pid_file, 'r', encoding='utf-8') as fh:
        return fh.read()

def get_port(pid_file):

    # Check if one is already running
    if os.path.exists(pid_file):
        pid_str = read_pid(pid_file)

        # Pid file exists but mongocrypt cleanly shutdown
        if not pid_str:
            return -1

        # When mongocryptd starts up, it first writes a single integer to the pid file
        # So if we hit this small timing window, keep trying until it is no longer an int
        try:
            while True:
                pid_str = read_pid(pid_file)
                pid_number = int(pid_str)
                threading.sleep(10)
        except:
            pass

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

    if running_port == -1:
        if port == 0:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.bind(("0.0.0.0", 0))
            port = sock.getsockname()[1]

        # Since the pid file was not found or we did not find a valid port, start mongocryptd
        cmd_str = "{} --port={} --pidfilepath={}".format(MONGOCRYPTD_PATH, port, pid_file)
        print("Starting mongocryptd: " + cmd_str)
        subprocess.Popen(cmd_str, creationflags=subprocess.CREATE_NEW_CONSOLE, close_fds=True)
    else:
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

    #start("foo.pid", 1234)

    start("foo.pid", 0)

if __name__ == '__main__':
    main()
