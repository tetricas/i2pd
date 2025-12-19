# Deployment Guide - i2pd File Transfer System

**Purpose**: Production deployment procedures
**Audience**: DevOps engineers, system administrators
**Level**: Operational

---

## Deployment Overview

This guide covers deploying the i2pd File Transfer System in production environments.

**Deployment Patterns**:
1. Single Floodfill + Multiple Peers
2. Cloud-Based Private I2P Network
3. On-Premises Deployment

---

## Prerequisites

### System Requirements

**Recommended** (Production):
- OS: Ubuntu 22.04 LTS
- CPU: 1-2+ cores
- RAM: 2GB+
- Disk: 50GB+ SSD
- Network: 1Gbps internal

### Software Dependencies

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    libboost-all-dev \
    libssl-dev \
    git \
    systemd
```

---

## Step 1: Build i2pd

```bash
# Clone repository
cd ~
git clone <repository_url> i2pd
cd i2pd
git checkout ftp-base  # Production branch

# Build
mkdir -p cmake-debug
cd cmake-debug
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# Verify build
./i2pd --version
```

### Build File Transfer Client

```bash
cd ~/i2pd/tests_client
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# Verify
./chunked_file_test --help
```

---

## Step 2: Deploy Floodfill Node

### Create Configuration

```bash
sudo mkdir -p /etc/i2pd
sudo mkdir -p /var/lib/i2pd-ff

cat | sudo tee /etc/i2pd/ff.conf << 'EOF'
netid = <your_network_id>
floodfill = true
bandwidth = X

[limits]
transittunnels = 1000
openfiles = 2000
coresize = 0

[http]
enabled = true
address = 0.0.0.0
port = 7070
auth = false
ssl = true
sslport = 7071
sslcert = /var/lib/i2pd-ff/httpd.crt
sslkey = /var/lib/i2pd-ff/httpd.key

[httpproxy]
enabled = false

[socksproxy]
enabled = false

[sam]
enabled = false

[bob]
enabled = false

[i2pcontrol]
enabled = false

[reseed]
verify = false
urls =
threshold = 0
EOF
```

### Create Systemd Service

```bash
cat | sudo tee /etc/systemd/system/i2pd-ff.service << 'EOF'
[Unit]
Description=I2P Daemon - Floodfill
After=network.target

[Service]
Type=simple
User=i2pd
ExecStart=/usr/local/bin/i2pd --datadir=/var/lib/i2pd-ff --conf=/etc/i2pd/ff.conf
Restart=on-failure
RestartSec=10
LimitNOFILE=65536

[Install]
WantedBy=multi-user.target
EOF
```

### Deploy

```bash
# Create user
sudo useradd -r -s /bin/false i2pd

# Install binary
sudo cp ~/i2pd/cmake-debug/i2pd /usr/local/bin/
sudo chmod +x /usr/local/bin/i2pd

# Set permissions
sudo chown -R i2pd:i2pd /var/lib/i2pd-ff
sudo chown -R i2pd:i2pd /etc/i2pd

# Start service
sudo systemctl daemon-reload
sudo systemctl enable i2pd-ff
sudo systemctl start i2pd-ff

# Verify
sudo systemctl status i2pd-ff
sudo journalctl -u i2pd-ff -f
```

### Retrieve Certificate Hash

```bash
# Wait for certificate generation (~30 seconds)
sleep 30

# Get hash
sudo cat /var/lib/i2pd-ff/httpd.crt_hash.txt
# Example: a1b2c3d4e5f6789...

# Save for peer configuration
echo "FF_CERT_HASH=a1b2c3d4e5f6789..." > /tmp/ff_cert_hash.txt
```

---

## Step 3: Deploy Peer Nodes

### Create Configuration

```bash
# On each peer node
sudo mkdir -p /etc/i2pd
sudo mkdir -p /var/lib/i2pd-peer

cat | sudo tee /etc/i2pd/peer.conf << 'EOF'
netid = <same_as_floodfill>
bandwidth = P

[limits]
transittunnels = 500
openfiles = 1000

[http]
enabled = false

[httpproxy]
enabled = false

[socksproxy]
enabled = false

[reseed]
floodfill = https://<FF_IP>:7071/router.info
verify = true
cert = <FF_CERT_HASH>
urls =
threshold = 0
EOF
```

Replace:
- `<FF_IP>`: Floodfill IP address (e.g., 10.114.0.2)
- `<FF_CERT_HASH>`: From `/tmp/ff_cert_hash.txt`

### Create Systemd Service

```bash
cat | sudo tee /etc/systemd/system/i2pd-peer.service << 'EOF'
[Unit]
Description=I2P Daemon - Peer
After=network.target

[Service]
Type=simple
User=i2pd
ExecStart=/usr/local/bin/i2pd --datadir=/var/lib/i2pd-peer --conf=/etc/i2pd/peer.conf
Restart=on-failure
RestartSec=10
LimitNOFILE=65536

