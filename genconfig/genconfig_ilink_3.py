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
    print(f"[ilink] {msg}", file=sys.stderr, flush=True)

parser = argparse.ArgumentParser(description='gen script to generate config file for ilink3')
parser.add_argument('--env', type=str, help='env')
args = parser.parse_args()

log(f"Starting genconfig_ilink_3.py with env={args.env}")

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

#
# select env
#
if args.env=="prod":
  remote_path="/MSGW/Production/Configuration"
elif args.env=="nrcert":
  remote_path="/MSGW/NRCert/Configuration"
elif args.env=="cert":
  remote_path="/MSGW/Cert/Configuration"
else:
  log("env must be prod, nrcert, or cert")
  sys.exit(1)

log(f"Remote path: {remote_path}")

# Get the file from the remote server
tmpfname='/tmp/MSGW_Config.xml'
remote_file = remote_path+"/MSGW_Config.xml"
log(f"Downloading {remote_file} to {tmpfname}...")
try:
    sftp.get(remote_file, tmpfname)
    log(f"Download complete")
except Exception as e:
    log(f"ERROR downloading file: {e}")
    sftp.close()
    transport.close()
    sys.exit(1)

log("Closing SFTP connection...")
# Close the SFTP connection
sftp.close()

log("Closing transport connection...")
# Close the transport connection
transport.close()

log(f"Reading downloaded file {tmpfname}...")
conf=open(tmpfname).read()

log("Parsing XML...")
configuration = ET.fromstring(conf)

log("Generating output...")
for marketsegment in configuration:
  segid = marketsegment.attrib['id']
  seglbl = marketsegment.attrib['label']

  print ("")
  print ("marketsegment"+str(segid),"{")
  print ("segid",segid)
  print ("name",'"'+seglbl+'"')

  for connections in marketsegment:
    if connections.tag == 'primary-host-ip':
      print ("primaryhost",connections.text)
    elif connections.tag == 'backup-host-ip':
      print ("backuphost",connections.text)
    elif connections.tag == 'ToCMESchemaVersion':
      print ('ToCMESchemaVersion "%s"' % (connections.text))
    elif connections.tag == 'FromCMESchemaVersion':
      print ('FromCMESchemaVersion "%s"' % (connections.text))

  print ("}")

log("genconfig_ilink_3.py completed successfully")
