#!/usr/bin/env python3
"""Synthetic isolated RC-20 Magnetic/cFlutter kernel probe (Linux x86_64).

Maps the hash-pinned original image and reconstructs constructor/prepare fields.
Never invokes original constructors, prepare allocations, scripts or plugin
entry points. Model callbacks supply explicit cached parameters; visualization
is disabled. This is a kernel experiment, not complete plugin hosting.
"""
import ctypes as C
import json
import math
import struct
from native_bitcolor_probe import NativeBitColor, HASH


class NativeMagnetic(NativeBitColor):
    PARAMETERS = {
        'enable': 0x108, 'depth': 0x140, 'depth_steer': 0x178,
        'stereo_phase': 0x250, 'wear_gain': 0x288, 'wear_pan': 0x2c0,
        'wear_stereo': 0x2f8, 'wear_depth': 0x330, 'dropout_steer': 0x368,
        'dropout_amount': 0x3a0, 'dropout_min': 0x3d8, 'dropout_max': 0x410,
    }

    def __init__(self, sample_rate=48000., depth=.5, depth_steer=.5, rate=.5,
                 stereo_phase=0., wear_gain=0., wear_pan=0., wear_stereo=0.,
                 wear_depth=.5, dropout_steer=0., dropout_amount=0.,
                 dropout_min=.1, dropout_max=.25, enable=1., normalized=True):
        super().__init__()  # Reuse the hash-pinned image loader and native Scale helpers.
        self.state = C.create_string_buffer(0x700)
        self.callbacks = []
        self.buffers = []
        self.values = {}
        self.sample_rate = self.float32(sample_rate)
        self.normalized = bool(normalized)
        self.guard = C.c_uint64(0x3141592653589793)
        struct.pack_into('<Q', self.image, 0x73a228, C.addressof(self.guard))
        self.libc = C.CDLL(None)
        for stub, name, library in [(0x578c12, 'memcpy', self.libc), (0x578c96, 'pow', self.libm)]:
            slot = stub + 6 + struct.unpack_from('<i', self.image, stub + 2)[0]
            struct.pack_into('<Q', self.image, slot, C.cast(getattr(library, name), C.c_void_p).value)
        struct.pack_into('<i', self.state, 0xf4, 2)
        self.f32(0xf8, self.sample_rate)
        inverse = self.float32(1. / self.sample_rate)
        self.f32(0x458, inverse); self.f32(0x45c, 1)
        self.f32(0x6b0, .015625)
        struct.pack_into('<i', self.state, 0x6b4, -1)
        # Constructor-owned previous control values, distinct from prepare fields.
        self.state[0x4b8:0x4c8] = self.image[0x5f5ac0:0x5f5ad0]
        for off in [0x4d0, 0x4f0]:
            struct.pack_into('<Q', self.state, off, self.base + 0x135330)
            self.f32(off + 8, -.5)
            self.f32(off + 0x10, .25)
            self.f32(off + 0x1c, inverse)
        def constant(address):
            return struct.unpack_from('<f', self.image, address)[0]
        cutoff = self.float32(self.sample_rate * constant(0x5f5838))
        cutoff = self.float32(cutoff * constant(0x5e1228))
        cutoff = self.float32(cutoff * constant(0x5ea9b0))
        exponent = self.float32(constant(0x5f0dcc) * cutoff)
        exponent = self.float32(exponent / constant(0x5e21e4))
        feed = self.float32(1. - self.exp32(exponent))
        for off in [0x460, 0x474, 0x488]:
            struct.pack_into('<I', self.state, off, 0x3c8637bd)
            self.f32(off + 4, 0)
            self.f32(off + 8, feed)
            self.f32(off + 12, cutoff)
            self.state[off + 16] = b'\x01'
        self.model = C.create_string_buffer(8)
        self.model_vtable = C.create_string_buffer(0x40)
        values = self.values
        getter = C.CFUNCTYPE(C.c_float, C.c_void_p, C.c_int)(lambda _, key: values[key])
        setter = C.CFUNCTYPE(None, C.c_void_p, C.c_int, C.c_float)(lambda *_: None)
        self.callbacks.extend([getter, setter])
        struct.pack_into('<Q', self.model_vtable, 0x38, C.cast(getter, C.c_void_p).value)
        struct.pack_into('<Q', self.model_vtable, 0x30, C.cast(setter, C.c_void_p).value)
        struct.pack_into('<Q', self.model, 0, C.addressof(self.model_vtable))
        for off in self.PARAMETERS.values():
            struct.pack_into('<Q', self.state, off + 0x18, C.addressof(self.model))
            struct.pack_into('<i', self.state, off + 0x28, off)
        # Two 64-float visualization scratch buffers and safe dummy publication stores.
        for off in [0x510, 0x530]:
            data = (C.c_float * 64)()
            pointers = (C.c_void_p * 1)(C.addressof(data))
            self.buffers.extend([data, pointers])
            struct.pack_into('<QQii', self.state, off, C.addressof(data), C.addressof(pointers), 1, 1)
        for off in [0x550, 0x600]:
            struct.pack_into('<Q', self.state, off + 0x18, C.addressof(self.model))
            self.state[off + 0xa8] = b'\x01'
            struct.pack_into('<i', self.state, off + 0x88, 0x7fffffff)
        self.set(depth=depth, depth_steer=depth_steer, rate=rate, stereo_phase=stereo_phase,
                 wear_gain=wear_gain, wear_pan=wear_pan, wear_stereo=wear_stereo,
                 wear_depth=wear_depth, dropout_steer=dropout_steer, dropout_amount=dropout_amount,
                 dropout_min=dropout_min, dropout_max=dropout_max, enable=enable)
        # Prepare invalidates these wrapper caches. Getter callbacks above let the
        # original code refill them, including the first-batch stereo phase reset.
        for off in [0x244, 0x16c, 0x2b4, 0x2ec, 0x27c]:
            self.state[off] = b'\x00'
        self.process_fn = C.CFUNCTYPE(None, C.c_void_p, C.c_void_p)(self.base + 0x353c30)
        self.smoothing_fn = C.CFUNCTYPE(None, C.c_void_p)(self.base + 0x353c00)

    def set(self, **values):
        for name, value in values.items():
            value = self.float32(value)
            if name == 'rate':
                physical = self.scale(value, 6, 30, 20) if self.normalized else value
                self.param(0x1b0, value if self.normalized else .5, physical)
                self.metadata(0x1b0, 6, 30, 20)
                self.state[0x244] = b'\x00'
            elif name in self.PARAMETERS:
                off = self.PARAMETERS[name]
                changed = self.values.get(off) != value
                # Preserve prepare's dirty depth/phase caches when a caller
                # republishes an unchanged value before the first render.
                if not changed and name in ('depth', 'stereo_phase'):
                    continue
                self.values[off] = value
                self.wrap(off, value)
                if changed and name in ('depth', 'stereo_phase'):
                    self.state[off + 0x2c] = b'\x00'
            else:
                raise ValueError(name)

    def reset_smoothing(self):
        """Observed native reset: dirty flags only, conditional on active history."""
        self.smoothing_fn(self.state)

    def reset(self):
        """Alias for the original resetSmoothing; no synthetic phase/history clear."""
        self.reset_smoothing()


if __name__ == '__main__':
    native = NativeMagnetic()
    for block in range(8):
        output = native.process([.5 * math.sin((block * 64 + i) * .1) for i in range(64)],
                                [.4 * math.cos((block * 64 + i) * .13) for i in range(64)])
        assert all(math.isfinite(x) for channel in output for x in channel)
    print(json.dumps(dict(scope='isolated Magnetic kernel with synthetic prepare/model state; visualization disabled',
                          reference_sha256=HASH, left=output[0][:8], right=output[1][:8])))
