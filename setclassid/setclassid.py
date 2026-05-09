#!/usr/bin/env python3

# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.

import fnmatch
import shutil
import os
import re

#
# maximum is 512
#

rdir=os.environ["CHOPIN_PROJ"]
print (rdir)

def listFilesRecursive(dirP,pattern):
    ret = listFiles(dirP,pattern)
    dirs= listDirs(dirP,'*')
    for d in dirs:
        files=listFilesRecursive(d,pattern)
        for f in files:
            ret.append(f)
    ret.sort()
    return ret

def listFiles(dirP,pattern):
    ret = []
    if dirP[len(dirP)-1] != '/':
        dirP = dirP + '/'
    l = os.listdir(dirP)
    l = fnmatch.filter(l,pattern)
    for f in l:
        if os.path.isfile(dirP+f):
            ret.append(dirP+f)
    ret.sort()
    return ret

def listDirs(dirP,pattern):
    ret = []
    if dirP[len(dirP)-1] != '/':
        dirP = dirP + '/'
    l = os.listdir(dirP)
    l = fnmatch.filter(l,pattern)
    for f in l:
        if os.path.isdir(dirP+f):
            ret.append(dirP+f)
    ret.sort()
    return ret

hfiles=listFilesRecursive(rdir, "*.hpp")

# Pass 1: Build global constants dictionary
constants = {}
for f in hfiles:
    with open(f,'r') as fd:
        for line in fd:
            # Match: constexpr int MSG_NAME = 123;
            match = re.search(r'constexpr\s+int\s+(\w+)\s*=\s*(\d+)', line)
            if match:
                const_name = match.group(1)
                const_value = int(match.group(2))
                constants[const_name] = const_value
            # Match: #define MSG_NAME 123
            match = re.search(r'#define\s+(\w+)\s+(\d+)', line)
            if match:
                const_name = match.group(1)
                const_value = int(match.group(2))
                constants[const_name] = const_value

# Pass 2: Process Message_N<...> declarations
cnt=0
edits=[]
msgid=[]
for f in hfiles:
    fd=open(f,'r')
    lines=fd.readlines()
    fd.close()
    fname_root=f.split(".hpp")[0]
    for i in range(len(lines)):
        l=lines[i]
        # Skip comment lines
        stripped = l.strip()
        if stripped.startswith('//') or stripped.startswith('*'):
            continue
        idx4=l.find("Message_N<")
        if idx4>0:
            #print(l)
            idx4+=len("Message_N<")
            idx5=l.find(">",idx4)
            if idx5>0:
                msg_value_str = l[idx4:idx5].strip()
                msgidx = None

                # Try direct int conversion
                try:
                    msgidx = int(msg_value_str)
                    print(f,msgidx)
                except ValueError:
                    # Not a number, try constant lookup
                    if msg_value_str in constants:
                        msgidx = constants[msg_value_str]
                        print(f, msg_value_str, "->", msgidx)
                    else:
                        print(f, msg_value_str, "-> NOT_FOUND")
                        continue

                if msgidx is not None:
                    if msgidx in msgid:
                        # Find free slots when duplicate detected
                        used = set(msgid)
                        free_slots = [i for i in range(512) if i not in used and i not in [0, 3]]
                        print(msgidx,"*** IS USED MORE THAN ONCE *** DUPLICATE ***",f)
                        print("   Used IDs so far:", sorted(msgid))
                        print("   FREE SLOTS (examples):", free_slots[:20])
                    assert msgidx not in msgid
                    assert msgidx < 512
                    msgid.append((msgidx))

if msgid:
    print ("max is",max(msgid))
    print(sorted(msgid))

    # Print summary of free slots
    print("\n=== MESSAGE ID ALLOCATION SUMMARY ===")
    used = set(msgid)
    free_slots = [i for i in range(512) if i not in used and i not in [0, 3]]
    print(f"Total IDs used: {len(msgid)}/512")
    print(f"Total IDs available: {len(free_slots)}")
    print(f"Reserved IDs (0, 3): 2")
    print(f"\nFree slots: {free_slots}")












