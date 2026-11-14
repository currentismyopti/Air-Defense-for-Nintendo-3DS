#!/usr/bin/env python3
"""Serves AirDefense.cia from this computer and tells FBI on the 3DS to install it.

On the 3DS:  FBI > Remote Install > Receive URLs over the network   (it shows the console's IP address)
On the PC:   python3 tools/send_to_fbi.py 192.168.1.50

Without an IP address it only serves the file and prints the URL (and a QR code if the `qrcode`
Python package is installed) for FBI > Remote Install > Scan QR Code / Manually enter URL."""
import http.server, os, socket, struct, sys, threading, time
from urllib.parse import quote

PORT = 8080
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
args = [a for a in sys.argv[1:]]
cia = next((a for a in args if a.lower().endswith(".cia")), os.path.join(root, "AirDefense.cia"))
target = next((a for a in args if not a.lower().endswith(".cia")), None)
if not os.path.isfile(cia):
    sys.exit("Can't find %s. Build it first (./build.sh) or pass the path to the .cia." % cia)

def my_ip(peer):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect((peer or "8.8.8.8", 9))
        return s.getsockname()[0]
    finally:
        s.close()

class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=os.path.dirname(os.path.abspath(cia)), **k)
    def log_message(self, fmt, *a):
        print("  3DS:", fmt % a)

server = http.server.ThreadingHTTPServer(("", PORT), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
url = "http://%s:%d/%s" % (my_ip(target), PORT, quote(os.path.basename(cia)))
print("Serving", cia)
print("URL:", url)

if target:
    print("Sending the URL to FBI at %s:5000 ..." % target)
    payload = url.encode("ascii")
    try:
        sock = socket.create_connection((target, 5000), timeout=10)
        sock.sendall(struct.pack("!L", len(payload)) + payload)
        sock.settimeout(None)
        print("FBI has the URL. Press A on the 3DS to install. Waiting for it to finish ...")
        while len(sock.recv(1)) < 1:
            time.sleep(0.05)
        sock.close()
        print("Done.")
    except OSError as e:
        print("Couldn't reach FBI (%s). Is 'Receive URLs over the network' open, and are both on the same Wi-Fi?" % e)
        print("The file is still being served; you can type the URL into FBI instead. Ctrl+C to stop.")
        while True:
            time.sleep(1)
else:
    try:
        import qrcode
        qr = qrcode.QRCode(border=2)
        qr.add_data(url)
        qr.print_ascii(invert=True)
        print("Scan with FBI > Remote Install > Scan QR Code.")
    except ImportError:
        print("In FBI: Remote Install > Manually enter URL (or `pip3 install qrcode` to get a QR code here).")
    print("Ctrl+C to stop.")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass
