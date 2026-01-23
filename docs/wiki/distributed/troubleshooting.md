# Distributed Mode Troubleshooting

Common issues and solutions for keyhunt distributed computing.

## Connection Issues

### Client Cannot Connect to Server

**Symptoms**: "Connection refused" or timeout

**Solutions**:

1. **Verify server is running**:
   ```bash
   # On server
   ps aux | grep keyhunt
   ```

2. **Check server is listening**:
   ```bash
   # On server
   netstat -tlnp | grep 2222
   ss -tlnp | grep 2222
   ```

3. **Test connectivity**:
   ```bash
   # On client
   nc -zv server-ip 2222
   telnet server-ip 2222
   ```

4. **Check firewall**:
   ```bash
   # On server (Ubuntu)
   sudo ufw status
   sudo ufw allow 2222/tcp

   # On server (CentOS)
   sudo firewall-cmd --list-ports
   sudo firewall-cmd --add-port=2222/tcp --permanent
   sudo firewall-cmd --reload
   ```

5. **Verify correct IP**:
   ```bash
   # On server
   ip addr show
   hostname -I
   ```

### Frequent Disconnections

**Symptoms**: Client keeps reconnecting

**Solutions**:

1. **Check network stability**:
   ```bash
   ping -c 100 server-ip
   ```

2. **Increase heartbeat timeout**:
   Configure longer timeout in wizard

3. **Check for packet loss**:
   ```bash
   mtr server-ip
   ```

4. **Check server load**:
   ```bash
   # On server
   top -b -n 1 | head -20
   ```

### Target File Mismatch

**Symptoms**: "TARGET_MISMATCH" error

**Solutions**:

1. **Ensure identical target files**:
   ```bash
   # Compare hashes
   md5sum target.txt  # On both machines
   ```

2. **Re-download target file** on client

3. **Check file encoding** (must be UTF-8, LF line endings):
   ```bash
   file target.txt
   ```

## Performance Issues

### Slow Search Speed

**Symptoms**: Lower than expected Mkeys/s

**Solutions**:

1. **Check CPU utilization**:
   ```bash
   htop
   # Look for low CPU usage or thermal throttling
   ```

2. **Verify SIMD detection**:
   ```
   # Check startup output for:
   [+] AVX2: Enabled
   [+] SHA-NI: Enabled
   ```

3. **Check thread count**:
   ```bash
   ./keyhunt --client --server-ip x.x.x.x -t $(nproc)
   ```

4. **Disable power saving**:
   ```bash
   sudo cpupower frequency-set -g performance
   ```

5. **Check memory bandwidth** (BSGS mode):
   ```bash
   # Ensure sufficient RAM
   free -h
   ```

### Work Units Complete Too Quickly

**Symptoms**: Constant work requests, high coordinator overhead

**Solution**: Increase work unit size in server configuration

```json
{
  "server": {
    "work_unit_size": "0x1000000000000"
  }
}
```

### Work Units Take Too Long

**Symptoms**: Progress reporting shows slow completion

**Solution**: Decrease work unit size for better load balancing

```json
{
  "server": {
    "work_unit_size": "0x10000000000"
  }
}
```

## Server Issues

### High Memory Usage

**Symptoms**: Server consuming excessive RAM

**Solutions**:

1. **Reduce bloom filter size**:
   Use fewer target addresses

2. **Disable server worker**:
   Run coordinator-only mode

3. **Check for memory leaks**:
   ```bash
   valgrind --leak-check=full ./keyhunt ...
   ```

### Checkpoint Not Saving

**Symptoms**: Progress lost after restart

**Solutions**:

1. **Check disk space**:
   ```bash
   df -h
   ```

2. **Check file permissions**:
   ```bash
   ls -la keyhunt_checkpoint.json
   ```

3. **Reduce checkpoint interval**:
   ```bash
   --checkpoint-interval 30
   ```

### Port Already in Use

**Symptoms**: "Address already in use" error

**Solutions**:

1. **Find process using port**:
   ```bash
   sudo lsof -i :2222
   sudo fuser 2222/tcp
   ```

2. **Kill old process**:
   ```bash
   sudo kill <PID>
   ```

3. **Use different port**:
   ```bash
   ./keyhunt ... --port 2223
   ```

## Client Issues

### GPU Not Detected

**Symptoms**: GPU mode disabled despite having GPU

**Solutions**:

1. **Check CUDA installation**:
   ```bash
   nvidia-smi
   nvcc --version
   ```

2. **Verify CUDA paths**:
   ```bash
   export PATH=/usr/local/cuda/bin:$PATH
   export LD_LIBRARY_PATH=/usr/local/cuda/lib64:$LD_LIBRARY_PATH
   ```

3. **Rebuild with CUDA support**

### Client Crashes

**Symptoms**: Segmentation fault or unexpected exit

**Solutions**:

1. **Check for OOM (Out of Memory)**:
   ```bash
   dmesg | grep -i "out of memory"
   dmesg | grep -i "killed process"
   ```

2. **Run with AddressSanitizer**:
   ```bash
   make clean
   CXXFLAGS="-fsanitize=address" make
   ```

3. **Reduce thread count**

### Progress Not Reported

**Symptoms**: Server shows 0 speed for client

**Solutions**:

1. **Check client output** for errors

2. **Verify network** isn't blocking progress messages

3. **Increase progress interval** for debugging:
   ```bash
   -s 5  # Report every 5 seconds
   ```

## Recovery Procedures

### Recovering from Server Crash

1. **Check checkpoint file exists**:
   ```bash
   ls -la keyhunt_checkpoint.json
   ```

2. **Restart server with same parameters**:
   ```bash
   ./keyhunt --wizard  # or original command
   ```

3. **Verify checkpoint loaded**:
   ```
   [+] Loaded checkpoint: 1523/4096 units completed
   ```

### Recovering from Client Crash

Clients automatically resume:

1. **Simply restart client**
2. **Server will reassign work unit** (after timeout)
3. **Progress from current unit may be lost**

### Recovering from Corrupted Checkpoint

1. **Backup corrupted file**:
   ```bash
   mv keyhunt_checkpoint.json keyhunt_checkpoint.json.bak
   ```

2. **Start fresh** (loses progress)

3. **Or manually edit** JSON to fix corruption

## Debugging

### Enable Verbose Logging

```bash
./keyhunt ... -v   # or --verbose
```

### Capture Network Traffic

```bash
# On server
sudo tcpdump -i any port 2222 -w capture.pcap
```

### Test Protocol Manually

```bash
# Connect and send test message
nc server-ip 2222
{"type":"heartbeat","client_name":"test"}
```

### Check System Resources

```bash
# CPU, memory, IO
vmstat 1

# Disk IO
iostat -x 1

# Network
iftop
```

## Common Error Messages

| Error | Cause | Solution |
|-------|-------|----------|
| "Connection refused" | Server not running or firewall | Check server, open port |
| "TARGET_MISMATCH" | Different target files | Sync target files |
| "VERSION_MISMATCH" | Different keyhunt versions | Update all nodes |
| "INVALID_MESSAGE" | Protocol error | Check for version mismatch |
| "No work available" | All units assigned | Wait or add more units |
| "Out of memory" | Insufficient RAM | Reduce threads or N value |

## Getting Help

If issues persist:

1. **Collect logs**: Server and client output
2. **System info**: `uname -a`, `cat /proc/cpuinfo`
3. **Network info**: `ip addr`, firewall rules
4. **Create GitHub issue** with all details

## See Also

- [Server Setup](server-setup.md) - Configuration options
- [Client Setup](client-setup.md) - Client configuration
- [Protocol](protocol.md) - Message format details
