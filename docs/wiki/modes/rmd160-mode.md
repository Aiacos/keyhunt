# RMD160 Mode

RMD160 mode searches directly for RIPEMD160 hashes, bypassing Base58 address encoding.

## Overview

| Aspect | Details |
|--------|---------|
| Command | `-m rmd160` |
| Input | Raw RIPEMD160 hashes (40 hex characters) |
| Complexity | O(N) - linear with range size |
| Best for | When you have raw hash160, not Base58 address |
| Advantage | Slightly faster than address mode (no Base58) |

## How It Works

1. **Key Generation**: Generate private keys in the search range
2. **Point Multiplication**: Compute public key `P = k * G`
3. **Hash Pipeline**: `SHA256(pubkey)` -> `RIPEMD160(sha256_result)`
4. **Direct Comparison**: Match Hash160 against target (no Base58 encoding)

## Basic Usage

```bash
./keyhunt -m rmd160 -f hashes.txt -b 66 -t 16 -l compress
```

## Target File Format

Raw RIPEMD160 hashes (40 hex characters, 20 bytes):

```
751e76e8199196d454941c45d1b3a323f1433bd6
89abcdef0123456789abcdef0123456789abcdef
```

### Converting Address to RMD160

A Bitcoin address is Base58Check encoded. To get the raw hash:

```python
import base58

address = "1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH"
decoded = base58.b58decode_check(address)
# decoded[0] is version byte (0x00 for mainnet)
hash160 = decoded[1:].hex()
print(hash160)  # 751e76e8199196d454941c45d1b3a323f1433bd6
```

Or use online tools to decode Base58Check addresses.

## Key Parameters

### Range Specification

```bash
# Bit range
./keyhunt -m rmd160 -f hashes.txt -b 66

# Explicit range
./keyhunt -m rmd160 -f hashes.txt -r 20000000000000000:3FFFFFFFFFFFFFFFF
```

### Key Type

```bash
./keyhunt -m rmd160 -f hashes.txt -b 66 -l compress
./keyhunt -m rmd160 -f hashes.txt -b 66 -l uncompress
./keyhunt -m rmd160 -f hashes.txt -b 66 -l both
```

### Random Mode

```bash
./keyhunt -m rmd160 -f hashes.txt -b 66 -R
```

## Performance

RMD160 mode is marginally faster than ADDRESS mode because it skips:
- Base58Check encoding
- Version byte handling

In practice, the difference is small (~5%) since RIPEMD160 hashing dominates.

## Example Scenarios

### Search with Raw Hash

```bash
echo "751e76e8199196d454941c45d1b3a323f1433bd6" > hash.txt
./keyhunt -m rmd160 -f hash.txt -b 66 -t 16 -l compress
```

### Puzzle 66 (using hash instead of address)

```bash
./keyhunt -m rmd160 -f tests/66.rmd -b 66 -l compress -R -q
```

## Output

```
[+] Version 0.2.230731 Satoshi Quest
[+] Mode: RMD160
[+] Search: COMPRESSED
[+] Threads: 16
[+] Range: 20000000000000000 to 3FFFFFFFFFFFFFFFF
[+] Loaded 1 rmd160 hashes
[+] Speed: 88.2 Mkeys/s
```

### When Match Found

```
[+] Found RMD160 match!
    Hash160: 751e76e8199196d454941c45d1b3a323f1433bd6
    Private Key: 0x1
    Address: 1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH
```

## When to Use RMD160 vs ADDRESS

| Use RMD160 | Use ADDRESS |
|------------|-------------|
| Already have raw hash | Have Base58 address |
| Bulk processing many hashes | Normal usage |
| Integration with other tools | Human-readable output |

## Limitations

- Input must be exact 40-character hex
- No checksum validation (unlike Base58)
- Minimal speed advantage over ADDRESS mode

## See Also

- [Address Mode](address-mode.md) - Standard address search
- [BSGS Mode](bsgs-mode.md) - For known public keys