[Install]
WantedBy=multi-user.target
EOF
```

### Deploy

```bash
sudo cp ~/i2pd/cmake-debug/i2pd /usr/local/bin/
sudo chown -R i2pd:i2pd /var/lib/i2pd-peer
sudo systemctl daemon-reload
sudo systemctl enable i2pd-peer
sudo systemctl start i2pd-peer
sudo systemctl status i2pd-peer
```

---

## Step 4: Verify Network

### Check Floodfill

```bash
# Check service
sudo systemctl status i2pd-ff

# Check logs
sudo journalctl -u i2pd-ff | grep "RouterInfo updated"

# Check HTTPS endpoint
curl -k https://<FF_IP>:7071/router.info | head -c 100
```

### Check Peers

```bash
# On each peer
sudo journalctl -u i2pd-peer | grep "Reseed"
# Should show: "inserted RI into netDb"

# Check NetDb
ls /var/lib/i2pd-peer/netDb/r*/ | wc -l
# Should show at least 1 (the floodfill)

# Check tunnels
sudo journalctl -u i2pd-peer | grep "Tunnel created"
```

---

## Step 5: Deploy File Transfer Service

### Install Client

```bash
# Copy binary to all nodes
sudo cp ~/i2pd/tests_client/build/chunked_file_test /usr/local/bin/
sudo chmod +x /usr/local/bin/chunked_file_test
```

### Server Deployment

```bash
# Create data directory
sudo mkdir -p /var/lib/file-transfer

# Create systemd service
cat | sudo tee /etc/systemd/system/file-transfer-server.service << 'EOF'
[Unit]
Description=i2pd File Transfer Server
After=i2pd-peer.service
Requires=i2pd-peer.service

[Service]
Type=simple
User=i2pd
WorkingDirectory=/var/lib/file-transfer
ExecStart=/usr/local/bin/chunked_file_test server \
    --hops 1 \
    --normal \
    --conf /etc/i2pd/peer.conf \
    --datadir /var/lib/file-transfer/server
Restart=on-failure
RestartSec=10

[Install]
WantedBy=multi-user.target
EOF

# Start service
sudo systemctl daemon-reload
sudo systemctl enable file-transfer-server
sudo systemctl start file-transfer-server

# Get B32 address
sleep 10
cat /tmp/server_b32.txt
```

---

## Production Checklist

### Security

- [ ] Firewall configured
- [ ] SSL certificates generated (RSA 4096)
- [ ] Certificate hashes distributed
- [ ] Unnecessary services disabled
- [ ] Log rotation configured
- [ ] Monitoring set up

### Performance

- [ ] Resource limits configured
- [ ] Network detection enabled
- [ ] Flow control optimized
- [ ] Bulk transfer optimization active

### Reliability

- [ ] Systemd services configured
- [ ] Auto-restart enabled
- [ ] Health checks implemented
- [ ] Backup procedures defined

### Operational

- [ ] Documentation updated
- [ ] Runbooks created
- [ ] Alert thresholds set
- [ ] On-call procedures defined

---

## Monitoring & Maintenance

### Health Checks

```bash
#!/bin/bash
# /usr/local/bin/health_check.sh

# Check i2pd process
if ! systemctl is-active --quiet i2pd-peer; then
    echo "ERROR: i2pd not running"
    exit 1
fi

# Check tunnels
TUNNELS=$(grep -c "Tunnel created" /var/log/i2pd/i2pd.log)
if [ $TUNNELS -lt 2 ]; then
    echo "WARNING: Insufficient tunnels"
    exit 1
fi

echo "OK: System healthy"
exit 0
```

### Performance Monitoring

```bash
# Monitor throughput
watch -n 5 'grep "throughput" /var/log/file-transfer.log | tail -5'

# Monitor memory
watch -n 5 'ps aux | grep i2pd'

# Monitor network
iftop -i eth0
```

---

## Troubleshooting

See **[09-Troubleshooting.md](09-Troubleshooting.md)** for detailed troubleshooting procedures.

---

## Upgrade Procedure

```bash
# Stop services
sudo systemctl stop file-transfer-server
sudo systemctl stop i2pd-peer

# Backup
sudo tar -czf /backup/i2pd-$(date +%Y%m%d).tar.gz \
    /var/lib/i2pd-peer \
    /etc/i2pd

# Update binary
cd ~/i2pd
git pull origin openssl
cd cmake-debug
make -j$(nproc)
sudo cp i2pd /usr/local/bin/

# Restart
sudo systemctl start i2pd-peer
sudo systemctl start file-transfer-server

# Verify
sudo systemctl status i2pd-peer file-transfer-server
```

---

## Rollback Procedure

```bash
# Stop services
sudo systemctl stop file-transfer-server i2pd-peer

# Restore from backup
sudo tar -xzf /backup/i2pd-<date>.tar.gz -C /

# Restore binary
sudo cp /backup/i2pd.old /usr/local/bin/i2pd

# Restart
sudo systemctl start i2pd-peer file-transfer-server
```

---

**Document Version**: 1.0
**Last Updated**: December 2025
