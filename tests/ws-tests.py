#!/usr/bin/env python3

import abc
import json
import os
import unittest
import uuid
from websocket import create_connection
import socket
import base64
import hashlib
import time

host = os.getenv('WEBDIS_HOST', '127.0.0.1')
port = int(os.getenv('WEBDIS_PORT', 7379))


def connect(format):
    return create_connection(f'ws://{host}:{port}/.{format}')


class TestWebdis(unittest.TestCase):
    def setUp(self) -> None:
        self.ws = connect(self.format())

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
        return response  # we'll just assert using the raw protocol

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


class TestPubSub(unittest.TestCase):
    def setUp(self):
        self.publisher = connect('json')
        self.subscriber = connect('json')

    def tearDown(self):
        self.publisher.close()
        self.subscriber.close()

    def serialize(self, cmd, *args):
        return json.dumps([cmd] + list(args))

    def deserialize(self, response):
        return json.loads(response)

    def test_publish_subscribe(self):
        channel_count = 2
        message_count_per_channel = 8
        channels = list(str(uuid.uuid4()) for i in range(channel_count))

        # subscribe to all channels
        sub_count = 0
        for channel in channels:
            self.subscriber.send(self.serialize('SUBSCRIBE', channel))
            sub_response = self.deserialize(self.subscriber.recv())
            sub_count += 1
            self.assertEqual(sub_response, {'SUBSCRIBE': ['subscribe', channel, sub_count]})

        # send messages to all channels
        prefix = 'message-'
        for i in range(message_count_per_channel):
            for channel in channels:
                message = f'{prefix}{i}'
                self.publisher.send(self.serialize('PUBLISH', channel, message))
                self.deserialize(self.publisher.recv())

        received_per_channel = dict((channel, []) for channel in channels)
        for j in range(channel_count * message_count_per_channel):
            received = self.deserialize(self.subscriber.recv())
            # expected: {'SUBSCRIBE': ['message', $channel, $message]}
            self.assertTrue(received, 'SUBSCRIBE' in received)
            sub_contents = received['SUBSCRIBE']
            self.assertEqual(len(sub_contents), 3)

            self.assertEqual(sub_contents[0], 'message')  # first element is the message type, here a push
            channel = sub_contents[1]
            self.assertTrue(channel in channels)  # second is the channel
            received_per_channel[channel].append(
                sub_contents[2])  # third, add to list of messages received for this channel

        # unsubscribe from all channels
        subs_remaining = channel_count
        for channel in channels:
            self.subscriber.send(self.serialize('UNSUBSCRIBE', channel))
            subs_remaining -= 1
            unsub_response = self.deserialize(self.subscriber.recv())
            self.assertEqual(unsub_response, {'UNSUBSCRIBE': ['unsubscribe', channel, subs_remaining]})

        # check that we received all messages
        for channel in channels:
            self.assertEqual(len(received_per_channel[channel]), message_count_per_channel)

        # check that we received them *in order*
        for i in range(message_count_per_channel):
            for channel in channels:
                expected = f'{prefix}{i}'
                self.assertEqual(received_per_channel[channel][i], expected,
                                 f'In {channel}: expected at offset {i} was "{expected}", actual was: "{received_per_channel[channel][i]}"')


class TestFrameSizes(TestWebdis):
    def format(self):
        return 'json'

    def serialize(self, cmd, *args):
        return json.dumps([cmd] + list(args))

    def deserialize(self, response):
        return json.loads(response)

    def test_length_126(self):
        self.validate_set_get('A' * 1024)  # this will require 2 bytes to encode the length

    def test_length_127(self):
        self.validate_set_get('A' * (2 ** 18))   # this will require more than 2 bytes to encode the length (actually using 8)

    def validate_set_get(self, value):
        key = str(uuid.uuid4())
        self.assertEqual(self.exec('SET', key, value), {'SET': [True, 'OK']})
        self.assertEqual(self.exec('GET', key), {'GET': value})
        self.exec('DEL', key)


class TestConnectDisconnect(TestWebdis):
    """Test for issue #209. A client connects, receives their handshake, disconnects"""

    def test_connect_handshake_disconnect(self):
        # Connect to Webdis
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            global host, port
            sock.connect((host, port))

            # Build WS handshake request
            raw_key = os.urandom(16)
            b64_key = base64.b64encode(raw_key)
            magic = b'258EAFA5-E914-47DA-95CA-C5AB0DC85B11'
            expected_raw = b64_key + magic
            hash = hashlib.sha1(expected_raw).digest()
            b64_hash = base64.b64encode(hash)

            request = ("GET /.json HTTP/1.1\r\n" + \
                "Host: %s:%d\r\n" + \
                "Connection: Upgrade\r\n" + \
                "Upgrade: WebSocket\r\n" + \
                "Origin: http://%s:%d\r\n" + \
                "Sec-WebSocket-Key: %s\r\n" + \
                "\r\n") % (host, port, host, port, b64_key.decode('utf-8'))

            # Send handshake and validate response
            sock.send(request.encode('utf-8'))
            response = sock.recv(1024)
            lines = response.decode('utf-8').split('\r\n')
            self.assertEqual(lines[0], 'HTTP/1.1 101 Switching Protocols')
            self.assertTrue('Sec-WebSocket-Accept: %s' % b64_hash.decode('utf-8') in lines)

            # send FIN frame. Format:
            # 4 bits flags (0x8 = Fin: true)
            # 4 bits opcode (0x8 = close)
            # 1 bit mask, 7 bits payload length -- here, 2
            fin_header = bytes([0b10001000, 0b10000010]) # 0x88, 0x82
            fin_payload = os.urandom(2)
            mask = os.urandom(4)
            fin_frame = fin_header + mask + bytes(fin_payload[0] ^ mask[0]) + bytes(fin_payload[1] ^ mask[1])
            sock.send(fin_frame)

        time.sleep(0.5)

        # now that we've disconnected, make sure Webdis is still alive
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.connect((host, port))


if __name__ == '__main__':
    unittest.main()
