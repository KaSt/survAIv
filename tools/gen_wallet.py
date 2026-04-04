#!/usr/bin/env python3
"""Generate an Ethereum/Polygon wallet compatible with x402 and Polymarket.

Usage:
    python3 tools/gen_wallet.py              # generate and print
    python3 tools/gen_wallet.py --json       # output as JSON
    python3 tools/gen_wallet.py --nvs out.csv  # generate NVS CSV for esptool

No dependencies required beyond Python 3.6+ standard library.
For address derivation: pip install eth-keys (optional — falls back to ecdsa).
"""

import argparse
import hashlib
import os
import sys
import json


def keccak256(data: bytes) -> bytes:
    """Keccak-256 (NOT SHA3-256). Uses hashlib if available, else pysha3."""
    try:
        return hashlib.new("keccak_256", data).digest()
    except ValueError:
        try:
            import sha3
            k = sha3.keccak_256()
            k.update(data)
            return k.digest()
        except ImportError:
            print("ERROR: Need Python 3.11+ or `pip install pysha3` for keccak256", file=sys.stderr)
            sys.exit(1)


def generate_wallet():
    """Generate a new Ethereum wallet using OS randomness."""
    private_key = os.urandom(32)
    pk_hex = private_key.hex()

    # Try eth_keys for proper address derivation.
    try:
        from eth_keys import keys
        pk = keys.PrivateKey(private_key)
        address = pk.public_key.to_checksum_address()
        return pk_hex, address
    except ImportError:
        pass

    # Fallback: manual secp256k1 with ecdsa library.
    try:
        from ecdsa import SigningKey, SECP256k1
        sk = SigningKey.from_string(private_key, curve=SECP256k1)
        vk = sk.get_verifying_key()
        pub_bytes = vk.to_string()  # 64 bytes (x || y)
        addr_hash = keccak256(pub_bytes)
        addr_hex = addr_hash[-20:].hex()
        # EIP-55 checksum encoding.
        addr_hash_hex = keccak256(addr_hex.encode()).hex()
        checksummed = "0x"
        for i, c in enumerate(addr_hex):
            checksummed += c.upper() if int(addr_hash_hex[i], 16) >= 8 else c
        return pk_hex, checksummed
    except ImportError:
        pass

    # Last resort.
    return pk_hex, "(install eth-keys or ecdsa to derive address)"


def main():
    parser = argparse.ArgumentParser(description="Generate an Ethereum/Polygon wallet")
    parser.add_argument("--json", action="store_true", help="Output as JSON")
    parser.add_argument("--nvs", metavar="FILE", help="Write NVS partition CSV for esptool")
    args = parser.parse_args()

    pk_hex, address = generate_wallet()

    if args.json:
        print(json.dumps({"private_key": pk_hex, "address": address}, indent=2))
    elif args.nvs:
        with open(args.nvs, "w") as f:
            f.write("key,type,encoding,value\n")
            f.write("survaiv,namespace,,\n")
            f.write(f"wallet_key,data,hex2bin,{pk_hex}\n")
        print(f"NVS CSV written to {args.nvs}")
        print(f"Flash with: python -m esptool --port PORT write_flash 0x9000 nvs.bin")
        print(f"  (generate nvs.bin with: python -m esp_idf_nvs_partition_gen generate {args.nvs} nvs.bin 0x6000)")
    else:
        print("╔═══════════════════════════════════════════════════╗")
        print("║  ⟁ SURVAIV — Wallet Generator                    ║")
        print("╚═══════════════════════════════════════════════════╝")
        print()
        print(f"  Address    : {address}")
        print(f"  Private Key: {pk_hex}")
        print()
        print("  ⚠  Save the private key securely — it cannot be recovered!")
        print()
        print("  Fund this address on Polygon with:")
        print("    • USDC.e (bridged USDC) for trading")
        print("    • A small amount of MATIC for gas (approvals)")
        print()
        print("  To provision on the board:")
        print(f"    ./flash.sh --wallet {pk_hex}")


if __name__ == "__main__":
    main()
