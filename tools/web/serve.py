import http.client
import http.server
import ipaddress
import os
import socketserver
import sys

root = os.path.dirname(os.path.abspath(__file__))
port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
os.chdir(root)


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, ".wasm": "application/wasm", ".js": "text/javascript"}

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def _relay(self):
        parts = self.path.split("/", 4)
        if len(parts) < 5 or not parts[3].isdigit():
            self.send_error(400)
            return
        host, port, rest = parts[2], int(parts[3]), "/" + parts[4]
        try:
            ip = ipaddress.ip_address(__import__("socket").gethostbyname(host))
            if not (ip.is_loopback or ip.is_private):
                self.send_error(403, "relay only reaches local/private addresses")
                return
            length = int(self.headers.get("Content-Length") or 0)
            body = self.rfile.read(length) if length else None
            headers = {}
            if self.headers.get("Content-Type"):
                headers["Content-Type"] = self.headers["Content-Type"]
            conn = http.client.HTTPConnection(host, port, timeout=30)
            conn.request(self.command, rest, body, headers)
            r = conn.getresponse()
            data = r.read()
            self.send_response(r.status)
            self.send_header("Content-Type", r.getheader("Content-Type", "application/octet-stream"))
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            conn.close()
        except Exception as e:
            self.send_error(502, str(e))

    def do_POST(self):
        if self.path.startswith("/__relay/"):
            self._relay()
        else:
            self.send_error(501)

    def do_GET(self):
        if self.path.startswith("/__relay/"):
            self._relay()
        else:
            super().do_GET()

    def log_message(self, *args):
        pass


socketserver.ThreadingTCPServer.allow_reuse_address = True
with socketserver.ThreadingTCPServer(("127.0.0.1", port), Handler) as server:
    print(f"Perlin web: http://localhost:{port}/")
    server.serve_forever()
