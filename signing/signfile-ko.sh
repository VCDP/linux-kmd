#!/bin/bash

# signfile-ko.sh
# Usage: ./signfile-ko.sh <module_path>

MODULE_PATH=$1
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

SIGNING_SCRIPT="${SCRIPT_DIR}/SignFile"
CERTIFICATE="$SCRIPT_DIR/Certificates/Media Driver Production Sign TAR 2024Q3.cer"
KERNEL_SIGNING_SCRIPT="/usr/src/kernels/5.14.0-427.13.1.el9_4.x86_64/scripts/sign-file"

base_name=$(basename "$MODULE_PATH")
sig_file="${MODULE_PATH}.sig"

module_dir=$(dirname "$MODULE_PATH")
signed_dir="$module_dir/signed"
mkdir -p "$signed_dir"

signed_file="$signed_dir/$base_name"

# Sign the module
$SIGNING_SCRIPT -u 'sys_media_ci' -p 'qwertyuiop[]\1234021' -c 'Media Driver Production Sign TAR 2024Q3' -vv -s cl -cf "$sig_file" "$MODULE_PATH"

# Verify the signature
openssl pkcs7 -print_certs -inform der -in "$sig_file" > certs.pem
openssl smime -verify -in "$sig_file" -inform der -content "$MODULE_PATH" -noverify 'certs.pem' > /dev/null

# Convert the certificate
openssl x509 -inform pem -in "$CERTIFICATE" -outform der -out cert.der

# Sign the file
$KERNEL_SIGNING_SCRIPT -s ${sig_file} sha256 cert.der "$MODULE_PATH" "$signed_file"

# Get the signature key
modinfo -F sig_key "$signed_file"

# Generate md5sum
echo "signed_file = $signed_file"
md5sum "$signed_file" > "$signed_file.md5"
