#!/bin/bash

# exit on first error
set -e

function send_delete() {
    curl -s "http://127.0.0.1:7379/DEL/$1" > /dev/null
}

function fail_with_error() {
    local message=$1
    local key=$2
    >&2 echo "${message}"
    send_delete "${key}"
    exit 1
}

# GitHub issue #194 (connection: close + HTTP 100)
function test_large_put_upload() {
    echo -n 'Test 1/2: Large PUT upload: generating the key... '
    if [[ $(command -v uuidgen) ]]; then # macOS
        key=$(uuidgen)
    elif [[ $(command -v uuid) ]]; then # Ubuntu
        key=$(uuid)
    else
        >&2 echo 'failed: neither uuidgen or uuid was available'
        exit 1
    fi
    echo 'OK'

    send_delete "${key}" # initial cleanup

    echo -n 'Sending a PUT request with a large payload... '
    put_output=$(printf 'A%.0s' $(seq 1 10000) | curl -s -H 'Connection: close' -XPUT "http://127.0.0.1:7379/SET/${key}" -d @-)
    if [[ ${PIPESTATUS[1]} -ne 0 || "${put_output}" != '{"SET":[true,"OK"]}' ]]; then
        fail_with_error "failed! Response was: ${put_output}" "${key}"
    else
        echo 'OK'
    fi

    echo -n 'Verifying the upload... '
    strlen_output=$(curl -s "http://127.0.0.1:7379/STRLEN/${key}.txt")
    if [[ $strlen_output != '10000' ]]; then
        fail_with_error "failed! Unexpected value for STRLEN: ${strlen_output}" "${key}"
    else
        echo 'OK'
    fi

    send_delete "${key}" # cleanup
}

# GitHub issue #217 (empty header ":" returned for OPTIONS)
function test_options_headers() {
    echo -n 'Test 2/2: Sending an OPTIONS request... '
    empty_header_present=$(curl -v -X OPTIONS "http://127.0.0.1:7379/" 2>&1 | grep -cE '^< : ' || true) # || true to avoid false-positive exit code from grep

    if [[ $empty_header_present != 0 ]]; then
        >&2 echo "failed! Found an empty header entry"
        exit 1
    else
        echo 'OK'
    fi
}

test_large_put_upload
echo
test_options_headers
