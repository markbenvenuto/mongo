# /usr/bin/env python3

#from http.server import HTTPServer,SimpleHTTPRequestHandler
#from socketserver import BaseServer
# import ssl

# httpd = HTTPServer(('localhost', 1443), SimpleHTTPRequestHandler)
# httpd.socket = ssl.wrap_socket (httpd.socket, certfile='certificate.pem', server_side=True)
# httpd.serve_forever()
import http.server
import socketserver
import urllib.parse
import collections 
from bson.codec_options import CodecOptions
import bson

PORT = 8000

class FreeMonHandler(http.server.BaseHTTPRequestHandler):

    """Simple HTTP request handler with GET and HEAD commands.

    This serves files from the current directory and any of its
    subdirectories.  The MIME type for files is determined by
    calling the .guess_type() method.

    The GET and HEAD requests are identical except that the HEAD
    request omits the actual contents of the file.

    """

    def do_GET(self):
        """Serve a GET request."""
        self.send_response(http.HTTPStatus.OK)
        self.send_header("content-type", "mcb/foo")
        self.end_headers()
        self.wfile.write("Hello Registration".encode())

    def do_POST(self):
        """Serve a POST request."""
        parts = urllib.parse.urlsplit(self.path)
        print(parts)
        print("Content-Type: %s" % self.headers.get('content-type'))
        print("Length: %s" % self.headers.get('content-length'))
        path = parts[2]

        if path == '/register':
            self._do_registration()
        elif path == '/metrics':
            self._do_metrics()
        else:
            self.wfile.write("Hello Registration".encode())

    def _send_header(self):
        self.send_response(http.HTTPStatus.OK)
        self.send_header("content-type", "application/octet-stream")
        self.end_headers()


    def _do_registration(self):
        clen = int(self.headers.get('content-length'))

        raw_input = self.rfile.read(clen)
        print("Raw input: %d" % len(raw_input))
        decoded_doc = bson.BSON.decode(raw_input)
        print("Posted document %s" % (decoded_doc))

        data = bson.BSON.encode( {
            'version' : bson.int64.Int64(1),
            'haltMetricsUploading' : False,
            'id' : 'mock123',
            'informationalURL' : 'http://www.example.com/123',
            'message' : 'Welcome to the Mock Free Monitoring Endpoint',
            'reportingInterval' : bson.int64.Int64(1),
            #'userReminder' : 
        }
        )

        # TODO: test what if header is sent first?
        self._send_header()

        self.wfile.write(data)


    def _do_metrics(self):
        clen = int(self.headers.get('content-length'))

        raw_input = self.rfile.read(clen)
        print("Raw input: %d" % len(raw_input))
        decoded_doc = bson.BSON.decode(raw_input)
        #print("Posted document %s" % (decoded_doc))

        data = bson.BSON.encode( {
            'version' : bson.int64.Int64(1),
            'haltMetricsUploading' : False,
            'permanentlyDelete' : False,
            'id' : 'mock123',
            'reportingInterval' : bson.int64.Int64(1),
            'message' : 'Thanks for all the metrics',
            #'userReminder' : 
        }
        )

        # TODO: test what if header is sent first?
        self._send_header()

        self.wfile.write(data)

def run(server_class=http.server.HTTPServer, handler_class=FreeMonHandler):
    server_address = ('', 8000)
    http.server.HTTPServer.protocol_version = "HTTP/1.1"
    print("Listening on %s" % (str(server_address)))
    httpd = server_class(server_address, handler_class)
    httpd.serve_forever()

def main():
    
    run()

if __name__ == '__main__':
    # with open('d:\\m2\\mongo\\metrics.dat', 'rb') as fh:
    #     bytes1 = fh.read()
    #     decoded_doc = bson.BSON.decode(bytes1)

    main()
