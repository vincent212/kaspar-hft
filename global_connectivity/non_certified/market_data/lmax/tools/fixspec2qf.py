#!/usr/bin/env python3
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: mayeski@gmail.com | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.
"""Write a QuickFIX data dictionary from an LMAX fix-spec.json.

LMAX publishes each FIX API as a machine-readable specification at
https://docs.lmax.com/fix-api/<api>/fix/latest/fix-spec.json (the PDF next to it
was a placeholder when this was written, 2026-11-18). It does not publish a
QuickFIX dictionary. This script turns the JSON into one, deterministically:

  - one <field> per entry of the version's field list, with its allowed values;
    a field used in a message but missing from the list (LMAX's TestPing, 999)
    takes its type from the message;
  - one <message> per message of the version, in the JSON's field order;
  - components are flattened into the message or group that uses them (the
    JSON gives each component's fields in place);
  - a group's count field is the group; the group's fields follow in order;
  - the standard header and trailer, restricted to the fields LMAX lists.

  fixspec2qf.py SPEC.json [--version FIX.4.4] [-o OUT.xml]
  fixspec2qf.py SPEC.json --check     # parse the result with xml.etree, list counts
  fixspec2qf.py SPEC.json --markdown [--messages V,W,Y]   # message tables for feed.md

The dictionary is reviewed against the JSON, not edited by hand: fix the script
or LMAX's JSON and regenerate.
"""
import argparse
import json
import sys
import xml.etree.ElementTree as ET
from xml.sax.saxutils import quoteattr

TYPE_MAP = {
    'STRING': 'STRING', 'CHAR': 'CHAR', 'INT': 'INT', 'BOOLEAN': 'BOOLEAN',
    'PRICE': 'PRICE', 'QUANTITY': 'QTY', 'AMOUNT': 'AMT', 'LENGTH': 'LENGTH',
    'SEQUENCE_NUMBER': 'SEQNUM', 'GROUP_INDICATOR': 'NUMINGROUP',
    'UTC_TIMESTAMP': 'UTCTIMESTAMP', 'UTC_DATE': 'UTCDATEONLY', 'UTC_TIME': 'UTCTIMEONLY',
    'LOCAL_DATE': 'LOCALMKTDATE', 'MULTIPLE_STRING_VALUES': 'MULTIPLEVALUESTRING',
    'CURRENCY': 'CURRENCY', 'EXCHANGE': 'EXCHANGE',
}

# FIX 4.4 standard header and trailer, in order; only those LMAX lists are kept.
HEADER = [8, 9, 35, 49, 56, 115, 128, 90, 91, 50, 142, 57, 143, 116, 144, 129, 145,
          43, 97, 52, 122, 212, 213, 347, 369, 34]
REQUIRED_HEADER = {8, 9, 35, 49, 56, 34, 52}
TRAILER = [93, 89, 10]


def qf_type(t, tag):
    if t not in TYPE_MAP:
        raise SystemExit(f'tag {tag}: LMAX type {t} has no QuickFIX type in TYPE_MAP')
    return TYPE_MAP[t]


