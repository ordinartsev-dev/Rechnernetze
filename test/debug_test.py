import subprocess
import time
from http.client import HTTPConnection
import pytest

@pytest.fixture
def webserver(port):
    """Запускает сервер перед тестом и завершает его после теста."""
    server_process = subprocess.Popen(['build/webserver', '127.0.0.1', str(port)])
    time.sleep(1)  # Даём время серверу запуститься
    yield  # Возвращаем управление тесту
    server_process.terminate()
    server_process.wait()

@pytest.mark.timeout(1)
def test_put_method(port):
    conn = HTTPConnection('127.0.0.1', port)
    conn.connect()

    path = '/dynamic/test-resource'
    content = b'This is a test content'
    headers = {'Content-Length': str(len(content))}

    # Создание нового ресурса
    conn.request('PUT', path, content, headers)
    response = conn.getresponse()
    assert response.status == 201, f"Expected 201 Created, got {response.status}"

    # Проверка существующего ресурса
    conn.request('GET', path)
    response = conn.getresponse()
    assert response.status == 200
    payload = response.read()
    assert payload == content, f"Expected content '{content}', got '{payload}'"

    # Обновление ресурса
    updated_content = b'Updated content'
    headers = {'Content-Length': str(len(updated_content))}
    conn.request('PUT', path, updated_content, headers)
    response = conn.getresponse()
    assert response.status == 204, f"Expected 204 No Content, got {response.status}"

    # Проверка обновленного ресурса
    conn.request('GET', path)
    response = conn.getresponse()
    assert response.status == 200
    payload = response.read()
    assert payload == updated_content, f"Expected content '{updated_content}', got '{payload}'"