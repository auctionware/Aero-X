# AeroX 

A high-performance, lightweight, custom-built C++ RandomX stratum mining client designed for localized P2Pool mining on Windows. 

AeroX strips away the heavy configuration bloat and multi-algorithm overhead of traditional miners (like XMRig), focusing strictly on bare-metal CPU optimization, rapid dataset initialization, and zero-latency local pool communication.

---

## Features

- **Laser-Focused Architecture:** Purpose-built exclusively for the RandomX algorithm (Monero).
- **Zero Configuration Bloat:** No complex JSON configuration files, fallback server lists, or embedded web dashboards. 
- **Optimized Performance:** Compiled with explicit AVX2 hardware acceleration and targeted core affinity bindings.
- **P2Pool Ready:** Out-of-the-box TCP stratum communication protocol tuned to interface seamlessly with local P2Pool instances (`127.0.0.1:3333`).
- **Standalone Binary:** Statically links core mining logic for clean deployment and portability.

---

## System Requirements

- **OS:** Windows 10 / 11 (64-bit)
- **CPU:** x86_64 processor with AVX2 instruction set support
- **RAM:** Minimum 3 GB available system memory (required to load the ~2.08 GiB RandomX dataset)
- **Toolchain:** Visual Studio (MSVC) Developer Command Prompt for building from source

---

## Building from Source

To compile `aerox.exe` natively on Windows using MSVC:

1. Open **x64 Native Tools Command Prompt for VS**.
2. Navigate to the directory containing `aerox_windows.cpp` and your RandomX source/library files.
3. Run the following compilation command:

```cmd
cl /O2 /MD /arch:AVX2 /EHsc aerox_windows.cpp ^
   /I"C:\path\to\RandomX\src" ^
   "C:\path\to\RandomX\build\Release\randomx.lib" ^
   ws2_32.lib advapi32.lib ^
   /link /OUT:aerox.exe
```

---

## Usage Guide

AeroX is designed to work in tandem with a locally running Monero node and P2Pool instance.

### Step 1: Start Your Monero Daemon (`monerod`)
Ensure your local node is fully synced and has ZeroMQ enabled:
```cmd
monerod.exe --zmq-pub tcp://127.0.0.1:18083
```

### Step 2: Launch P2Pool
Start P2Pool in a separate terminal window, pointing it to your wallet address and choosing your network chain (e.g., `--mini` for smaller, more frequent payouts):
```cmd
p2pool.exe --wallet YOUR_MONERO_WALLET_ADDRESS --mini
```

### Step 3: Run AeroX
Once P2Pool is active and listening on port `3333`, launch your compiled client:
```cmd
aerox.exe
```

AeroX will automatically initialize the RandomX dataset in memory, establish a TCP socket handshake with your local P2Pool instance, bind worker threads, and begin submitting shares.

---

## Project Structure

```text
├── aerox_windows.cpp   # Main client source code (Stratum client, VM loop, threading)
├── README.md           # Project documentation
└── README.pdf          # Formatted PDF documentation release
```

---

## License

Distributed under the MIT License. See LICENSE for more information.
This project is open-source and intended for educational, personal, and experimental mining optimization use.
***Disclaimer: Cryptocurrency mining consumes electricity and hardware resources. Please ensure you monitor your hardware temperatures and power usage properly.***
