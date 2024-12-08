import http.client
import random
import string
import contextlib
import time
import socket

def randbytes(length=8):
    """Generate a random string of given length."""
    return ''.join(random.choices(string.ascii_lowercase + string.digits, k=length))

def send_requests():
    # Connect to the server
    # conn = socket.create_connection(("localhost", 8080))  # Change the port if necessary

    # conn.send('GET / HTTP/1.1\r\nContent-Length: 0\r\n\r\nGET / HTTP/1.1\r\nContent-Length: 0\r\n\r\n'.encode())
    # time.sleep(.5)
    # response = conn.recv(1024).split(b'\r\n\r\n')
    # print(response)

    conn =  http.client.HTTPConnection("localhost", 8080)
    path = f'/dynamic/{randbytes(8)}'
    content = randbytes(32)

    conn.request('GET', path)
    response = conn.getresponse()
    payload = response.read()
    print(response.status)
    assert response.status == 404, f"'{path}' should be missing, but GET was not answered with '404'"

    conn.request('PUT', path, content)
    response = conn.getresponse()
    payload = response.read()
    print(response.status)
    assert response.status in {200, 201, 202, 204}, f"Creation of '{path}' did not yield '201'"

    conn.request('GET', path)
    response = conn.getresponse()
    payload = response.read().decode('utf-8')
    print(response.status)
    print(payload+"="+content)
    assert response.status == 200
    assert payload == content, f"Content of '{path}' does not match what was passed"

    conn.request('DELETE', path)
    response = conn.getresponse()
    response.read()
    print(response.status)
    assert response.status in {200, 202, 204}, f"Deletion of '{path}' did not succeed"

    conn.request('GET', path)
    response = conn.getresponse()
    response.read()
    print(response.status)
    assert response.status == 404, f"'{path}' should be missing"

if __name__ == "__main__":
    send_requests()
