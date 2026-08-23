# WinInspect Python SDK

Python client library for the WinInspect remote desktop inspection and automation daemon.

## Installation

```bash
pip install .
```

## Quick Start

```python
from wininspect import Client

# Connect to a local daemon over TCP
client = Client(transport="tcp", host="127.0.0.1", port=1985)

# List windows
windows = client.call("window.listTop")
for w in windows["result"]:
    print(f"  {w['title']} ({w['hwnd']})")

# Capture the screen
capture = client.call("screen.capture", left=0, top=0, right=1920, bottom=1080)
with open("screenshot.bmp", "wb") as f:
    f.write(base64.b64decode(capture["result"]["data_b64"]))

# Get daemon identity
identity = client.call("daemon.identity")
print(f"Connected to: {identity['result']['name']} ({identity['result']['uuid']})")

client.close()
```

## Usage with HTTP transport

```python
from wininspect import Client

client = Client(transport="http", host="127.0.0.1", port=8088, token="mytoken")
print(client.call("daemon.health"))
```

## WebSocket Events

```python
from wininspect import Client

client = Client(transport="ws", host="127.0.0.1", port=8088)

for event in client.events():
    print(f"Event: {event}")
```

## Control State Management

```python
from wininspect import Client, ControllerType

client = Client()

# Take control as a human
client.take_control(ControllerType.HUMAN, id="my-session")

# Do some actions
client.click(500, 500)
client.type_text("Hello, world!")

# Release control
client.release_control(ControllerType.HUMAN)

# Get audit log
audit = client.call("control.auditLog", max=10)
```

## Session Recording

```python
client.start_recording(output_path="session.wisession", interval_ms=500)
# ... do things ...
result = client.stop_recording()
print(f"Recorded {result['frames']} frames")
```
