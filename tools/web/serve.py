import http.server
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

    def log_message(self, *args):
        pass


socketserver.ThreadingTCPServer.allow_reuse_address = True
with socketserver.ThreadingTCPServer(("127.0.0.1", port), Handler) as server:
    print(f"Perlin web: http://localhost:{port}/")
    server.serve_forever()
