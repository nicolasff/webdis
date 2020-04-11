#!/usr/bin/python3
import urllib.request, urllib.error, urllib.parse, unittest, json, hashlib
from functools import wraps
try:
	import msgpack
except:
	msgpack = None

import os
host = os.getenv('WEBDIS_HOST', '127.0.0.1')
port = int(os.getenv('WEBDIS_PORT', 7379))

class TestWebdis(unittest.TestCase):

	def wrap(self,url):
		return 'http://%s:%d/%s' % (host, port, url)

	def query(self, url, data = None, headers={}):
		r = urllib.request.Request(self.wrap(url), data, headers)
		return urllib.request.urlopen(r)

class TestBasics(TestWebdis):

	def test_crossdomain(self):
		f = self.query('crossdomain.xml')
		self.assertTrue(f.getheader('Content-Type') == 'application/xml')
		self.assertTrue(b"allow-access-from domain" in f.read())

	def test_options(self):
		pass
		# not sure if OPTIONS is supported by urllib2...
		#	f = self.query('')	# TODO: call with OPTIONS.
		#	self.assertTrue(f.headers.getheader('Content-Type') == 'text/html')
		#	self.assertTrue(f.headers.getheader('Allow') == 'GET,POST,PUT,OPTIONS')
		#	self.assertTrue(f.headers.getheader('Content-Length') == '0')
		#	self.assertTrue(f.headers.getheader('Access-Control-Allow-Origin') == '*')


class TestJSON(TestWebdis):

	def test_set(self):
		"success type (+OK)"
		self.query('DEL/hello')
		f = self.query('SET/hello/world')
		self.assertTrue(f.getheader('Content-Type') == 'application/json')
		self.assertTrue(f.getheader('ETag') == '"0db1124cf79ffeb80aff6d199d5822f8"')
		self.assertTrue(f.read() == b'{"SET":[true,"OK"]}')

	def test_get(self):
		"string type"
		self.query('SET/hello/world')
		f = self.query('GET/hello')
		self.assertTrue(f.getheader('Content-Type') == 'application/json')
		self.assertTrue(f.getheader('ETag') == '"8cf38afc245b7a6a88696566483d1390"')
		self.assertTrue(f.read() == b'{"GET":"world"}')

	def test_incr(self):
		"integer type"
		self.query('DEL/hello')
		f = self.query('INCR/hello')
		self.assertTrue(f.getheader('Content-Type') == 'application/json')
		self.assertTrue(f.getheader('ETag') == '"500e9bcdcbb1e98f25c1fbb880a96c99"')
		self.assertTrue(f.read() == b'{"INCR":1}')

	def test_list(self):
		"list type"
		self.query('DEL/hello')
		self.query('RPUSH/hello/abc')
		self.query('RPUSH/hello/def')
		f = self.query('LRANGE/hello/0/-1')
		self.assertTrue(f.getheader('Content-Type') == 'application/json')
		self.assertTrue(f.getheader('ETag') == '"622e51f547a480bef7cf5452fb7782db"')
		self.assertTrue(f.read() == b'{"LRANGE":["abc","def"]}')

	def test_error(self):
		"error return type"
		f = self.query('UNKNOWN/COMMAND')
		self.assertTrue(f.getheader('Content-Type') == 'application/json')
		try:
			obj = json.loads(f.read().decode('utf-8'))
		except:
			self.assertTrue(False)
			return

		self.assertTrue(len(obj) == 1)
		self.assertTrue('UNKNOWN' in obj)
		self.assertTrue(isinstance(obj['UNKNOWN'], list))
		self.assertTrue(obj['UNKNOWN'][0] == False)
		self.assertTrue(isinstance(obj['UNKNOWN'][1], str))

class TestCustom(TestWebdis):
	def test_list(self):
		"List responses with custom format"
		self.query('DEL/hello')
		self.query('RPUSH/hello/a/b/c')
		f = self.query('LRANGE/hello/0/-1.txt')
		self.assertTrue(f.getheader('Content-Type') == 'text/plain')
		self.assertTrue(f.read() == b"abc")

	def test_separator(self):
		"Separator in list responses with custom format"
		self.query('DEL/hello')
		self.query('RPUSH/hello/a/b/c')
		f = self.query('LRANGE/hello/0/-1.txt?sep=--')
		self.assertTrue(f.getheader('Content-Type') == 'text/plain')
		self.assertTrue(f.read() == b"a--b--c")

