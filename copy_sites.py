"""Locate full-pattern copy paths in the exact stock OS used for this build."""
import json
from pathlib import Path


def prepare(sdk, image_path):
    from tooling.formats import ldr, mapped_offset, require, sha
    from tooling.firmware import absolute_call

    stock = Path(image_path).read_bytes()
    profile = json.loads((sdk / 'profiles/opxy-1.2.1.json').read_text())
    require(sha(stock) == profile['stock_sha256'], 'Expected stock OP-XY OS 1.2.1')
    blocks = ldr(stock)
    memcpy = absolute_call(0x040201c4)
    sites = []
    # Only the project editor: clipboard, paste, duplicate and add pattern.
    # Each redirected call always performs the original copy. The adapter
    # mirrors settings only for an exact pattern length and a known endpoint.
    for first, last in ((0x8d5819cc, 0x8d581e00), (0x8d587b3c, 0x8d587e00)):
        start = mapped_offset(blocks, first, last-first)
        for offset in range(0, last-first-7, 2):
            if stock[start+offset:start+offset+8] == memcpy:
                sites.append(dict(kind='hook', address=hex(first+offset), file_offset=hex(start+offset),
                                  length=8, before=memcpy.hex(), symbol='m3_copy', call=True))
    addresses = [int(s['address'], 16) for s in sites]
    require(0x8d581a5e in addresses, 'Pattern clipboard copy differs from the SDK audit')
    require(any(0x8d581a74 <= a < 0x8d581d48 for a in addresses), 'Pattern paste copy not identified')
    (sdk / 'm3-copy-sites.json').write_text(json.dumps(sites))
