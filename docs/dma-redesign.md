# DMA buffer pool redesign (ARM/DSP memory management)

## Problem

* Fixed physical address `0xc4000100` hardcoded in firmware linker + ARM side
* `/dev/mem` mapping bypassing kernel CMA
* V4L2 USERPTR into DSP memory — driver unaware of buffer ownership
* 31 MB carveout, of which only 3.6 MB used (15 MB gap between `0xc3100000` and `0xc4000000`)
* CMA pool pinned by remoteproc entire lifetime, unavailable to other devices

## New architecture

```
┌─────────────────────────────────────────────────────────────┐
│  ARM side (trikRuntime)                                     │
│  CameraManager → V4L2 MMAP → CMA buffer                    │
│       ↓ phys addr (EXPBUF + trik-buf.ko)                    │
│  DspServer → MessageQ STEP → { dsp_in_buffer = phys }     │
│       ↓                                                     │
└─────────────────────────────────────────────────────────────┘
                          ↓ phys addr + size
┌─────────────────────────────────────────────────────────────┐
│  DSP firmware (C674x)                                       │
│  trik_handle_step:                                          │
│    in_buff = msg->dsp_in_buffer;   // per-frame phys addr   │
│    Cache_inv(in_buff, size);                                │
│    trik_run_cv_algorithm(...);                              │
└─────────────────────────────────────────────────────────────┘
```

## DSP firmware changes (trikDsp/trik-media-sensors)

### config.bld

```diff
- base: 0xC4000000, len: 0x1000000,
+ base: 0xC3100000, len: 0x800000,
```

### rsc_table_omapl138.h

```diff
- #define DATA_SIZE (SZ_1M * 31)
+ #define DATA_SIZE (SZ_1M * 8)
```

### Dsp.cfg

```diff
- Cache.MAR192_223 = 0x00000010;  // only MAR196 cached
+ Cache.MAR192_223 = 0x00000030;  // MAR196+197 cached
```

### dsp_server.c

```diff
- int8_t in_buff[TRIK_INPUT_TOTAL][BUFFER_SIZE];   // static array
+ // in_buff removed — per-frame phys addr comes in STEP message

- in_buffer.start = in_buff[res->buffer_idx];       // indexed lookup
+ in_buffer.start = res->dsp_in_buffer;             // direct phys addr
+ in_buffer.length = res->dsp_frame_size;
```

### msg.h (protocol)

```diff
struct trik_res_step_msg {
    struct trik_msg header;
-   uint32_t buffer_idx;          // flat index
+   void* dsp_in_buffer;          // phys addr from CMA
+   uint32_t dsp_frame_size;      // frame size
    ...
};
```

## trikRuntime changes

### CameraManager — removed

| Removed | Reason |
|---|---|
| `mapInputRegion()` | `/dev/mem` bypass no longer needed |
| `mInputMap` (`MappedMemory`) | RAII /dev/mem owner gone |
| `setUserPtrBuffers()` interface + impl | MMAP only from now on |
| `inputRegion` (PortInfo, Entry) | DSP regions concept obsolete |
| `inputBuffersPerRegion` / `dspInputRegions` / `dspInputBufferTotal` | Static geometry removed |
| `inputBufferBase` (DspChannel) | No more indexed in_buff |
| USERPTR paths in videoDeviceFileBase | MMAP only |

### VideoDeviceFileInterface — added

```cpp
virtual uint32_t bufferPhys(uint32_t bufferIdx) const;
```

Implemented by `VideoDeviceFileBase` using `VIDIOC_EXPBUF` + `/dev/trik-buf` ioctl.
Trik-buf fd is opened once in `allocateBuffers()`, cached in `mTrikBufFd`, and closed in `freeBuffers()`.

### Token and Frame

```diff
struct Token {
    ...
    uint32_t bufferIdx = 0;    // kept for V4L2 QBUF
+   uint32_t bufferPhys = 0;   // CMA phys addr for DSP
    ...
};
```

```diff
- uint32_t bufferIndex() const;
+ uint32_t bufferPhys() const;
```

### dspFramePipeline

```diff
- channel.inputBufferBase = info.inputRegion * info.inputBuffersPerRegion;
- mDsp->processFrame(channel, out, frame.bufferIndex());
+ mDsp->processFrame(channel, out, frame.bufferPhys());
```

## Kernel module — trik-buf.ko

* Path: `drivers/misc/trik-buf.c`
* `/dev/trik-buf` misc device with `TRIK_BUF_GET_PHYS` ioctl
* Takes a dma-buf fd (from `VIDIOC_EXPBUF`), returns the physical address via `dma_buf_map_attachment`
* Module auto-loads on boot or via init script

## Device Tree changes (trikboard.dts)

```dts
dsp_memory_region: dsp-memory@c3000000 {
    reg = <0xc3000000 0x200000>;   // 2 MB (was 32)
};

linux,cma@c5000000 {
    compatible = "shared-dma-pool";
    reg = <0xc5000000 0x2000000>;  // 32 MB default CMA
    linux,cma-default;
};
```

## Memory layout after redesign

```
0xC3000000 ─── 0xC30C0000   IPC vrings + buffer pools  (non-cached)
0xC3100000 ─── 0xC3900000   DSP carveout 8 MB (code/stack/heap, cached)
0xC5000000 ─── 0xC7000000   Default CMA 32 MB (V4L2 buffers, cached)
```

## Flow (zero-copy)

1. `CameraManager` opens V4L2 device → MMAP (9 buffers from CMA)
2. `VideoDeviceFileBase::allocateBuffers()` → EXPBUF + trik-buf → caches `bufferPhys[]`
3. VPIF DMA writes frame into CMA buffer at `phys` (V4L2 transparent)
4. `onDeviceFrame()` stores `bufferPhys` in Token
5. `DspServer::processFrame(phys, ...)` → MessageQ STEP with `dsp_in_buffer=phys`
6. DSP does `Cache_inv(phys, size)`, processes frame, returns result
7. Frame released → V4L2 QBUF → buffer reused