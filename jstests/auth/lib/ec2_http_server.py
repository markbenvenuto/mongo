#! /usr/bin/env python3
"""Mock AWS KMS Endpoint."""

import argparse
import collections
import base64
import http.server
import json
import logging
import socketserver
import sys
import urllib.parse
import ssl

import ec2_http_common

SECRET_PREFIX = "00SECRET"

# Pass this data out of band instead of storing it in AwsKmsHandler since the
# BaseHTTPRequestHandler does not call the methods as object methods but as class methods. This
# means there is not self.
stats = ec2_http_common.Stats()
disable_faults = False
fault_type = None

"""Fault which causes encrypt to return 500."""
FAULT_ENCRYPT = "fault_encrypt"

"""Fault which causes encrypt to return an error that contains a type and message"""
FAULT_ENCRYPT_CORRECT_FORMAT = "fault_encrypt_correct_format"

"""Fault which causes encrypt to return wrong fields in JSON."""
FAULT_ENCRYPT_WRONG_FIELDS = "fault_encrypt_wrong_fields"

"""Fault which causes encrypt to return bad BASE64."""
FAULT_ENCRYPT_BAD_BASE64 = "fault_encrypt_bad_base64"

"""Fault which causes decrypt to return 500."""
FAULT_DECRYPT = "fault_decrypt"

"""Fault which causes decrypt to return an error that contains a type and message"""
FAULT_DECRYPT_CORRECT_FORMAT = "fault_decrypt_correct_format"

"""Fault which causes decrypt to return wrong key."""
FAULT_DECRYPT_WRONG_KEY = "fault_decrypt_wrong_key"


# List of supported fault types
SUPPORTED_FAULT_TYPES = [
    FAULT_ENCRYPT,
    FAULT_ENCRYPT_CORRECT_FORMAT,
    FAULT_ENCRYPT_WRONG_FIELDS,
    FAULT_ENCRYPT_BAD_BASE64,
    FAULT_DECRYPT,
    FAULT_DECRYPT_CORRECT_FORMAT,
    FAULT_DECRYPT_WRONG_KEY,
]

class AwsKmsHandler(http.server.BaseHTTPRequestHandler):
    """
    Handle requests from AWS KMS Monitoring and test commands
    """
    # HTTP 1.1 requires us to always send the length back
    #protocol_version = "HTTP/1.1"

    def do_GET(self):
        """Serve a Test GET request."""
        parts = urllib.parse.urlsplit(self.path)
        path = parts[2]

        if path == ec2_http_common.URL_PATH_STATS:
            self._do_stats()
        elif path == ec2_http_common.URL_DISABLE_FAULTS:
            self._do_disable_faults()
        elif path == ec2_http_common.URL_ENABLE_FAULTS:
            self._do_enable_faults()
        elif path == "/latest/meta-data/iam/security-credentials/":
            self._do_security_credentials()
        elif path == "/latest/meta-data/iam/security-credentials/mock_role":
            self._do_security_credentials_mock_role()
        else:
            self.send_response(http.HTTPStatus.NOT_FOUND)
            self.end_headers()
            self.wfile.write("Unknown URL".encode())

    def do_POST(self):
        """Serve a POST request."""
        parts = urllib.parse.urlsplit(self.path)
        path = parts[2]

        if path == "/":
            self._do_post()
        else:
            self.send_response(http.HTTPStatus.NOT_FOUND)
            self.end_headers()
            self.wfile.write("Unknown URL".encode())

    def _send_reply(self, data, status=http.HTTPStatus.OK):
        print("Sending Response: " + data.decode())

        self.send_response(status)
        self.send_header("content-type", "application/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()

        self.wfile.write(data)


    def _send_header(self):
        self.send_response(http.HTTPStatus.OK)
        self.send_header("content-type", "application/octet-stream")
        self.end_headers()

    def _do_stats(self):
        self._send_header()

        self.wfile.write(str(stats).encode('utf-8'))

    def _do_disable_faults(self):
        global disable_faults
        disable_faults = True
        self._send_header()

    def _do_enable_faults(self):
        global disable_faults
        disable_faults = False
        self._send_header()

    def _do_security_credentials(self):
        self._send_header()

        self.wfile.write(str("mock_role\n").encode('utf-8'))

    def _do_security_credentials_mock_role(self):
        self._send_header()

        with open("creds.json") as fh:
        # These keys are periodically rotated by AWS
            str1 = fh.read()

        self.wfile.write(str1.encode('utf-8'))


def run(port, server_class=http.server.HTTPServer, handler_class=AwsKmsHandler):
    """Run web server."""
    server_address = ('', port)

    httpd = server_class(server_address, handler_class)

    print("Mock EC2 Instance Metadata Web Server Listening on %s" % (str(server_address)))

    httpd.serve_forever()


def main():
    """Main Method."""
    global fault_type
    global disable_faults

    parser = argparse.ArgumentParser(description='MongoDB Mock AWS KMS Endpoint.')

    parser.add_argument('-p', '--port', type=int, default=8000, help="Port to listen on")

    parser.add_argument('-v', '--verbose', action='count', help="Enable verbose tracing")

    parser.add_argument('--fault', type=str, help="Type of fault to inject")

    parser.add_argument('--disable-faults', action='store_true', help="Disable faults on startup")

    args = parser.parse_args()
    if args.verbose:
        logging.basicConfig(level=logging.DEBUG)

    if args.fault:
        if args.fault not in SUPPORTED_FAULT_TYPES:
            print("Unsupported fault type %s, supports types are %s" % (args.fault, SUPPORTED_FAULT_TYPES))
            sys.exit(1)

        fault_type = args.fault

    if args.disable_faults:
        disable_faults = True

    run(args.port)


if __name__ == '__main__':

    main()
