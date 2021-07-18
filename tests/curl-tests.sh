#!/bin/bash

# exit on first error
set -e

# GitHub issue #194 (connection: close + HTTP 100)
function validate_connection_close_100() {
    key=$(cat /dev/urandom | LC_ALL=C tr -dc 'a-zA-Z0-9' | fold -w 32 | head -n 1)
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

validate_connection_close_100
