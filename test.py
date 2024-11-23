#!/usr/bin/env python3
import requests
from socket import *

ret = requests.get("http://localhost:6969/index.html")
print(ret)

'''
sock = socket(AF_INET, SOCK_STREAM)
sock.connect(("localhost", 6969))

sock.sendall(b"GET /index.html HTTP/1.1")
print(sock.recv(1024))
'''
