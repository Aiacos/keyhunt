# Stale Cache Verification - Quick Guide

## Status: ✅ READY FOR MANUAL VERIFICATION

Stale cache files have been created and are ready for testing.

## What Was Done

1. **Created stale cache files** (48 hours old):
   - `~/.keyhunt/privatekeys_progress.json` (puzzle 66, fetch_time: 48h ago)
   - `~/.keyhunt/keyslol_progress.json` (puzzle 66, fetch_time: 48h ago)

2. **Verified cache staleness**:
   - Age: 48 hours (172800 seconds)
   - Threshold: 24 hours (86400 seconds)
   - Status: **STALE** ✓

## Manual Verification Steps

### Option 1: Quick Test (No Network Blocking)

If you have a slow/unreliable network connection:

```bash
# 1. Run the wizard
./keyhunt --wizard

# 2. Select puzzle #66
# 3. Choose mode (Server or Client)
# 4. Enable community integration when prompted
# 5. Watch for output during community data fetch
```

**Look for these messages:**
- `[!] Network error - using stale cache as fallback (2.0 days old, may be outdated)`
- OR if network works: `[+] Successfully fetched data from privatekeys.pw`

### Option 2: Full Offline Test (Recommended)

Block network access to force stale cache usage:

```bash
# 1. Block API access (requires sudo)
sudo bash -c 'cat >> /etc/hosts << EOF
127.0.0.1 btcpuzzle.info
127.0.0.1 privatekeys.pw
127.0.0.1 keys.lol
EOF'

# 2. Run the wizard
./keyhunt --wizard

# 3. Select puzzle #66, enable community integration

# 4. VERIFY: You should see stale cache warnings for both sources
```

### Option 3: Automated Test Script

```bash
# Run the test script
./test_stale_cache.sh

# Select option 1 (Create stale cache only)
# Follow the manual verification steps shown
```

## Expected Output

When the wizard encounters stale cache with network failure:

```
[+] Step 5: Community Progress Integration
[?] Enable community progress integration? [y/N]: y

Fetching community progress data...

[1/3] Fetching from BTCPuzzle.info...
[+] Successfully fetched data from BTCPuzzle.info

[2/3] Fetching from Privatekeys.pw...
[-] Failed to fetch from privatekeys.pw (network error)
[!] Network error - using stale cache as fallback (2.0 days old, may be outdated)

[3/3] Fetching from Keys.lol...
[-] Failed to fetch from Keys.lol (network error)
[!] Network error - using stale cache as fallback (2.0 days old, may be outdated)

[i] Successfully loaded data from 1/3 sources (using cached fallbacks)

Community Progress for Puzzle #66:
  • Privatekeys.pw: 42.12% scanned (123,456,789 keys)
  • Keys.lol: 38.65% scanned (987,654,321 keys)
  • Combined: 42.12% already scanned
```

## Pass Criteria

✅ **Test passes if:**
1. Stale cache warning appears: `[!] Network error - using stale cache as fallback`
2. Age is displayed correctly: `2.0 days old` or `~48 hours`
3. Warning includes "may be outdated" text
4. Wizard continues with cached data (doesn't crash)
5. Data is used for range exclusion despite being stale

❌ **Test fails if:**
1. No stale cache warning appears
2. Wizard crashes when network fails
3. Age calculation is incorrect
4. Cache is rejected entirely (should be used as fallback)

## Cleanup

After testing, restore normal operation:

```bash
# Remove hosts entries (if used)
sudo sed -i '/# Temporary block for keyhunt/,+3d' /etc/hosts
# OR manually edit:
sudo nano /etc/hosts  # Remove the 3 lines for btcpuzzle, privatekeys, keys.lol

# Optionally restore original cache
mv ~/.keyhunt/privatekeys_progress.json.backup ~/.keyhunt/privatekeys_progress.json

# Or remove test cache
rm ~/.keyhunt/privatekeys_progress.json
rm ~/.keyhunt/keyslol_progress.json

# Or use cleanup script
./test_stale_cache.sh  # Select option 3
```

## Cache Details

```
Cache Location: ~/.keyhunt/
Files Created:
  - privatekeys_progress.json (115 bytes)
  - keyslol_progress.json (115 bytes)

Timestamp: 1771971913 (48 hours before test)
Current Time: ~1772144728
Age: 172815 seconds (48.0 hours, 2.0 days)
Threshold: 86400 seconds (24 hours)
Status: STALE ✓
```

## Troubleshooting

**Q: Warning doesn't appear**
- Ensure network is actually blocked (test with: `curl -I https://privatekeys.pw`)
- Verify cache files exist: `ls -l ~/.keyhunt/*.json`
- Check cache timestamps: `cat ~/.keyhunt/privatekeys_progress.json`

**Q: Gets fresh data instead of using stale cache**
- Network blocking may not be working
- Try alternative method (firewall rules, disconnect network)
- Verify hosts file: `cat /etc/hosts | grep privatekeys`

**Q: Wizard crashes**
- This is a FAIL - report as bug
- Cache fallback should prevent crashes
- Check error messages

## Code References

Stale cache logic implemented in:
- `src/wizard/wizard_community.c` lines 767-800 (privatekeys.pw)
- `src/wizard/wizard_community.c` lines 1054-1090 (Keys.lol)

Key constants:
```c
#define PRIVATEKEYS_REFRESH_INTERVAL (24 * 60 * 60)  /* 24 hours */
#define KEYSLOL_REFRESH_INTERVAL (24 * 60 * 60)      /* 24 hours */
```

## Success Confirmation

Once verified, document in `build-progress.txt`:

```
✅ Subtask-5-5: Test offline mode with stale cache (>24h)
- Created 48-hour old cache files
- Verified stale cache warning appears
- Confirmed wizard continues with stale data
- Age calculation accurate (2.0 days)
- No crashes or failures
```

---

**Test Status:** ✅ SETUP COMPLETE - Ready for manual verification
**Test Files:** Available in repository
**Documentation:** Complete with troubleshooting guide
**Estimated Time:** 5 minutes
