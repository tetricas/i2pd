# Security Guide - i2pd File Transfer System

**Purpose**: Security features, threat model, and best practices
**Audience**: Security engineers, DevOps, administrators
**Level**: Advanced

---

## Security Overview

The i2pd File Transfer System implements defense-in-depth with multiple security layers:

1. **I2P Network Layer**: Garlic encryption, onion routing
2. **Transport Layer**: HTTPS with certificate pinning (Session 20)
3. **Application Layer**: Integrity verification (SHA-256)

---

## Threat Model

### Protected Against

✅ **Passive Network Sniffing**
- I2P garlic encryption (multi-layer)
- HTTPS for router.info distribution
- No plaintext credentials or data

✅ **Active MITM Attacks**
- Certificate pinning (Session 20)
- SHA-256 fingerprint verification
- I2P destination authentication

✅ **Data Corruption**
- SHA-256 integrity verification
- Per-transfer checksum validation
- Packet-level error detection

✅ **Memory Exhaustion Attacks**
- 16K SavedPackets limit (Session 15)
- Buffer size limits
- Resource cleanup (RAII patterns)

### NOT Protected Against

❌ **Compromised Endpoints**
- If client or server is compromised, data is exposed
- No application-layer encryption beyond I2P

❌ **Traffic Analysis**
- Timing attacks possible
- File size may be observable
- Transfer patterns may leak information

❌ **DoS Attacks**
- No rate limiting in protocol
- Resource exhaustion possible
- Requires network-level protection

❌ **Side-Channel Attacks**
- CPU timing variations
- Memory access patterns
- Cache timing attacks

---

## Security Features

### 1. HTTPS with Certificate Pinning (Session 20)

**Purpose**: Secure router.info distribution

**Implementation**:
```cpp
// Server: Generate certificate and hash
CreateCertificate();  // RSA 4096-bit
std::string hash = CalculateCertificateHash();  // SHA-256

// Client: Verify fingerprint
bool verified = VerifyCertificateFingerprint(expectedHash);
if (!verified) {
    throw SecurityException("Certificate mismatch");
}
```

**Configuration** (Floodfill):
```ini
[http]
ssl = true
sslport = 7071
sslcert = /var/lib/i2pd/httpd.crt
sslkey = /var/lib/i2pd/httpd.key
```

**Configuration** (Peer):
```ini
[reseed]
floodfill = https://10.114.0.2:7071/router.info
verify = true
cert = a1b2c3d4e5f6...  # SHA-256 fingerprint
```

### 2. I2P Garlic Encryption

**Layers**:
1. **Garlic Layer**: Multi-hop encryption
2. **Tunnel Layer**: Per-hop encryption
3. **Transport Layer**: SSU2/NTCP2 encryption

**Anonymity**:
- Configurable hops: 0-3
- Separate inbound/outbound tunnels
- No single point knows full route

### 3. Data Integrity Verification

**File-Level**:
```cpp
std::string calculateSHA256(const std::vector<uint8_t>& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(data.data(), data.size(), hash);
    return toHexString(hash, SHA256_DIGEST_LENGTH);
}
```

**Verification**:
```cpp
if (receivedHash != expectedHash) {
    throw IntegrityException("Checksum mismatch");
}
```

---

## Security Best Practices

### Deployment Security

**1. Isolate I2P Network**:
```bash
# Use dedicated network namespace
ip netns add i2p_network
ip netns exec i2p_network ./i2pd --datadir=/var/lib/i2pd
```

**2. Minimal Privileges**:
```bash
# Run as dedicated user
useradd -r -s /bin/false i2pd
chown -R i2pd:i2pd /var/lib/i2pd
sudo -u i2pd ./i2pd
```

**3. Firewall Configuration**:
```bash
# Only allow required ports
ufw default deny incoming
ufw allow from 10.114.0.0/24 to any port 7071  # HTTPS
ufw allow from 10.114.0.0/24 to any port 9111  # I2P SSU2
ufw enable
```

### Configuration Security

**1. Disable Unnecessary Services**:
```ini
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
```

**2. Strong Network ID**: # TODO
```ini
netid = 12345678  # Use unpredictable value, not 123
```

**3. Certificate Management**:
```bash
# Generate strong certificate
openssl req -x509 -newkey rsa:4096 -keyout httpd.key \
  -out httpd.crt -days 365 -nodes

# Restrict permissions
chmod 600 httpd.key
chmod 644 httpd.crt
```

### Operational Security

**1. Log Management**:
```bash
# Sanitize logs (remove sensitive data)
sed -i '/router.info/d' /var/log/i2pd.log

# Rotate logs
logrotate /etc/logrotate.d/i2pd
```

