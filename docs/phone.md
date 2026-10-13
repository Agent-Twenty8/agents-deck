# Phone app (design note, not built yet)

A small phone app, built from the same Flutter package as the desk panel and the menu bar app,
paired with the deck like any other network device (see [protocol.md](protocol.md#devices-and-security)).
Until it exists, the dashboard is on the Mac only (`127.0.0.1`).

## The chicken-and-egg problem

The phone has to trust the deck, and the deck has to trust the phone, before either has talked to
the other. Baking a certificate into the app doesn't solve it: every install of the app would carry the
same certificate and key, anyone can pull them out of the app package, and so they would prove only
that someone has the app, not that it's *your* phone. A shared secret in a published app is not a
secret.

What the app can carry is *code*, not identity. The identity is made on the phone, and the trust is
carried by something you can see: a QR code on the Mac's screen.

## Pairing

1. On the Mac: `agentctl pair phone --qr` shows a QR code with three things: the listener's address,
   the deck's CA fingerprint, and a one-time pairing code (2 minutes, single use).
2. The app scans it and generates its own key pair in the phone's secure hardware (Secure Enclave on
   iOS, StrongBox/Keystore on Android), marked non-exportable.
3. It connects to the address, checks that the server's certificate chains to a CA with exactly the
   scanned fingerprint (so a look-alike server on the Wi-Fi is refused), and sends
   `POST /v1/pair {"code", "csr"}`.
4. The deck checks the code, signs the request and records the phone in `devices.json` with its scope.
   From then on the phone connects with that certificate, like the panel.

The QR code is the out-of-band channel: whoever can see your Mac's screen within those two minutes can
pair. The phone's private key never leaves its secure hardware, so a stolen backup or a copied app
can't impersonate it, and `agentctl devices revoke phone` cuts it off.

## What signing the app adds

Code-signing (App Store, TestFlight, Play) proves the app wasn't tampered with. It can be checked by
the deck too (Apple App Attest, Play Integrity), which would let the deck refuse look-alike apps. That
is a later, optional layer on top of pairing, not a replacement for it.

## Later

- Reaching the deck away from home: over a VPN (Tailscale or WireGuard) to the same listener, never
  by opening `:7341` to the internet.
- `agentctl pair` already implements the server side (`POST /v1/pair` with a CSR); the `--qr` flag and
  the app are what's missing.
