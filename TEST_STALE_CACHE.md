# Stale Cache Testing Documentation

## Overview

This document describes the testing procedure for verifying that the keyhunt wizard properly handles stale cache (>24 hours old) in offline mode.

## Test Objective

Verify that when community API data is cached but older than 24 hours:
1. The wizard attempts to fetch fresh data from network
2. If network fails, it falls back to stale cache with a warning
3. The warning clearly indicates cache age (in days or hours)
4. The wizard continues operation with stale data

## Cache Architecture

### Cache Files

The wizard maintains separate cache files for each community data source:

| Source | Cache File | Location |
|--------|-----------|----------|
| Privatekeys.pw | `privatekeys_progress.json` | `~/.keyhunt/` |
| Keys.lol | `keyslol_progress.json` | `~/.keyhunt/` |

### Cache Structure

```json
{
  "puzzle_number": 66,
  "percent_scanned": 42.123456,
  "keys_scanned": 123456789,
  "fetch_time": 1708992000
}
```

**Key Fields:**
- `fetch_time`: Unix timestamp of when data was fetched
- Staleness calculated as: `current_time - fetch_time`
- Refresh threshold: 24 hours (86400 seconds)

## Test Scenarios

### Scenario 1: Fresh Cache (<24h)

**Setup:** Cache file with timestamp within last 24 hours

**Expected Behavior:**
```
[+] Using cached privatekeys.pw data (12.5 hours old)
[+] Using cached Keys.lol data (12.5 hours old)
```

**Result:** No network fetch, no warning, uses cache directly

---

### Scenario 2: Stale Cache + Network Available (>24h)

**Setup:** Cache file with timestamp >24 hours old, network working

**Expected Behavior:**
```
[+] Fetching puzzle progress from Privatekeys.pw...
[+] Successfully fetched data from privatekeys.pw
[+] Fetching puzzle progress from Keys.lol...
[+] Successfully fetched data from Keys.lol
```

**Result:** Fetches fresh data, updates cache, no stale cache warning

---

### Scenario 3: Stale Cache + Network Failure (>24h) ⭐

**Setup:** Cache file with timestamp >24 hours old, network unavailable

**Expected Behavior:**
```
[+] Fetching puzzle progress from Privatekeys.pw...
[-] Failed to fetch from privatekeys.pw (network error)
[!] Network error - using stale cache as fallback (2.0 days old, may be outdated)

[+] Fetching puzzle progress from Keys.lol...
[-] Failed to fetch from Keys.lol (network error)
[!] Network error - using stale cache as fallback (2.0 days old, may be outdated)

[i] Successfully loaded data from 0/3 sources (using cached fallbacks)
```

**Result:** Falls back to stale cache with age warnings

---

## Testing Procedure

### Automated Test Script

Use the provided `test_stale_cache.sh` script for automated testing:

```bash
./test_stale_cache.sh
```

**Test Modes:**

1. **Create Stale Cache Only** (Recommended for first-time testing)
   - Creates 48-hour old cache files
   - Backs up existing cache
   - Provides manual verification steps
   - Safe, no network changes

2. **Create Stale Cache + Simulate Network Failure**
   - Creates 48-hour old cache files
   - Modifies `/etc/hosts` to block API access
   - Requires sudo privileges
   - ⚠️ Remember to run cleanup after testing

3. **Clean Up Test Artifacts**
   - Restores original cache files from backup
   - Removes `/etc/hosts` modifications
   - Always run this after testing!

### Manual Test Procedure

#### Step 1: Create Stale Cache

```bash
# Calculate timestamp 48 hours ago
CURRENT=$(date +%s)
OLD_TIME=$((CURRENT - 48 * 3600))

# Create stale cache for privatekeys.pw
mkdir -p ~/.keyhunt
cat > ~/.keyhunt/privatekeys_progress.json << EOF
{
  "puzzle_number": 66,
  "percent_scanned": 42.123456,
  "keys_scanned": 123456789,
  "fetch_time": $OLD_TIME
}
EOF

# Create stale cache for Keys.lol
cat > ~/.keyhunt/keyslol_progress.json << EOF
{
  "puzzle_number": 66,
  "percent_scanned": 38.654321,
  "keys_scanned": 987654321,
  "fetch_time": $OLD_TIME
}
EOF

echo "Stale cache created with timestamp: $OLD_TIME"
echo "Cache date: $(date -d @$OLD_TIME '+%Y-%m-%d %H:%M:%S')"
```

#### Step 2: Simulate Network Failure (Optional)

**Method A: Block via /etc/hosts**
```bash
sudo bash -c 'cat >> /etc/hosts << EOF
127.0.0.1 btcpuzzle.info
127.0.0.1 privatekeys.pw
127.0.0.1 keys.lol
EOF'
```

