#!/bin/python3

# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
# Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
#
# Licensed under the MIT License. See LICENSE file in the project root.

import xml.etree.ElementTree as ET
import paramiko
import sys
import argparse
import socket

# Set socket timeout to prevent indefinite hangs
socket.setdefaulttimeout(30)

def log(msg):
    """Print log message to stderr so it doesn't pollute stdout output"""
    print(f"[mdp3] {msg}", file=sys.stderr, flush=True)

parser = argparse.ArgumentParser(description='gen script to generate config file for mdp3.3')
parser.add_argument('--file', type=str, default="", help='file')
parser.add_argument('--env', type=str, help='env')

args = parser.parse_args()

log(f"Starting genconfig_mdp3_3.py with env={args.env}, file={args.file}")

hostname='sftpng.cmegroup.com'

log(f"Creating transport to {hostname}:22...")
try:
    # Create a transport object
    transport = paramiko.Transport((hostname, 22))
    log("Transport created successfully")
except Exception as e:
    log(f"ERROR creating transport: {e}")
    sys.exit(1)

log("Connecting/authenticating to SFTP server...")
try:
    # Authenticate to the server
    transport.connect(username='cmeconfig', password='G3t(0nnect3d')
    log("Authentication successful")
except Exception as e:
    log(f"ERROR during authentication: {e}")
    transport.close()
    sys.exit(1)

log("Opening SFTP client...")
try:
    # Create an SFTP object from the transport
    sftp = transport.open_sftp_client()
    log("SFTP client opened successfully")
except Exception as e:
    log(f"ERROR opening SFTP client: {e}")
    transport.close()
    sys.exit(1)

# urlprod         = "https://www.cmegroup.com/ftp/SBEFix/Production/Configuration/config.xml"
# urlnrcert       = "https://www.cmegroup.com/ftp/SBEFix/NRCert/Configuration/config.xml"
# urlcert         = "https://www.cmegroup.com/ftp/SBEFix/Cert/Configuration/config.xml"
# urlautocertplus = "https://www.cmegroup.com/ftp/SBEFix/CertAutoCertPlus/Configuration/config.xml"

# urlprod_ust         = "https://www.cmegroup.com/ftp/SBEFix/Production/Configuration/USActivesPrmConfiguration/USActivesPrmConfig.xml"
# urlnrcert_ust       = "https://www.cmegroup.com/ftp/SBEFix/NRCert/Configuration/USActivesPrmConfiguration/USActivesPrmConfig.xml"
# urlcert_ust         = "https://www.cmegroup.com/ftp/SBEFix/Cert/Configuration/USActivesPrmConfiguration//USActivesPrmConfig.xml"
# urlautocertplus_ust = "https://www.cmegroup.com/ftp/SBEFix/CertAutoCertPlus/Configuration/USActivesPrmConfiguration/USActivesPrmConfig.xml"

if args.env == "prod":
  remote_path = "SBEFix/Production/Configuration"
elif args.env == "nrcert":
  remote_path = "SBEFix/NRCert/Configuration"
elif args.env == "cert":
  remote_path = "SBEFix/Cert/Configuration"
elif args.env == "autocertplus":
  remote_path = "SBEFix/CertAutoCertPlus/Configuration"
elif args.env == "prod_ust":
  remote_path = "SBEFix/Production/Configuration/USActivesPrmConfiguration"
elif args.env == "nrcert_ust":
  remote_path = "SBEFix/NRCert/Configuration/USActivesPrmConfiguration"
elif args.env == "cert_ust":
  remote_path = "SBEFix/Cert/Configuration/USActivesPrmConfiguration"
elif args.env == "autocertplus_ust":
  remote_path = "SBEFix/CertAutoCertPlus/Configuration/USActivesPrmConfiguration"
else:
  log(f"ERROR: Invalid environment {args.env}")
  sftp.close()
  transport.close()
  sys.exit(1)

log(f"Remote path: {remote_path}")

if args.file == "":
  # Get the file from the remote server
  if "autocert" in args.env:
    tmpfname='/tmp/ACPUSActivesPrmConfig.xml'
    targetfname='/ACPUSActivesPrmConfig.xml'
  elif "ust" in args.env:
    tmpfname='/tmp/USActivesPrmConfig.xml'
    targetfname='/USActivesPrmConfig.xml'
  else:
    tmpfname='/tmp/config.xml'
    targetfname='/config.xml'

  remote_file = remote_path+targetfname
  log(f"Downloading {remote_file} to {tmpfname}...")
  try:
      sftp.get(remote_file, tmpfname)
      log("Download complete")
  except Exception as e:
      log(f"ERROR downloading file: {e}")
      sftp.close()
      transport.close()
      sys.exit(1)

  log("Closing SFTP connection...")
  sftp.close()

  log("Closing transport connection...")
  transport.close()

  log(f"Parsing XML from {tmpfname}...")
  tree = ET.parse(tmpfname)
  root = tree.getroot()
else:
  log("Closing SFTP connection (using local file)...")
  sftp.close()
  transport.close()

  log(f"Parsing XML from local file {args.file}...")
  tree = ET.parse(args.file)
  root = tree.getroot()

log("Generating output...")
for channel in root:
  chid = channel.attrib['id']
  chlbl = channel.attrib['label']

  print ("")
  print ("chan"+str(chid),"{")
  print ("chanid",chid)
  print ("name",'"'+chlbl+'"')
  for connections in channel:
    if connections.tag != 'connections':
      continue
    for conparams in connections:
      conid = conparams.attrib['id']
      if 'IA' in conid:
        sfx = '_a'
      elif 'IB' in conid:
        sfx = "_b"
      elif 'NA' in conid:
        sfx = "_ir"
      elif 'AMBO' in conid:
        sfx = "_dr"
      else:
        continue

      for params in conparams:
        if params.tag == 'ip':
          print ("group"+sfx,params.text)
        if params.tag == 'port':
          print ("port"+sfx,params.text)


#
# CME Production Interface IP Addresses
#
# CME Futures (ES/NQ/ZN/etc): Use VLAN 3008/3080
#   mdinterface_a: 172.17.248.103 (ens1f0.3008)
#   mdinterface_b: 172.17.249.103 (ens1f1.3080)
#
# BTEC uses different interfaces: 172.19.22.195/211 (VLAN 3009/3081)
# CERT uses: 10.248.146.35 (VLAN 311)
#
# To verify: ip addr | grep -E "172.17.248|172.17.249"
#

  if args.env == "prod" or args.env == "prod_ust":
    print ("mdinterface_a 172.17.248.103")
    print ("mdinterface_b 172.17.249.103")
  elif "cert" in args.env:
    print ("mdinterface_a 10.248.146.35")
    print ("mdinterface_b 10.248.146.35")
    #print ("mdinterface_b 10.248.146.90")
  else:
    log(f"WARNING: No interface defined for environment {args.env}")
    print ("mdinterface_a 0.0.0.0")
    print ("mdinterface_b 0.0.0.0")

  print ("}")

log("genconfig_mdp3_3.py completed successfully")
