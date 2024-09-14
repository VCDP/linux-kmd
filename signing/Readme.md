#Command for generate key
```sh
    openssl req -x509 -new -nodes -utf8 -sha256 -days 36500 \
    -batch -config configuration_file.config -outform DER \
    -out my_signing_key_pub.der \
    -keyout my_signing_key.priv
```