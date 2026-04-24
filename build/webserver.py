#!/usr/bin/python3

import sys
import os
import signal
import time
import http.server
import socketserver
import daemon


PID_FILE = "webserver.pid"
LOG_FILE = "webserver.log"
HTTP_PORT = 8000


def kill_instance(pid):
    while True:
        try:
            os.kill(pid, 0)
        except OSError:
            try:
                os.remove(PID_FILE)
            except FileNotFoundError:
                pass
            return
        print(f"Killing previous webserver instance with pid {pid}")
        try:
            os.kill(pid, signal.SIGTERM)
        except OSError:
            pass
        time.sleep(.1)

class SafeHTTPRequestHandler(http.server.SimpleHTTPRequestHandler):
    def send_head(self):
        path = self.translate_path(self.path)
        if os.path.isdir(path):
            for index in ("index.html", "index.htm"):
                index = os.path.join(path, index)
                if os.path.exists(index):
                    path = index
                    break
            else:
                return self.list_directory(path)
        if not os.path.exists(path):
            self.send_error(404, f"File not found: {self.path}")
            return None
        return http.server.SimpleHTTPRequestHandler.send_head(self)

class ReusableTCPServer(socketserver.TCPServer):
    allow_reuse_address = True

def run_server(port):
    with open(PID_FILE, "w") as f:
        f.write(str(os.getpid()))
    with ReusableTCPServer(("", port), SafeHTTPRequestHandler) as httpd:
        print(f"Serving HTTP on port {port}")
        httpd.serve_forever()



def main():
    if len(sys.argv) > 1:
        port = sys.argv[1]
        try:
            port = int(port)
        except ValueError:
            raise RuntimeError(f"Bad port value: {port}")
    else:
        port = 8000
    os.chdir(os.path.dirname(sys.argv[0]))
    os.chdir("bin")
    try:
        with open(PID_FILE) as pid:
            pid = int(pid.read())
    except FileNotFoundError:
        pid = None
    if pid:
        kill_instance(pid)
    # MAIN
    log_file = open(LOG_FILE, "a+")
    with daemon.DaemonContext(
        stderr = log_file,
        stdout = log_file,
        working_directory = os.getcwd()
    ):
        try:
            run_server(port)
        except Exception as e:
            try:
                print(f"Removing pid file {PID_FILE}")
                os.remove(PID_FILE)
            except:
                pass
            raise e

if __name__ == "__main__":
    main()

