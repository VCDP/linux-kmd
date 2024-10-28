
the old `Signfile` binary is from https://gitlab.devtools.intel.com/EDSS/signfilereleases/tags

the new one is from https://github.com/intel-innersource/applications.security.edss.docs.signfile-releases/releases
you may need to apply for permission on [AGS](https://ags.intel.com/identityiq/home.jsf)



## Enterprise Digital Signing Service
### GPG
page: https://edss.intel.com/key/c100fa0c-c7d0-4509-8d09-ef4c99b9b702
Name: Media_Driver_Prod_PGP_2022Q3
Key Signing Limit: 200000

"mss-linux-media.public":
Options --> Display public Key Details --> Download Certificate
rename "Media_Driver_Prod_PGP_2022Q3.pem" to "mss-linux-media.public"
run `sed -i -e '$a\' mss-linux-media.public` to avoid "key 1 not an armored public key"

### Production key for tar
page: https://edss.intel.com/signoperation/d1665a4e-64b7-487d-9c71-008237e21ddf
Name: Media Driver Production Sign 2022Q2
Signing Limit: 100000
Expiration Date: 2023-05-23T23:59:59.0000000Z


## sign rpm file
### import public key
```sh
gpg --quiet --with-fingerprint mss-linux-media.public
rpm --import mss-linux-media.public
```

### sign
```sh
## ./SignFile -vv -u ${U} -p ${P} xxx.rpm
./SignFile -vv xxx.rpm
```

### verify
```sh
rpm -Kv xxx.rpm | grep 'Signature'
```


## sign tar file
```sh
./SignFile -vv -c 'Media Driver Production Sign 2022Q2' -s cl -cf XXX.tar.gz.sig XXX.tar.gz
```

### verify
```sh
openssl pkcs7 -print_certs -inform der -in XXX.tar.gz.sig > certs.pem
## openssl x509 -in certs.pem -serial -noout
openssl smime -verify -in XXX.tar.gz.sig -inform der -content XXX.tar.gz -noverify certs.pem > /dev/null
```

Output: **Verification successful**

## reference documents
https://wiki.ith.intel.com/pages/viewpage.action?pageId=1879024604