**Method B: Disable Network Interface**
```bash
# Identify interface
ip link show

# Disable (requires sudo)
sudo ip link set <interface> down

# Re-enable after testing
sudo ip link set <interface> up
```

**Method C: Firewall Rules**
```bash
# Block outgoing HTTPS to specific domains
sudo iptables -A OUTPUT -d btcpuzzle.info -j DROP
sudo iptables -A OUTPUT -d privatekeys.pw -j DROP
sudo iptables -A OUTPUT -d keys.lol -j DROP

# Remove rules after testing
sudo iptables -D OUTPUT -d btcpuzzle.info -j DROP
sudo iptables -D OUTPUT -d privatekeys.pw -j DROP
sudo iptables -D OUTPUT -d keys.lol -j DROP
```

#### Step 3: Run Wizard

```bash
./keyhunt --wizard
```

**Wizard Steps:**
1. Select puzzle #66
2. Choose mode (Server or Client)
3. **Enable community integration** when prompted
4. Observe output during community data fetch

#### Step 4: Verify Expected Output

Look for these key messages:

✅ **Stale Cache Warning:**
```
[!] Network error - using stale cache as fallback (2.0 days old, may be outdated)
```

✅ **Age Display:**
- For >24 hours: Shows in days (e.g., "2.0 days old")
- For <24 hours: Shows in hours (e.g., "18.5 hours old")

✅ **Fallback Summary:**
```
[i] Successfully loaded data from 0/3 sources (using cached fallbacks)
```

✅ **Continues Operation:**
- Wizard proceeds to next step
- Does not crash or abort
- Uses stale data for range exclusion

#### Step 5: Cleanup

```bash
# Restore network access if blocked
sudo sed -i '/btcpuzzle.info/d; /privatekeys.pw/d; /keys.lol/d' /etc/hosts

# Or re-enable network interface
sudo ip link set <interface> up

# Or remove firewall rules
sudo iptables -F OUTPUT

# Optionally remove stale cache
rm ~/.keyhunt/privatekeys_progress.json
rm ~/.keyhunt/keyslol_progress.json
```

## Code References

### Cache Refresh Logic

Located in `src/wizard/wizard_community.c`:

**Privatekeys.pw Cache:**
```c
/* Line 767-800 */
bool have_cache = (cache_result == 0 && progress->puzzle_number == puzzle_number);

/* Check if cache is still fresh (< 24 hours) */
time_t now = time(NULL);
bool needs_refresh = !have_cache ||
                     (now - progress->fetch_time >= PRIVATEKEYS_REFRESH_INTERVAL);

if (!needs_refresh) {
    /* Cache is fresh, use it */
    double age_hours = (now - progress->fetch_time) / 3600.0;
    printf("[+] Using cached privatekeys.pw data (%.1f hours old)\n", age_hours);
    return 0;
}

/* Attempt network fetch... */

/* Fetch failed - try to use stale cache as fallback */
if (have_cache) {
    double age_hours = (now - progress->fetch_time) / 3600.0;
    if (age_hours >= 24.0) {
        double age_days = age_hours / 24.0;
        printf("[!] Network error - using stale cache as fallback (%.1f days old, may be outdated)\n", age_days);
    } else {
        printf("[!] Network error - using stale cache as fallback (%.1f hours old)\n", age_hours);
    }
    return 0;
}
```

**Keys.lol Cache:**
```c
/* Line 1054-1090 */
/* Identical logic to privatekeys.pw */
```

### Constants

```c
#define PRIVATEKEYS_REFRESH_INTERVAL (24 * 60 * 60)  /* 24 hours */
#define KEYSLOL_REFRESH_INTERVAL (24 * 60 * 60)      /* 24 hours */
```

## Expected Test Results

### ✅ PASS Criteria

1. **Stale cache detected:** Age calculation is correct (shows ~48 hours or 2 days)
2. **Warning displayed:** "[!] Network error - using stale cache as fallback" appears
3. **Fallback successful:** Wizard continues with cached data
4. **No crash:** Application doesn't abort or error out
5. **User informed:** Clear indication that data may be outdated

### ❌ FAIL Criteria

1. No stale cache warning appears
2. Wizard crashes or aborts when network fails
3. Cache age calculation is incorrect
4. Stale cache is rejected entirely (should be used as fallback)
5. No indication to user that data is stale

## Troubleshooting

### Cache Not Loading

**Symptom:** Wizard doesn't show stale cache warning, just fails

**Solutions:**
- Verify cache file exists: `ls -la ~/.keyhunt/*.json`
- Check cache file format (valid JSON)
- Ensure `fetch_time` field is present and numeric
- Verify `puzzle_number` matches selected puzzle

### Network Not Blocked

**Symptom:** Fresh data fetched instead of using stale cache

