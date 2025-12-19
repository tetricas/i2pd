# i2pd File Transfer System - Complete Documentation

**Version:** 1.0  
**Last Updated:** December 2025  
**Project Status:** Production Ready  
**Production Tool:** `tunnel_ftp_demo`

---

## Welcome

This documentation provides a comprehensive guide to the i2pd File Transfer System. This project represents 20 sessions of development work, evolving from streaming-based experiments (`chunked_file_test`) to the final tunnel-based production system (`tunnel_ftp_demo`) optimized for cloud environments.

---

## Production vs Experimental

### ✅ Production Tool: tunnel_ftp_demo

**The cloud-optimized production system** achieving peak performance:
- **Architecture**: Tunnel/datagram-based communication
- **Performance**: 5-6 MB/s (100MB), 8-9 MB/s (1GB) in cloud
- **Location**: `tests_client/app/tunnel_ftp_demo.cpp`

### 📚 Experimental Foundation: chunked_file_test  

**The development path** that led to production system:
- **Architecture**: Streaming-based (Simple & Normal protocols)
- **Sessions**: 01-16 (extensive development and optimization)
- **Limitation**: Could not reach peak speeds in cloud environments
- **Value**: Essential learning that informed tunnel_ftp_demo design
- **Status**: Deprecated for production, documented as development history

---

## What is This Project?

The i2pd File Transfer System is an **enterprise-grade file transfer solution** for private I2P networks:

- **High Performance**: 5-6 MB/s (100MB), 8-9 MB/s (1GB) in cloud
- **Tunnel-Based Protocol**: Optimized datagram communication
- **Security**: HTTPS with certificate pinning
- **Reliability**: 100% data integrity (CRC32)
- **Cloud-Optimized**: Built for cloud deployments

---

## Project Highlights

### Performance (tunnel_ftp_demo in cloud)

- **100MB**: 5-6 MB/s (17-20 seconds, 200-250% vs baseline)
- **1GB**: 8-9 MB/s (~2 minutes, 400-450% vs baseline)  
- **Zero crashes** - complete stability
- **100% integrity** - CRC32 verification

### Development Evolution

**Phase 1 (Sessions 01-16)**: Streaming Experiments
- `chunked_file_test` development
- Simple Messaging & Normal Streaming protocols
- Session 15: Critical optimizations (16K limit, 4K batching)
- Result: Good local performance, but cloud limitations discovered

**Phase 2 (Sessions 17-20)**: Production System
- `tunnel_ftp_demo` development
- Tunnel/datagram architecture
- Cloud optimization
- Result: **5-9 MB/s in cloud** (production ready)

---

## Quick Navigation

### Getting Started
1. **[Quick Start Guide](01-Quick-Start-Guide.md)** - tunnel_ftp_demo setup (15 min)
2. **[Deployment Guide](07-Deployment-Guide.md)** - Production deployment
3. **[Testing Guide](08-Testing-Guide.md)** - Validation

### Understanding the System
4. **[Architecture Overview](02-Architecture-Overview.md)** - Evolution & design
5. **[Development History](03-Development-History.md)** - Complete 20-session journey
6. **[File Transfer Protocols](04-File-Transfer-Protocols.md)** - All protocols

### Advanced
7. **[Performance Optimizations](05-Performance-Optimizations.md)** - Optimization journey
8. **[Security Guide](06-Security-Guide.md)** - Security features
9. **[API Reference](10-API-Reference.md)** - Code interfaces

### Operations
10. **[Troubleshooting](09-Troubleshooting.md)** - Common issues
11. **[Future Roadmap](11-Future-Roadmap.md)** - Future plans

---

## System Requirements

**Recommended**: Ubuntu 22.04+, 1-2+ cores, 2GB+ RAM, 50GB+ SSD  
**Network**: Private I2P network with floodfill node

---

## Key Metrics

| Metric | Value |
|--------|-------|
| Production Tool | tunnel_ftp_demo |
| Experimental Tool | chunked_file_test (deprecated) |
| 100MB Performance | 5-6 MB/s (cloud) |
| 1GB Performance | 8-9 MB/s (cloud) |
| Data Integrity | 100% (CRC32) |

---

## Next Steps

**New Users**: Start with [Quick Start Guide](01-Quick-Start-Guide.md)  
**Developers**: Read [Architecture Overview](02-Architecture-Overview.md) → [Development History](03-Development-History.md)  
**Operations**: Follow [Deployment Guide](07-Deployment-Guide.md)

---

**Status**: ✅ Production Ready (tunnel_ftp_demo)  
**Document Version**: 1.0  
**Last Updated**: December 2025
