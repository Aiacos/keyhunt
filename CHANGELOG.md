# Version 0.3.0 Distributed Mode (Unreleased)

## Distributed Computing
- **Coordinator/Worker Architecture**: TCP-based distributed mode on port 7777
- **JSON Protocol**: Length-prefixed JSON messages for all communication
- **Message Types**: register, welcome, request_work, work_assignment, work_done, heartbeat, found
- **Hardware Reporting**: Workers report CPU cores/threads/name, GPU name/memory/speed
- **Heartbeat System**: Server-configurable heartbeat interval in welcome message
- **Work Synchronization**: Hex-encoded range_start/range_end in work assignments
- **Key Found Notification**: Immediate reporting with private_key and address

## Interactive Wizard (`--wizard` or `-W`)
- **5-Step Setup Flow**: Puzzle selection, mode, server config, search config, community sync
- **Dual-Mode Operation**: Server (coordinator + local worker) and Client (worker only)
- **JSON Configuration**: Saves to `keyhunt_wizard.json` for session resume

## Community Progress Integration
- **BTCPuzzle.info Integration**: Downloads puzzle database and scanned ranges
- **Privatekeys.pw Cloud Search**: Fetches community scanning progress percentage
- **24-Hour Caching**: Stored in `~/.keyhunt/privatekeys_progress.json`
- **Search Optimization**: Sequential mode skips scanned regions, random mode avoids them

## Hardware Auto-Detection (`sysinfo.c`)
- **CPU Detection**: Physical cores, logical threads, model name, cache sizes (L1/L2/L3)
- **SIMD Detection**: AVX2, AVX-512 (F/DQ/BW/VL), SHA-NI
- **GPU Detection**: NVIDIA via NVML (count, name, VRAM, compute capability)
- **Memory Detection**: Total, available, and free RAM
- **Auto-Tuning**: Optimal threads, batch size, N value, K factor based on hardware

## GPU Performance Optimizations
- **GPU Autotune** (`gpu/gpu_autotune.c`): Runtime kernel parameter optimization
- **Multi-GPU Scheduler** (`gpu/multi_gpu_scheduler.c`): Adaptive work distribution across GPUs
- **Async Pipeline** (`gpu/async_pipeline.c`): Triple-buffered GPU operations for max utilization
- **Memory Pool** (`util/mempool.c`): Fast arena-style allocation with cache-line alignment

## OpenCL GPU Backend (`src/gpu/`)
- **Multi-Vendor Support**: Cross-platform acceleration for AMD (ROCm), NVIDIA, and Intel GPUs
- **Auto-Detection Script** (`build_opencl.sh`): Auto-detects ROCm, GPU architecture, and OpenCL runtime
- **Multi-Device Acceleration**: Heterogeneous GPU support with performance-weighted work distribution
- **Platform Enumeration**: Runtime discovery of all OpenCL platforms and devices (max 8 GPUs)
- **Device-Specific Tuning**: Optimized parameters per GPU architecture (RDNA 3/2, Vega, etc.)
- **ROCm Integration**: Native support for AMD GPUs via ROCm 5.0+ OpenCL implementation
- **Supported AMD GPUs**: RDNA 3 (gfx1100), RDNA 2 (gfx1030), RDNA 1 (gfx1010), Vega (gfx900)
- **Kernel Optimization**: C-style OpenCL kernels for secp256k1 ECC and SHA256+RIPEMD160 pipeline
- **Graceful Fallback**: CPU-only mode if OpenCL unavailable or initialization fails

## Parameter Validation (`parameter_validator.c`)
- **Input Validation**: Threads vs cores, N/K vs RAM, batch size alignment
- **Auto-Correction**: Prevents OOM crashes and excessive thread counts
- **User Feedback**: Visual indicators for optimal/acceptable/corrected parameters

---

# Version 0.2.230519 Satoshi Quest
- Speed x2 in BSGS mode for main version

# Version 0.2.230507 Satoshi Quest
- fixed some variables names
- fixed bug in addvanity (realloc problem with dirty memory)
- Added option -6 to skip SHA256 checksum when you read the files (Improved startup process)
- Added warning when you Endomorphism and BSGS, THEY DON'T WORK together!
- Legacy version for ARM processor and other systems
- remove pub2rmd

# Version 0.2.230430 Satoshi Quest
- fixed typos in README
- Speed counter fixed for Compress search without endomorphism check https://github.com/albertobsd/keyhunt/tree/development#Speeds

# Version 0.2.230428 Satoshi Quest
- Merge of address and rmd160 speeds
- Added option for endomorphism
- Added SAVE bloom filter and table option for adddress, rmd160, minikeys and xpoint
- Improved Makefile options
- Updated random function to use the Linux RNG with the function getrandom

# Version 0.2.211117 SSE Trick or treat ¡Beta!
- Minikeys new sequential generator and x2 times more speed
- third bloom filter check for bsgs 20% less memory usage

# Version 0.2.211031 Trick or treat ¡Beta!
- Minikeys improvements in speed
- Test to try solve the https://github.com/albertobsd/keyhunt/issues/139 issue

