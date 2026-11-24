"""Verify the resonator catalog, adapter and optional stock OS preimage."""
import argparse
import json
from pathlib import Path
import struct
import sys
import tempfile

here = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('sdk', type=Path)
parser.add_argument('--stock-os', type=Path)
args = parser.parse_args()
sdk = args.sdk.resolve()
sys.path.insert(0, str(sdk))
from tooling import adapters, machines, native
from tooling.firmware import decode_branch
from tooling.formats import ldr, mapped_offset, require, sha

layout = machines.read_layout(here/'probe-layout.json', build=True)
entries = machines.read_collection(extra=[here/'m3probe'])
profile, items, definitions = native.configuration(entries, layout=layout)
native.check_sources(items)
print('PASS: machine sources satisfy the pinned SDK contributor checks.')
variants = [('trine', 11)]
require(definitions['m3probe_fx_id'] == 11 and
        all(number in native.stock_tables.free_effects() and number < native.stock_tables.EFFECT_SENTINEL
            for _, number in variants), 'Unexpected instrument filter IDs')

hook = next(h for h in profile['patches'] if h.get('symbol') == 'm3probe_filter_init')
at = int(hook['address'], 16)
require(hook['symbol'] == 'm3probe_filter_init' and at == 0x8d506c3e and hook['call'] and
        decode_branch(at, bytes.fromhex(hook['before'])) == (0x8d50673c, True),
        'Wrong hook or stock registrar')
stock_profile = json.loads((sdk/'profiles/opxy-1.2.1.json').read_text())
anchor = next(row for row in stock_profile['patches'] if row['address'] == '0x8d506c74')
require(int(hook['file_offset'], 16) - at == int(anchor['file_offset'], 16) - int(anchor['address'], 16),
        'Hook file offset differs from adjacent platform records')
if args.stock_os:
    stock = args.stock_os.read_bytes()
    require(sha(stock) == profile['stock_sha256'], 'Stock OS 1.2.1 hash mismatch')
    offset = mapped_offset(ldr(stock), at, hook['length'])
    require(offset == int(hook['file_offset'], 16) and
            stock[offset:offset + hook['length']] == bytes.fromhex(hook['before']),
            'Instrument-filter stock call does not match the hook preimage')
    image = ldr(stock)
    def load(address, count):
        start = mapped_offset(image, address, count)
        return stock[start:start + count]
    expected_methods = (
        0x8d51c0c2, 0x8d51c0d8, 0x8d51c24a, 0x8d51c2b2, 0x8d5006a4,
        0x8d51c042, 0x8d51bc20, 0x8d500684, 0, 0,
    )
    require(load(0x8405307c, 40) == struct.pack('<10I', *expected_methods),
            'Stock SVF method table differs from the audit')
    require(load(0x8d51c07c, 8).hex() == '01dac6849c230000' and
            load(0x8d51c092, 8).hex() == '01da05847c300000',
            'Stock SVF allocator differs from the audit')
    require(load(0x8400e380, 12) == struct.pack('<3I', *([0x8c0b3314] * 3)),
            'Stock K2–K4 click dispatch differs from the Setup audit')
    require(load(0x11a0982e, 8) == bytes.fromhex('03da7f00ffff0000') and
            load(0x11a09836, 4) == bytes.fromhex('04c41326'),
            'Stock filter key argument differs from the SVF audit')
    print('PASS: verified the stock filter hook, SVF allocator and callback table.')
source = (sdk/'profiles/opxy-1.2.1/content/op1_factory.json').read_bytes()
assets = native.prepare_assets(source, profile, items)
menu = json.loads(assets['content/op1_factory.json'])
before = json.loads(source)
defaults = adapters.catalog_defaults(items[0].definition)
expected = [{'type':name, 'id':number, 'default_params':defaults}
            for name, number in variants]
require(menu == dict(before, filter_types=before['filter_types'] + expected), 'Wrong menu delta')
with tempfile.TemporaryDirectory() as tmp:
    unit = adapters.generate_registration(tmp, items[0]).read_text()
    carrier = adapters.generate_fx_object(tmp, items[0]).read_text()
    require('sdk_register_effect(&record)' in unit and '&TrackInsert::construct' in unit and
            'kInstrumentFilterPage = 3' in unit and 'return 0x7fffffff;' in carrier and
            items[0].dry_parameter is None and items[0].dry_level == 0x7fffffff,
            'Missing stereo track insert or unity dry level')
    require('"trine"' in unit and 'm3_resonator::perform' in unit and
            'm3_setup_ui.hpp' in unit and 'm3probe_filter_init' in unit, 'Missing M3 adapter')
    require(all('content/display/' + asset in assets for asset in ('m3_main.svg','m3_setup.svg')),
            'Missing Main or Alt artwork')
    from tooling.firmware import hook_instruction, hook_target
    for h in profile['patches']:
        if not h.get('symbol','').startswith(('m3_', 'm3probe_')):
            continue
        target = profile['module_base'] + 0x8000
        require(hook_target(h, hook_instruction(h, target)) == (target,h['call']), 'Unreachable M3 hook')
        if args.stock_os:
            require(load(int(h['address'],16),h['length']) == bytes.fromhex(h['before']) and
                    mapped_offset(image,int(h['address'],16),h['length']) == int(h['file_offset'],16),
                    'M3 hook differs from stock at '+h['address'])
print('PASS: one Trine entry, Setup screen, native hook footprints and DSP adapter.')
