#!/bin/bash

# exit on first error
set -e

# GitHub issue #194 (connection: close + HTTP 100)
function test_large_put_upload() {
    echo 'Testing large PUT upload: generating the key...'
    if [[ $(command -v uuidgen) ]]; then # macOS
        key=$(uuidgen)
    else
        key=$(cat /dev/urandom | LC_ALL=C tr -dc 'a-zA-Z0-9' | fold -w 32 | head -n 1)
    fi
    echo -n 'Sending a PUT request with a large payload... '
    put_output=$(printf 'A%.0s' $(seq 1 10000) | curl -s -H 'Connection: close' -XPUT "http://127.0.0.1:7379/SET/${key}" -d @-)
    if [[ ${PIPESTATUS[1]} -ne 0 || "${put_output}" != '{"SET":[true,"OK"]}' ]]; then
        echo "failed! Response was: ${put_output}"
        exit 1
    else
        echo 'OK'
    fi

    echo -n 'Verifying the upload... '
    strlen_output=$(curl -s "http://127.0.0.1:7379/STRLEN/${key}.txt")
    if [[ $strlen_output != '10000' ]]; then
        echo "failed! Unexpected value for STRLEN: ${strlen_output}"
        exit 1;
    else
        echo 'OK'
    fi
}

# GitHub issue #217 (empty header ":" returned for OPTIONS)
function test_options_headers() {
    echo -n 'Sending an OPTIONS request... '
    empty_header_present=$(curl -v -X OPTIONS "http://127.0.0.1:7379/" 2>&1 | grep -cE '^< : ' || true) # || true to avoid false-positive exit code from grep

    if [[ $empty_header_present != 0 ]]; then
        echo "failed! Found an empty header entry"
        exit 1
    else
        echo 'OK'
    fi
}

test_large_put_upload
test_options_headers
