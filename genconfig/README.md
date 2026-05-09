<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# genconfig

Scripts to download CME configuration files from CME's SFTP server.

## Scripts

- **genconfig.sh** - Main script that runs both ilink and mdp3 config generators for all environments (prod, nrcert, cert)
- **genconfig_ilink_3.py** - Downloads iLink market segment configuration from CME SFTP
- **genconfig_mdp3_3.py** - Downloads MDP3 channel configuration from CME SFTP

## Output Files

| File | Description |
|------|-------------|
| `ilink_prod.info` | iLink production market segments |
| `ilink_nrcert.info` | iLink NR certification market segments |
| `ilink_cert.info` | iLink certification market segments |
| `mdp3_prod.info` | MDP3 production channels |
| `mdp3_nrcert.info` | MDP3 NR certification channels |
| `mdp3_cert.info` | MDP3 certification channels |

## Usage

```bash
cd /home/vm/m2_kaspr/genconfig
./genconfig.sh
```

## CME SFTP Server

The scripts connect to CME's SFTP server:
- **Host**: `sftpng.cmegroup.com`
- **Port**: 22 (SSH/SFTP)
- **IP**: `164.74.122.33`
- **Username**: `cmeconfig`
- **Password**: `G3t(0nnect3d`

## Troubleshooting Connection Issues

### Symptoms

If the scripts hang or timeout, the most common cause is a **missing static route** to the CME SFTP server.

```
$ ./genconfig.sh
[ilink] Creating transport to sftpng.cmegroup.com:22...
# hangs here for ~2 minutes, then:
paramiko.ssh_exception.SSHException: Unable to connect to sftpng.cmegroup.com: [Errno 110] Connection timed out
```

### Diagnosis

1. **Test SSH connectivity**:
   ```bash
   ssh -v -o ConnectTimeout=10 sftpng.cmegroup.com
   ```
   If it shows `Connection timed out`, the route is missing.

2. **Run traceroute**:
   ```bash
   traceroute -n 164.74.122.33
   ```
   If you see all `* * *` after the first hop, the route is not configured.

3. **Check DNS resolution**:
   ```bash
   ping sftpng.cmegroup.com
   # Should resolve to 164.74.122.33
   # Note: ping will NOT get responses (ICMP blocked), but DNS should resolve
   ```

### Solution: Add Static Route

Contact your network administrator (Nirvana Technology Solutions) to add the static route:

```
Route: 164.74.122.33/32
Next-hop: 10.4.70.1
```

**Contact**: Nirvana Technology Solutions
- Support: 312-724-7080
- Email: Support@nirvanats.com

### Example Fix Request Email

```
Subject: Add static route to CME SFTP server

Hi,

Can you add a static route 164.74.122.33/32 next-hop 10.4.70.1.

This is needed to connect to CME's SFTP server (sftpng.cmegroup.com) for
downloading market data configuration files.

Thanks
```

### Verifying the Fix

After the route is added, verify connectivity:

```bash
# Should connect (will prompt for password)
ssh -o ConnectTimeout=10 cmeconfig@sftpng.cmegroup.com

# Or test with sftp
sftp cmeconfig@sftpng.cmegroup.com
```

Then run the genconfig scripts:
```bash
./genconfig.sh
```

## Notes

- Ping to `sftpng.cmegroup.com` will NOT work (ICMP is blocked by CME)
- Only SSH/SFTP (TCP port 22) is allowed
- The route goes through the internet, not the CME private network
