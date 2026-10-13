#!/bin/sh
# Test-only PKI for agents_client_test.dart (never used outside tests): a CA,
# a server certificate for 127.0.0.1 and a device certificate signed by it,
# and a device certificate from another CA. Committed so the tests don't
# depend on the openssl a machine happens to have. 800 days: macOS refuses
# TLS server certificates valid for more than 825 days, even from a private
# CA. Regenerate (OpenSSL 3) when the tests say they expired:
#   sh test/fixtures/pki/generate.sh
set -eu
cd "$(dirname "$0")"
days=${DAYS:-800}
ca() {
	openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days $days \
		-subj "/CN=test $1" -keyout "$1.key" -out "$1.pem" \
		-addext basicConstraints=critical,CA:TRUE -addext keyUsage=critical,keyCertSign
}
leaf() {
	printf '%b' "$3" >"$1.ext"
	openssl req -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -subj "/CN=test $1" \
		-keyout "$1.key" -out "$1.csr"
	openssl x509 -req -in "$1.csr" -CA "$2.pem" -CAkey "$2.key" -CAcreateserial -days $days \
		-out "$1.pem" -extfile "$1.ext"
	rm -f "$1.csr" "$1.ext"
}
ca ca
ca other
leaf server ca 'subjectAltName=IP:127.0.0.1\nextendedKeyUsage=serverAuth\n'
leaf panel ca 'extendedKeyUsage=clientAuth\n'
leaf stranger other 'extendedKeyUsage=clientAuth\n'
rm -f ./*.srl