def build(spec, version):
    vs = [v for v in spec['versions'] if v['version'] == version]
    if not vs:
        raise SystemExit(f'{version} not in the spec (has: {[v["version"] for v in spec["versions"]]})')
    v = vs[0]
    fields = {f['tag']: f for f in v['fields']}

    # Fields used in messages but missing from the list: take them from the message.
    def collect(fs):
        for f in fs:
            if f['kind'] == 'field' and f['tag'] not in fields:
                fields[f['tag']] = {'tag': f['tag'], 'name': f['name'], 'type': f['type'],
                                    'allowedValues': f.get('allowedValues') or []}
            if f.get('fields'):
                collect(f['fields'])
    for m in v['messages']:
        collect(m['fields'])

    names = {}
    for t, f in fields.items():
        if f['name'] in names and names[f['name']] != t:
            raise SystemExit(f'field name {f["name"]} used by tags {names[f["name"]]} and {t}')
        names[f['name']] = t

    major, minor = v['major'], v['minor']
    out = [f'<fix major="{major}" minor="{minor}" servicepack="0" type="FIX">']

    def req(r):
        return 'Y' if r == 'REQUIRED' else 'N'

    def body(fs, ind, in_group=False):
        lines = []
        for f in fs:
            k = f['kind']
            if k == 'field':
                if in_group and f['type'] == 'GROUP_INDICATOR':
                    continue  # the group element is the count field
                lines.append(f'{ind}<field name="{fields[f["tag"]]["name"]}" required="{req(f["required"])}"/>')
            elif k == 'component':
                lines += body(f['fields'], ind, in_group)
            elif k == 'group':
                count = fields[f['tag']]['name']
                lines.append(f'{ind}<group name="{count}" required="{req(f["required"])}">')
                lines += body(f['fields'], ind + '  ', True)
                lines.append(f'{ind}</group>')
            else:
                raise SystemExit(f'unknown kind {k}')
        return lines

    out.append('  <header>')
    for t in HEADER:
        if t in fields:
            out.append(f'    <field name="{fields[t]["name"]}" required="{"Y" if t in REQUIRED_HEADER else "N"}"/>')
    out.append('  </header>')

    out.append('  <messages>')
    for m in v['messages']:
        cat = 'admin' if m['category'] == 'session' else 'app'
        out.append(f'    <message name="{m["name"]}" msgtype="{m["msgType"]}" msgcat="{cat}">')
        out += body(m['fields'], '      ')
        out.append('    </message>')
    out.append('  </messages>')

    out.append('  <trailer>')
    for t in TRAILER:
        if t in fields:
            out.append(f'    <field name="{fields[t]["name"]}" required="{"Y" if t == 10 else "N"}"/>')
    out.append('  </trailer>')

    out.append('  <components>')
    out.append('  </components>')

    out.append('  <fields>')
    for t in sorted(fields):
        f = fields[t]
        vals = f.get('allowedValues') or []
        # MsgType's values are message types, which QuickFIX takes from <messages>.
        if t == 35:
            vals = []
        qt = qf_type(f['type'], t)
        if not vals:
            out.append(f'    <field number="{t}" name="{f["name"]}" type="{qt}"/>')
            continue
        out.append(f'    <field number="{t}" name="{f["name"]}" type="{qt}">')
        for a in sorted(vals, key=lambda a: a['code']):
            desc = ''.join(c if c.isalnum() else '_' for c in a['name']).strip('_').upper() or 'V'
            out.append(f'      <value enum={quoteattr(a["code"])} description={quoteattr(desc)}/>')
        out.append('    </field>')
    out.append('  </fields>')
    out.append('</fix>')
    return '\n'.join(out) + '\n'


def markdown(spec, version, only):
    v = [x for x in spec['versions'] if x['version'] == version][0]
    fields = {f['tag']: f for f in v['fields']}
    out = []

    def rows(fs, depth):
        for f in fs:
            if f['kind'] == 'component':
                rows(f['fields'], depth)
                continue
            vals = f.get('allowedValues') or []
            if not vals and f.get('tag') in fields:
                vals = fields[f['tag']].get('allowedValues') or []
            vtxt = ', '.join(f"{a['code']}={a['name']}" for a in vals)
            desc = ' '.join((f.get('description') or '').split())
            if len(desc) > 160:
                desc = desc[:157] + '...'
            name = ('&nbsp;&nbsp;' * depth) + f['name'].split('_')[-1]
            req = 'Y' if f['required'] == 'REQUIRED' else 'N'
            out.append(f"| {f.get('tag')} | {name} | {req} | {f.get('type') or 'group'} | {vtxt} | {desc} |")
            if f['kind'] == 'group':
                rows([x for x in f['fields'] if x.get('type') != 'GROUP_INDICATOR'], depth + 1)

    for m in v['messages']:
        if only and m['msgType'] not in only:
            continue
        out.append(f"### {m['name']} (35={m['msgType']}, {m['direction']})\n")
        out.append(' '.join((m.get('description') or '').split()) + '\n')
        out.append('| Tag | Field | Req | Type | Values | LMAX description |')
        out.append('|---|---|---|---|---|---|')
        rows(m['fields'], 0)
        out.append('')
    return '\n'.join(out) + '\n'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('spec')
    ap.add_argument('--version', default='FIX.4.4')
    ap.add_argument('-o', '--output')
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--markdown', action='store_true')
    ap.add_argument('--messages', default='')
    a = ap.parse_args()
    spec = json.load(open(a.spec))
    if a.markdown:
        sys.stdout.write(markdown(spec, a.version, set(x for x in a.messages.split(',') if x)))
        return
    xml = build(spec, a.version)
    root = ET.fromstring(xml)
    if a.check:
        msgs = root.find('messages')
        print(f'{spec["title"]} {a.version}: {len(root.find("fields"))} fields, '
              f'{len(msgs)} messages: ' + ', '.join(f'{m.get("name")}({m.get("msgtype")})' for m in msgs))
        return
    header = (f'<!-- Generated by fixspec2qf.py from LMAX "{spec["title"]}" fix-spec.json, {a.version}, '
              f'latest release {spec["changelog"][0]["releaseVersion"]} ({spec["changelog"][0]["releaseDate"]}). '
              f'Do not edit; regenerate. -->\n')
    text = header + xml
    if a.output:
        open(a.output, 'w').write(text)
    else:
        sys.stdout.write(text)


if __name__ == '__main__':
    main()