**2. Monitoring**:
```bash
# Monitor for suspicious activity
grep -i "error\|fail\|attack" /var/log/i2pd.log | tail -50

# Check resource usage
ps aux | grep i2pd
netstat -tulpn | grep i2pd
```

**3. Update Strategy**:
```bash
# Regular updates
git pull origin openssl
make clean && make -j$(nproc)

# Verify build
sha256sum ./i2pd
```

---

## Security Audit Findings (Session 09)

### Critical Issue: Weak RNG Seeding

**Location**: `libi2pd/RouterContext.cpp`

**Issue**:
```cpp
srand(m_Rng() % 1000);  // Only 1000 possible seeds!
```

**Risk**: HIGH - Predictable random state

**Recommendation**:
```cpp
// REMOVE weak srand() initialization
// Use crypto-safe generators exclusively (RAND_bytes)
```

### Medium Issue: Method Typo

**Location**: `libi2pd/Streaming.h`

**Issue**:
```cpp
const uint8_t* GetRemaningBuffer()  // Typo: "Remaning"
```

**Risk**: MEDIUM - API confusion, maintenance issues

**Recommendation**:
```cpp
const uint8_t* GetRemainingBuffer()  // Correct spelling
// Update all 5 calling locations
```

---

## Incident Response

### Suspected Compromise

**1. Isolate System**:
```bash
# Disconnect from network
systemctl stop i2pd
iptables -P INPUT DROP
iptables -P OUTPUT DROP
```

**2. Collect Evidence**:
```bash
# Preserve logs
cp -a /var/log/i2pd /incident/logs/
cp -a /var/lib/i2pd /incident/data/

# Check file integrity
sha256sum /usr/local/bin/i2pd > /incident/checksums.txt
```

**3. Analyze**:
```bash
# Check for unauthorized changes
diff -r /var/lib/i2pd /incident/data/

# Review logs
grep -i "unauthorized\|error\|fail" /incident/logs/i2pd.log
```

### Certificate Compromise

**1. Revoke Certificate**:
```bash
# Generate new certificate
./generate_new_certificate.sh

# Update all peers with new fingerprint
ansible-playbook update_cert_fingerprint.yml
```

**2. Rotate Keys**:
```bash
# Generate new I2P destination
rm /var/lib/i2pd/destinations/*

# Restart with new identity
systemctl restart i2pd
```

---

## Secure Deployment Checklist

### Pre-Deployment

- [ ] Review security audit findings
- [ ] Update to latest stable version
- [ ] Generate strong certificates (RSA 4096)
- [ ] Configure firewall rules
- [ ] Set up log rotation
- [ ] Create dedicated user account
- [ ] Configure resource limits

### Deployment

- [ ] Use minimal configuration
- [ ] Enable HTTPS with certificate pinning
- [ ] Verify all certificate fingerprints
- [ ] Test in isolated environment first
- [ ] Monitor for 24-48 hours
- [ ] Document configuration

### Post-Deployment

- [ ] Regular log review
- [ ] Monitor resource usage
- [ ] Update certificates before expiry
- [ ] Apply security patches
- [ ] Review access logs
- [ ] Test backup/restore procedures

---

## Security Configuration Examples

### Floodfill (Maximum Security):
```ini
netid = <strong_random_value>
floodfill = true

[http]
enabled = true
address = 10.114.0.2
port = 7070
auth = true
user = admin
pass = <strong_password>

[http]
ssl = true
sslport = 7071
sslcert = /var/lib/i2pd-ff/httpd.crt
sslkey = /var/lib/i2pd-ff/httpd.key

[limits]
transittunnels = 100
openfiles = 500
coresize = 0

[reseed]
verify = true
```

### Peer (Maximum Security):
```ini
netid = <same_as_floodfill>

[reseed]
floodfill = https://10.114.0.2:7071/router.info
verify = true
cert = <sha256_fingerprint>
urls =
threshold = 0

[limits]
transittunnels = 100
openfiles = 500

[http]
enabled = false

[httpproxy]
enabled = false

[socksproxy]
enabled = false
```

---

## Conclusion

The i2pd File Transfer System provides:

✅ **Transport Security**: HTTPS with certificate pinning
✅ **Network Security**: I2P garlic encryption
✅ **Data Integrity**: SHA-256 verification
✅ **Memory Safety**: Resource limits and RAII

**Critical**: Address weak RNG seeding before production deployment.

**Status**: Production-ready with documented security considerations.

---

**Document Version**: 1.0
**Last Updated**: December 2025
