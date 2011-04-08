#!/usr/bin/python
import urllib2, unittest, json
from functools import wraps
try:
	import bson
except:
	bson = None


host = '127.0.0.1'
port = 7379

class TestWebdis(unittest.TestCase):

	def wrap(self,url):
		return 'http://%s:%d/%s' % (host, port, url)

	def query(self, url):
		r = urllib2.Request(self.wrap(url))
		return urllib2.urlopen(r)

class TestBasics(TestWebdis):

	def test_crossdomain(self):
		f = self.query('crossdomain.xml')
		self.assertTrue(f.headers.getheader('Content-Type') == 'application/xml')
		self.assertTrue("allow-access-from domain" in f.read())

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
		self.assertTrue(f.headers.getheader('Content-Type') == 'application/json')
		self.assertTrue(f.headers.getheader('ETag') == '"0db1124cf79ffeb80aff6d199d5822f8"')
		self.assertTrue(f.read() == '{"SET":[true,"OK"]}')

	def test_get(self):
		"string type"
		self.query('SET/hello/world')
		f = self.query('GET/hello')
		self.assertTrue(f.headers.getheader('Content-Type') == 'application/json')
		self.assertTrue(f.headers.getheader('ETag') == '"8cf38afc245b7a6a88696566483d1390"')
		self.assertTrue(f.read() == '{"GET":"world"}')

	def test_incr(self):
		"integer type"
		self.query('DEL/hello')
		f = self.query('INCR/hello')
		self.assertTrue(f.headers.getheader('Content-Type') == 'application/json')
		self.assertTrue(f.headers.getheader('ETag') == '"500e9bcdcbb1e98f25c1fbb880a96c99"')
		self.assertTrue(f.read() == '{"INCR":1}')

	def test_list(self):
		"list type"
		self.query('DEL/hello')
		self.query('RPUSH/hello/abc')
		self.query('RPUSH/hello/def')
		f = self.query('LRANGE/hello/0/-1')
		self.assertTrue(f.headers.getheader('Content-Type') == 'application/json')
		self.assertTrue(f.headers.getheader('ETag') == '"622e51f547a480bef7cf5452fb7782db"')
		self.assertTrue(f.read() == '{"LRANGE":["abc","def"]}')

	def test_error(self):
		"error return type"
		f = self.query('UNKNOWN/COMMAND')
		self.assertTrue(f.headers.getheader('Content-Type') == 'application/json')
		try:
			obj = json.loads(f.read())
		except:
			self.assertTrue(False)
			return

		self.assertTrue(len(obj) == 1)
		self.assertTrue('UNKNOWN' in obj)
		self.assertTrue(isinstance(obj['UNKNOWN'], list))
		self.assertTrue(obj['UNKNOWN'][0] == False)
		self.assertTrue(isinstance(obj['UNKNOWN'][1], unicode))

class TestRaw(TestWebdis):

	def test_set(self):
		"success type (+OK)"
		self.query('DEL/hello')
		f = self.query('SET/hello/world.raw')
		self.assertTrue(f.headers.getheader('Content-Type') == 'binary/octet-stream')
		self.assertTrue(f.read() == "+OK\r\n")

	def test_get(self):
		"string type"
		self.query('SET/hello/world')
		f = self.query('GET/hello.raw')
		self.assertTrue(f.read() == '$5\r\nworld\r\n')

	def test_incr(self):
		"integer type"
		self.query('DEL/hello')
		f = self.query('INCR/hello.raw')
		self.assertTrue(f.read() == ':1\r\n')

	def test_list(self):
		"list type"
		self.query('DEL/hello')
		self.query('RPUSH/hello/abc')
		self.query('RPUSH/hello/def')
		f = self.query('LRANGE/hello/0/-1.raw')
		self.assertTrue(f.read() == "*2\r\n$3\r\nabc\r\n$3\r\ndef\r\n")

	def test_error(self):
		"error return type"
		f = self.query('UNKNOWN/COMMAND.raw')
		self.assertTrue(f.read().startswith("-ERR "))

def need_bson(fn):
	def wrapper(self):
		if bson:
			fn(self)
	return wrapper

class TestBSon(TestWebdis):

	@need_bson
	def test_set(self):
		"success type (+OK)"
		self.query('DEL/hello')
		f = self.query('SET/hello/world.bson')
		self.assertTrue(f.headers.getheader('Content-Type') == 'application/bson')
		obj = bson.decode_all(f.read())
		self.assertTrue(obj == [{u'SET': [True, bson.Binary('OK', 0)]}])

	@need_bson
	def test_get(self):
		"string type"
		self.query('SET/hello/world')
		f = self.query('GET/hello.bson')
		obj = bson.decode_all(f.read())
		self.assertTrue(obj == [{u'GET': bson.Binary('world', 0)}])

	@need_bson
	def test_incr(self):
		"integer type"
		self.query('DEL/hello')
		f = self.query('INCR/hello.bson')
		obj = bson.decode_all(f.read())
		self.assertTrue(obj == [{u'INCR': 1L}])

	@need_bson
	def test_list(self):
		"list type"
		self.query('DEL/hello')
		self.query('RPUSH/hello/abc')
		self.query('RPUSH/hello/def')
		f = self.query('LRANGE/hello/0/-1.bson')
		obj = bson.decode_all(f.read())
		self.assertTrue(obj == [{u'LRANGE': [bson.Binary('abc', 0), bson.Binary('def', 0)]}])

	@need_bson
	def test_error(self):
		"error return type"
		f = self.query('UNKNOWN/COMMAND.bson')
		obj = bson.decode_all(f.read())
		self.assertTrue(len(obj) == 1)
		self.assertTrue(u'UNKNOWN' in obj[0])
		self.assertTrue(isinstance(obj[0], dict))
		self.assertTrue(isinstance(obj[0][u'UNKNOWN'], list))
		self.assertTrue(obj[0]['UNKNOWN'][0] == False)
		self.assertTrue(isinstance(obj[0]['UNKNOWN'][1], bson.Binary))

if __name__ == '__main__':
	unittest.main()
