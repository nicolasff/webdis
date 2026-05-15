#!/usr/bin/python3
import socket
import unittest

import os
HOST = os.getenv('WEBDIS_HOST', '127.0.0.1')
PORT = int(os.getenv('WEBDIS_PORT', 7379))

class BlockingSocket:

	def __init__(self):
		self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
		self.s.setblocking(True)
		self.s.connect((HOST, PORT))

	def __del__(self):
		self.s.close()

	def recv(self):
		out = b""
		while True:
			try:
				ret = self.s.recv(4096)
			except:
				return out
			if len(ret) == 0:
				return out
			out += ret

	def recv_until(self, limit):
		out = b""
		while not limit in out:
			try:
				out += self.s.recv(4096)
			except:
				return False
		return out

	def send(self, buf):
		sz = len(buf)
		done = 0
		while done < sz:
			try:
				ret = self.s.send(buf[done:4096])
			except:
				return False
			done = done + ret
			# print "Sent %d/%d so far (%s just now)" % (done, sz, ret)
			if ret < 0:
				return False
		return True

class LargeString:

	def __init__(self, c, n):
		self.char = c
		self.len = n

	def __len__(self):
		return self.len

	def __getitem__(self, chunk):
		if chunk.start > self.len:
			return b""
		if chunk.start + chunk.stop > self.len:
			return self.char * (self.len - chunk.start)
		return self.char * chunk.stop

class TestSocket(unittest.TestCase):

	def __init__(self, *args, **kwargs):
		super(TestSocket, self).__init__(*args, **kwargs)
		self.s = BlockingSocket()


class TestHugeUrl(TestSocket):

	def test_huge_url(self):
		n = 1024*1024*1024	# 1GB query-string

		start = b"GET /GET/x"
		end = b" HTTP/1.0\r\n\r\n"

		ok = self.s.send(start)
		fail1 = self.s.send(LargeString(b"A", n))
		fail2 = self.s.send(end)
		out = self.s.recv()

		self.assertTrue(ok)
		self.assertTrue(b"400 Bad Request" in out)

	UPLOAD_SIZE = 1024*1024*1024	# 1GB - well above default 128MiB cap

	def _send_large_body_assert_413(self, ok):
		fail = self.s.send(LargeString(b"A", self.UPLOAD_SIZE))
		out = self.s.recv()
		self.assertTrue(ok)
		self.assertFalse(fail)
		self.assertTrue(b"413 Request Entity Too Large" in out)
		return out

	def test_huge_upload(self):
		start = b"PUT /SET/x HTTP/1.0\r\n"\
		+ ("Content-Length: %d\r\n" % (self.UPLOAD_SIZE)).encode('utf-8')\
		+ b"Expect: 100-continue\r\n\r\n"

		ok = self.s.send(start)
		cont = self.s.recv_until(b"\r\n")
		self.assertTrue(b"HTTP/1.1 100 Continue" in cont)
		self._send_large_body_assert_413(ok)

	def test_huge_upload_keepalive(self):
		# HTTP/1.1 keep-alive variant: exercises the worker.c branch that
		# forces c->keep_alive=0 so http_response_cleanup closes the fd.
		start = b"PUT /SET/x HTTP/1.1\r\n"\
		+ b"Host: localhost\r\n"\
		+ ("Content-Length: %d\r\n" % (self.UPLOAD_SIZE)).encode('utf-8')\
		+ b"\r\n"

		ok = self.s.send(start)
		out = self._send_large_body_assert_413(ok)
		self.assertTrue(b"Connection: Close" in out)

if __name__ == '__main__':
	unittest.main(verbosity=5)
