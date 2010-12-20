About
-----

A very simple prototype providing an HTTP interface to Redis.

<pre>
make clean all
./turnip &
curl http://127.0.0.1:7379/SET/hello/world
curl http://127.0.0.1:7379/GET/hello

→ “world”
</pre>