# Version 0.2.211026 Chocolate ¡Beta!
- Solved https://github.com/albertobsd/keyhunt/issues/130
- Minikeys new generator improvements in speed

# Version 0.2.211024 Chocolate ¡Beta!
- Ethereum support
- Double speed for rmd160 mode
- Minikeys mode support
- Stride option

# Version 0.2.211018 Chocolate ¡Beta!
- Solved some bugs: https://github.com/albertobsd/keyhunt/issues/122 https://github.com/albertobsd/keyhunt/issues/111
- Files are going to be updated automatillyca 
-- from keyhunt_bsgs_3_*.blm  to keyhunt_bsgs_4*.blm 
-- from keyhunt_bsgs_1_*.blm  to keyhunt_bsgs_5*.blm 
-- the program will notify you when time to delete the old files

# Version 0.2.211012 Chocolate ¡Beta!
- Fixed the slow bP table generation.
-- This fix make obsolete the files keyhunt_bsgs_0_*.blm 
-- please delete those files, please do:

```
rm keyhunt_bsgs_0_*.blm 
```

- Added multi vanitysearch for address mode


# Version 0.2.211007 Chocolate ¡Beta!
- BSGS improvements:
--  10x more Speed
--  new submodes for BSGS, secuential (default), backward, both, random and dance
--  automatic file generation for bloom filter file and bPtable file.
--  Good bye to bPfile.
- Memory check periodically for bloom filters and bP Table

# Version 0.1.20210420 secp256k1
- Solved Issues 49, 50 51
  See:
  https://github.com/albertobsd/keyhunt/issues/51
  https://github.com/albertobsd/keyhunt/issues/50
  https://github.com/albertobsd/keyhunt/issues/49
- Solved Issues 56 https://github.com/albertobsd/keyhunt/issues/56
- Added mutex to the bloom filter for multithread writing

# Version 0.1.20210412 secp256k1
- Full migration from libgmp to secp256k1
- Change the way for keygeneration for modes xpoint, address, and rmd160
- Improve performance for xpoint mode, now is ten times faster
- Change N variable type for modes address,rmd160 and xpoint, from uint32_t to uint64_t
- Added method pub2rmd to search publickeys of the puzzles and other legacy address (Compress publickeys only)

# Version 0.1.20210331
- Small changes to be compiled with mingw on Windows
- Changed sort functions and binary search for modes address/rmd160/xpoint, now those modes can load MAX 2^64 items
- xpoint input file now can contains Comments after the line of data
- from this version all furthers developments will be in the branch `development`

# Version 0.1.20210328
- Added a progress counter (this solve bug https://github.com/albertobsd/keyhunt/issues/18 )
- Added multithread for precalculating bP items or reading then from file
- Fixed the code to avoid warnings (this solve the issue https://github.com/albertobsd/keyhunt/issues/19)

# Version 0.1.20210322
- Added xxhash for bloomfilter this hash have better performance than murmurhash2. And it is 64 bits hash :)
- We reduce the number of items of the bPtable in ram using a second bloom filter, thanks @iceland2k14
- The ram saved space is around 80%, so we can use a bigger K value, around 4 or 5 times bigger than previous version

# Version 0.1.20210320 K*BSGS
- Solved little error with compress and uncompress new param -l. See https://github.com/albertobsd/keyhunt/issues/17
- function bsgs optimized to use a little less RAM (not related with Pfile)
- Again removed some compile warnings. See https://github.com/albertobsd/keyhunt/issues/16

# Version 0.1.20210311 K*BSGS
- Added mode rmd160, this method works two times faster than Address method. This mode can search all the altcoins


# Version 0.1.20210311 K*BSGS
- Solved some bug when the publickeys in the input file was invalid but the program keeps running with 0 publickeys
- Now publickeys can be compressed, not only uncompressed

# Version 0.1.20210306 K*BSGS
- Added K factor for BSGS
- Added bPfile.c to generate a precalculated file
- Remove unused files about keccak and sha3
- Change Bloom filter limits and % of error from 0.001 to 0.00001 in bloomfilter.

# Version 0.1.20210112 BSGS
- Added mode BSGS this work with a file with uncompressed keys
- Updated  bloom filter to allow More items

# Version 0.1.20201228
- Change Quicksort to Introsort, this solve some edge cases of quicksort.
- Introsort is avaible to keyhunt and hexcharstoraw. worst case. O(N log N).
- Aling of some output text

# Version 0.1.20201223
- Added new tool hexcharstoraw to create a raw binary file for xpoint from a text-hexadecimal file
- Added option -w to work with raw binary file, this file contains xpoint in binary format fixed to 32 bytes

# Version 0.1.20201222
- Fixed some ugly bug in the searchbinary function thanks to Ujang
- Added to stdout the vanitykeys found with -v option

# Version 0.1.20201221
- Fixed search by xpoint.
- Added -e option to skip the sort process whe the file is already sorted.
- Fixed debugcount when upto N is less than debugcount.
- Changed "-R upto" to "-R" and added "-n upto" option.

# Version 0.1.20201218
- Minor bugs fixed.

# Version 0.1.20201217
- First Release
- Thanks to all CryptoHunters to make this code possible
