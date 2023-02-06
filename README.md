
## Purpose

My reason for writing this utility is to support Full Disk Encryption
using a hardware key like the yubikey. Normally, the applications
doing that pull in a pretty tall stack of code - a USB library,
a smart card library, possibly requiring an access broker for card
readers, the actual card driver, plus a crypto library.

That is quite a lot, even if you're just considering a systemd-boot
based scenario where you can copy your boot time environment
to initrd.  However, the amount of code you depend on becomes
prohibitive if you think about adding this code to a boot loader
like grub.

Fortunately, the actual code required to make this work is much smaller.
It turns out that you can do it in 3167 LoC.

This is what utoken-decrypt is intended for.

## Theory of Operation

This implementation uses the PIV application of a yubikey to decrypt a secret that
was previously encrypted with a private key residing on the yubikey. I tested this
with a Yubikey 5.

## Initialize your yubikey

First, you need to create a public key. Use the following command:

	ykman piv generate-certificate 9a pubkey.pem -s "Secure Boot"

This creates a 2048 bit RSA key and extracts the public key portion and
writes it to the file ``pubkey.pem``.

## Encrypt your secret

For the purpose of demoing the approach, create a small file with some text in it,
and encrypt it with the public key:

	echo -n SECRET > cleartext
	openssl rsautl -in cleartext -inkey pubkey.pem -pubin -encrypt -out secret

## Use the Yubikey to Decrypt

To decrypt the secret using your yubikey:

	utoken-decrypt -T 1050 -p 123456 secret -o recovered

The -T option tells it to look for a USB device manufactured by yubico (USB vendor
ID 1050 - you could be more specific and look for vendor:product id).

The -p option provides the PIN. If you haven't changed it, the default is 123456.

The -o option tells it where to write the recovered secret to. If you omit this,
data will be written to stdout (note that all informational and debug messages
are written to stderr, so there should be no risk of these outputs getting mixed up).

## Things to be done

This code still needs a bit of love and clean-up. Plus packaging. And
then porting to make it usable in grub.
