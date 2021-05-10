#!/bin/bash

# exit on first error
set -e

# GitHub issue #194 (connection: close + HTTP 100)
printf 'A%.0s' $(seq 1 10000) | curl -v -H 'Connection: close' -XPUT 'http://127.0.0.1:7379/SET/toobig' -d @-
if [[ $(curl -v 'http://127.0.0.1:7379/STRLEN/toobig.txt') != '10000' ]]; then exit 1; fi