class TestRaw(TestWebdis):

	def test_set(self):
		"success type (+OK)"
		self.query('DEL/hello')
		f = self.query('SET/hello/world.raw')
		self.assertTrue(f.getheader('Content-Type') == 'binary/octet-stream')
		self.assertTrue(f.read() == b"+OK\r\n")

	def test_get(self):
		"string type"
		self.query('SET/hello/world')
		f = self.query('GET/hello.raw')
		self.assertTrue(f.read() == b'$5\r\nworld\r\n')

	def test_incr(self):
		"integer type"
		self.query('DEL/hello')
		f = self.query('INCR/hello.raw')
		self.assertTrue(f.read() == b':1\r\n')

	def test_list(self):
		"list type"
		self.query('DEL/hello')
		self.query('RPUSH/hello/abc')
		self.query('RPUSH/hello/def')
		f = self.query('LRANGE/hello/0/-1.raw')
		self.assertTrue(f.read() == b"*2\r\n$3\r\nabc\r\n$3\r\ndef\r\n")

	def test_error(self):
		"error return type"
		f = self.query('UNKNOWN/COMMAND.raw')
		self.assertTrue(f.read().startswith(b"-ERR "))

def need_msgpack(fn):
	def wrapper(self):
		if msgpack:
			fn(self)
	return wrapper

class TestMsgPack(TestWebdis):

	@need_msgpack
	def test_set(self):
		"success type (+OK)"
		self.query('DEL/hello')
		f = self.query('SET/hello/world.msg')
		self.assertTrue(f.getheader('Content-Type') == 'application/x-msgpack')
		obj = msgpack.loads(f.read())
		self.assertTrue(obj == {'SET': (True, 'OK')})

	@need_msgpack
	def test_get(self):
		"string type"
		self.query('SET/hello/world')
		f = self.query('GET/hello.msg')
		obj = msgpack.loads(f.read())
		self.assertTrue(obj == {'GET': 'world'})

	@need_msgpack
	def test_incr(self):
		"integer type"
		self.query('DEL/hello')
		f = self.query('INCR/hello.msg')
		obj = msgpack.loads(f.read())
		self.assertTrue(obj == {'INCR': 1})

	@need_msgpack
	def test_list(self):
		"list type"
		self.query('DEL/hello')
		self.query('RPUSH/hello/abc')
		self.query('RPUSH/hello/def')
		f = self.query('LRANGE/hello/0/-1.msg')
		obj = msgpack.loads(f.read())
		self.assertTrue(obj == {'LRANGE': ('abc', 'def')})

	@need_msgpack
	def test_error(self):
		"error return type"
		f = self.query('UNKNOWN/COMMAND.msg')
		obj = msgpack.loads(f.read())
		self.assertTrue('UNKNOWN' in obj)
		self.assertTrue(isinstance(obj, dict))
		self.assertTrue(isinstance(obj['UNKNOWN'], tuple))
		self.assertTrue(obj['UNKNOWN'][0] == False)
		self.assertTrue(isinstance(obj['UNKNOWN'][1], str))

class TestETag(TestWebdis):

	def test_etag_match(self):
		self.query('SET/hello/world')
		h = hashlib.md5("world".encode()).hexdigest()	# match Etag
		try:
			f = self.query('GET/hello.txt', None, {'If-None-Match': '"'+ h +'"'})
		except urllib.error.HTTPError as e:
			self.assertTrue(e.code == 304)
			return
		self.assertTrue(False) # we should have received a 304.

	def test_etag_fail(self):
		self.query('SET/hello/world')
		h = hashlib.md5("nonsense".encode()).hexdigest()	# non-matching Etag
		f = self.query('GET/hello.txt', None, {'If-None-Match': '"'+ h +'"'})
		self.assertTrue(f.read() == b'world')

class TestDbSwitch(TestWebdis):
	def test_db(self):
		"Test database change"
		self.query('0/SET/key/val0')
		self.query('1/SET/key/val1')
		f = self.query('0/GET/key.txt')
		self.assertTrue(f.read() == b"val0")
		f = self.query('1/GET/key.txt')
		self.assertTrue(f.read() == b"val1")
		f = self.query('GET/key.txt')
		self.assertTrue(f.read() == b"val0")

if __name__ == '__main__':
	unittest.main()
