#!/usr/bin/env python3

import abc
import json
import os
import unittest
import uuid
from websocket import create_connection

host = os.getenv('WEBDIS_HOST', '127.0.0.1')
port = int(os.getenv('WEBDIS_PORT', 7379))

class TestWebdis(unittest.TestCase):
	def setUp(self) -> None:
		self.ws = create_connection(f'ws://{host}:{port}/.{self.format()}')

	def tearDown(self) -> None:
		self.ws.close()

	def exec(self, cmd, *args):
		self.ws.send(self.serialize(cmd, *args))
		return self.deserialize(self.ws.recv())

	def clean_key(self):
		"""Returns a key that was just deleted"""
		key = str(uuid.uuid4())
		self.exec('DEL', key)
		return key

	@abc.abstractmethod
	def format(self):
		"""Returns the format to use (added after a dot to the WS URI)"""
		return
	
	@abc.abstractmethod
	def serialize(self, cmd):
		"""Serializes a command according to the format being tested"""
		return

	@abc.abstractmethod
	def deserialize(self, response):
		"""Deserializes a response according to the format being tested"""
		return


class TestJson(TestWebdis):
	def format(self):
		return 'json'

	def serialize(self, cmd, *args):
		return json.dumps([cmd] + list(args))

	def deserialize(self, response):
		return json.loads(response)

	def test_ping(self):
		self.assertEqual(self.exec('PING'), {'PING': [True, 'PONG']})

	def test_multiple_messages(self):
		key = self.clean_key()
		n = 100
		for i in range(n):
			lpush_response = self.exec('LPUSH', key, f'value-{i}')
			self.assertEqual(lpush_response, {'LPUSH': i + 1})
		self.assertEqual(self.exec('LLEN', key), {'LLEN': n})


class TestRaw(TestWebdis):
	def format(self):
		return 'raw'

	def serialize(self, cmd, *args):
		buffer = f"*{1 + len(args)}\r\n${len(cmd)}\r\n{cmd}\r\n"
		for arg in args:
			buffer += f"${len(arg)}\r\n{arg}\r\n"
		return buffer

	def deserialize(self, response):
		return response # we'll just assert using the raw protocol

	def test_ping(self):
		self.assertEqual(self.exec('PING'), "+PONG\r\n")

	def test_get_set(self):
		key = self.clean_key()
		value = str(uuid.uuid4())
		not_found_response = self.exec('GET', key)
		self.assertEqual(not_found_response, "$-1\r\n")  # Redis protocol response for "not found"
		set_response = self.exec('SET', key, value)
		self.assertEqual(set_response, "+OK\r\n")
		get_response = self.exec('GET', key)
		self.assertEqual(get_response, f"${len(value)}\r\n{value}\r\n")


if __name__ == '__main__':
	unittest.main()