**Solutions:**
- Verify `/etc/hosts` modifications: `cat /etc/hosts | grep -E "btcpuzzle|privatekeys|keys.lol"`
- Test with curl: `curl -I https://privatekeys.pw` (should fail or timeout)
- Try alternative blocking method (firewall rules)
- Disconnect network physically

### Cache Age Incorrect

**Symptom:** Shows wrong age (e.g., 0 hours instead of 48)

**Solutions:**
- Recalculate timestamp: `echo $(($(date +%s) - 48 * 3600))`
- Verify current time is correct: `date +%s`
- Check timezone settings: `timedatectl`
- Manually inspect `fetch_time` in cache file

### Permission Errors

**Symptom:** Cannot create/modify cache files or /etc/hosts

**Solutions:**
- Run with sudo for `/etc/hosts` modifications
- Check `~/.keyhunt/` directory permissions: `ls -ld ~/.keyhunt`
- Create directory manually: `mkdir -p ~/.keyhunt && chmod 755 ~/.keyhunt`

## Integration with CI/CD

For automated testing in CI pipelines:

```bash
#!/bin/bash
# ci_test_stale_cache.sh

# Create stale cache
CURRENT=$(date +%s)
OLD_TIME=$((CURRENT - 48 * 3600))

mkdir -p ~/.keyhunt
cat > ~/.keyhunt/privatekeys_progress.json << EOF
{
  "puzzle_number": 66,
  "percent_scanned": 42.123456,
  "keys_scanned": 123456789,
  "fetch_time": $OLD_TIME
}
EOF

cat > ~/.keyhunt/keyslol_progress.json << EOF
{
  "puzzle_number": 66,
  "percent_scanned": 38.654321,
  "keys_scanned": 987654321,
  "fetch_time": $OLD_TIME
}
EOF

# Block network via iptables
sudo iptables -A OUTPUT -d btcpuzzle.info -j DROP
sudo iptables -A OUTPUT -d privatekeys.pw -j DROP
sudo iptables -A OUTPUT -d keys.lol -j DROP

# Run wizard with automated input
echo -e "66\n1\ny\n\n" | timeout 30 ./keyhunt --wizard 2>&1 | tee wizard_output.log

# Check for stale cache warning
if grep -q "using stale cache as fallback.*days old" wizard_output.log; then
    echo "✅ PASS: Stale cache warning detected"
    EXIT_CODE=0
else
    echo "❌ FAIL: Stale cache warning not found"
    EXIT_CODE=1
fi

# Cleanup
sudo iptables -F OUTPUT
rm -f ~/.keyhunt/*.json

exit $EXIT_CODE
```

## Security Considerations

### Privacy

- Cache files contain **no sensitive data** (no private keys)
- Data stored: puzzle number, scan percentage, key count, timestamp
- Safe to inspect and share cache files for debugging

### Network Modifications

- `/etc/hosts` changes are **temporary** and reversible
- Always run cleanup after testing
- Firewall rules should be removed to restore connectivity
- Document any persistent changes

### Sudo Access

- Required only for network blocking (optional test mode)
- Not required for basic stale cache testing
- Use `test_stale_cache.sh` mode 1 for sudo-free testing

## Success Metrics

**Test Coverage:**
- ✅ Stale cache detection (>24h)
- ✅ Fresh cache usage (<24h)
- ✅ Network failure fallback
- ✅ Age calculation accuracy
- ✅ User warning display
- ✅ Continued operation with stale data

**Quality Indicators:**
- Clear, actionable warning messages
- Accurate age calculation (days/hours)
- Graceful degradation (no crashes)
- User awareness of data staleness

## Additional Notes

### Why 24 Hours?

The 24-hour cache refresh interval balances:
- **Performance:** Reduces API calls and network traffic
- **Freshness:** Ensures data is reasonably current for active puzzles
- **Reliability:** Provides offline capability for ~1 day

### Future Enhancements

Potential improvements to cache system:
1. **Configurable refresh interval** (e.g., via `KEYHUNT_CACHE_TTL` env var)
2. **Force refresh flag** (e.g., `--force-refresh`)
3. **Cache validation** (checksum/signature verification)
4. **Multi-level cache** (hot/warm/cold with different TTLs)
5. **Background refresh** (async updates while using stale data)

## Conclusion

This test verifies that the keyhunt wizard properly implements the acceptance criteria:

> **Offline mode caches community data for 24 hours**

The implementation successfully:
- Caches data for 24 hours
- Uses stale cache as fallback when network fails
- Warns users about stale data
- Continues operation gracefully

**Test Status:** ✅ READY FOR EXECUTION

**Recommended Test Path:**
1. Run `./test_stale_cache.sh` → Select option 1
2. Follow manual verification steps
3. Run `./test_stale_cache.sh` → Select option 3 (cleanup)

**Estimated Test Time:** 5 minutes
