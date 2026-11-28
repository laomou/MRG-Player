#!/usr/bin/env python3
"""Run a command on the emulated Brick through its root shell socket.

    tools/brick-exec.py out/run/shell.sock "pidof MainUI"
    tools/brick-exec.py --timeout 120 out/run/shell.sock "ls /mnt/SDCARD/Apps"

The socket is the one `./brick run` creates (out/run/shell.sock by default,
or --shell-sock). Unlike `./brick shell`, this is not interactive, so a test
script can call it and read the output.
"""

import argparse
import re
import socket
import sys
import time

ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
PROMPT = re.compile(r"^\S*\s*[#$]\s*$")


def drain(sock, quiet=0.25):
    sock.settimeout(quiet)
    data = b""
    while True:
        try:
            chunk = sock.recv(65536)
        except socket.timeout:
            break
        if not chunk:
            break
        data += chunk
    return data


def read_until(sock, marker, timeout, occurrences=1):
    deadline = time.time() + timeout
    data = b""
    while time.time() < deadline:
        sock.settimeout(0.3)
        try:
            chunk = sock.recv(65536)
        except socket.timeout:
            if data.count(marker) >= occurrences:
                break
            continue
        if not chunk:
            break
        data += chunk
        if data.count(marker) >= occurrences:
            break
    return data


def run(sock_path, command, timeout):
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect(sock_path)
    try:
        # Clear a half-typed line or an open quote left by an earlier call.
        sock.sendall(b"\x03\n")
        drain(sock, 0.3)

        # The tty echoes the input and wraps long commands, so bracket the real
        # output with marker lines and cut between them. The echoed "echo
        # MRG_BEGIN_x" line is not equal to the marker itself, so it is skipped.
        stamp = "%d" % (int(time.time() * 1000) % 1000000)
        begin = "MRG_BEGIN_%s" % stamp
        end = "MRG_END_%s" % stamp
        payload = (
            "echo %s\r\n" % begin
            + command
            + "\r\necho %s\r\n" % end
        )
        sock.sendall(payload.encode())
        data = read_until(sock, end.encode(), timeout, occurrences=2)

        text = ANSI.sub("", data.decode("utf-8", "replace")).replace("\r", "")
        lines = text.split("\n")
        start = -1
        for index, line in enumerate(lines):
            if line.strip() == begin:
                start = index
        stop = len(lines)
        if start >= 0:
            for index in range(start + 1, len(lines)):
                if lines[index].strip() == end:
                    stop = index
                    break
        body = lines[start + 1:stop]
        # Drop the echoed command. It sits between the begin marker and the
        # command output, and a long line arrives wrapped, so compare with all
        # whitespace removed instead of line by line.
        wanted = "".join(command.split())
        seen = ""
        while body and seen != wanted:
            echo_line = re.sub(r"^\s*\S*\s*#\s?", "", body[0], count=1)
            seen += "".join(echo_line.split())
            body.pop(0)
        end_echo = "echo %s" % end
        output = []
        for line in body:
            stripped = line.strip()
            if not stripped or PROMPT.match(stripped):
                continue
            if stripped.endswith(end_echo):
                continue
            output.append(line.rstrip())
        return "\n".join(output).strip()
    finally:
        sock.close()


def main():
    parser = argparse.ArgumentParser(
        description="Run a command on the emulated Brick through its root shell socket."
    )
    parser.add_argument("socket", help="root shell socket, e.g. out/run/shell.sock")
    parser.add_argument("command", help="command to run on the Brick")
    parser.add_argument("--timeout", type=float, default=60.0)
    args = parser.parse_args()
    output = run(args.socket, args.command, args.timeout)
    if output:
        print(output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
