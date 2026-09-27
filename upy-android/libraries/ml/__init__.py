# ml/__init__.py -- Model wrapper around this project's own `litert`
# module (litert.Environment / litert.CompiledModel / litert.TensorBuffer,
# see litert_module.cpp). This is a from-scratch implementation written
# against a behavioral spec only -- it has never seen any other `ml`
# module's source.
#
# Layering: `litert` gives raw tensor buffers (write_int8/read_int8,
# write_float/read_float, write_int/read_int, write_bool/read_bool,
# write_long/read_long) plus tensor introspection (shape/dtype/
# quantization). This module adds the higher-level, numpy-and-image-
# friendly `Model`/`predict()` convenience layer on top, matching how
# real scripts are expected to use it (see examples/ml_selftest).

import litert
from ulab import numpy as np

# A sentinel distinct from None, so `postprocess` can be REQUIRED even
# though its legitimate value is often literally `None`. A plain
# `postprocess=None` default would make an *omitted* argument
# indistinguishable from an explicitly-passed `None` -- this sentinel
# keeps them distinguishable so we can raise on "omitted" only.
_REQUIRED = object()


# ---------------------------------------------------------------------
# dtype plumbing: translate litert's own dtype strings ("int8",
# "float32", ...) to single-char typecodes matching Python's
# array/struct convention, and back out to per-dtype element size and
# saturation range (needed for quantizing ndarray inputs and for sizing
# the scratch buffer handed to a callable input).
# ---------------------------------------------------------------------

# Confirmed by spec: int8->'b', uint8->'B', int16->'h', uint16->'H',
# float32->'f'. The rest (int32/int64/uint32/uint64/bool/float64/
# float16/unknown) are not exercised by any known fixture or by the
# TensorBuffer primitives below -- extended here on a best-effort basis
# so the attribute always exists, rather than left to crash.
_DTYPE_TO_TYPECODE = {
    "int8": "b",
    "uint8": "B",
    "int16": "h",
    "uint16": "H",
    "int32": "i",
    "uint32": "I",
    "int64": "q",
    "uint64": "Q",
    "float32": "f",
    "float64": "d",
    "bool": "B",
    "float16": "f",  # no dedicated half-float typecode available
}

_ITEMSIZE = {
    "b": 1, "B": 1, "h": 2, "H": 2,
    "i": 4, "I": 4, "q": 8, "Q": 8,
    "f": 4, "d": 8,
}

# Saturation range for quantizing real-world floats into a raw integer
# tensor. Only dtypes litert can actually report a *quantization* scheme
# for are listed; float dtypes never go through this path.
_DTYPE_RANGE = {
    "int8": (-128, 127),
    "uint8": (0, 255),
    "int16": (-32768, 32767),
    "uint16": (0, 65535),
    "int32": (-2147483648, 2147483647),
    "uint32": (0, 4294967295),
}


def _dtype_to_typecode(dtype_str):
    return _DTYPE_TO_TYPECODE.get(dtype_str, "B")


def _resolve_shape(shape):
    # litert's raw tensor introspection can report a leading dimension
    # of -1 (dynamic/unresolved batch dim in the model file itself);
    # real models actually run with that dim resolved to a concrete 1
    # (confirmed, load-bearing correction from the spec).
    if shape is None:
        return None
    shape = tuple(shape)
    if shape and shape[0] == -1:
        shape = (1,) + shape[1:]
    return shape


def _num_elements(shape):
    if not shape:
        return 1
    n = 1
    for d in shape:
        n *= d
    return n


def _quantize_value(value, scale, zero_point, dtype):
    raw = round(value / scale) + zero_point
    lo_hi = _DTYPE_RANGE.get(dtype)
    if lo_hi is not None:
        lo, hi = lo_hi
        if raw < lo:
            raw = lo
        elif raw > hi:
            raw = hi
    return raw


def _channel_index(flat_index, shape, quantized_dimension, channel_count):
    # Row-major element position -> index along `quantized_dimension`,
    # used to pick the right scale/zero_point for per-channel
    # quantization (e.g. conv weights). Not exercised by any known
    # fixture (those are all per-tensor or unquantized) but implemented
    # properly rather than left as a stub.
    trailing = 1
    for d in shape[quantized_dimension + 1:]:
        trailing *= d
    return (flat_index // trailing) % channel_count


class _TensorInfo:
    """Shape/dtype/quantization bundle for one input or output tensor."""

    def __init__(self, shape, dtype, quant):
        self.shape = shape
        self.dtype = dtype  # litert's own string, e.g. "int8"
        self.typecode = _dtype_to_typecode(dtype)
        self.quant = quant  # litert's own tagged tuple, e.g. ("per_tensor", scale, zp)

    @property
    def scale(self):
        if self.quant[0] == "per_tensor":
            return self.quant[1]
        if self.quant[0] == "per_channel":
            return self.quant[2][0]  # first channel, as a scalar placeholder
        return 1.0

    @property
    def zero_point(self):
        if self.quant[0] == "per_tensor":
            return self.quant[2]
        if self.quant[0] == "per_channel":
            return self.quant[3][0]
        return 0


def _read_raw_values(buf, dtype):
    if dtype == "float32" or dtype == "float16":
        return list(buf.read_float())
    if dtype == "int8":
        raw = buf.read_int8()
        # bytes() iterates as unsigned 0..255 even though int8 tensors
        # are logically signed -- reinterpret.
        return [v - 256 if v > 127 else v for v in raw]
    if dtype == "uint8":
        return list(buf.read_int8())
    if dtype == "int32" or dtype == "uint32":
        return list(buf.read_int())
    if dtype == "int64" or dtype == "uint64":
        return list(buf.read_long())
    if dtype == "bool":
        return list(buf.read_bool())
    raise ValueError("ml: tensor dtype %r has no raw buffer primitive available" % (dtype,))


def _write_raw_values(buf, dtype, values):
    if dtype == "float32" or dtype == "float16":
        buf.write_float(values)
        return
    if dtype == "int8":
        buf.write_int8(bytes([v & 0xFF for v in values]))
        return
    if dtype == "uint8":
        buf.write_int8(bytes(values))
        return
    if dtype == "int32" or dtype == "uint32":
        buf.write_int(values)
        return
    if dtype == "int64" or dtype == "uint64":
        buf.write_long(values)
        return
    if dtype == "bool":
        buf.write_bool(values)
        return
    raise ValueError("ml: tensor dtype %r has no raw buffer primitive available" % (dtype,))


def _ulab_dtype_for(dtype_str):
    if dtype_str == "int8":
        return np.int8
    if dtype_str == "uint8":
        return np.uint8
    return np.float


def _flatten_input(obj):
    if hasattr(obj, "flatten"):
        return obj.flatten().tolist()
    if hasattr(obj, "tolist"):
        return obj.tolist()
    return list(obj)


class Model:
    def __init__(self, path, postprocess=_REQUIRED, labels=None):
        # Checked first, before the model file is even opened -- matches
        # observed device behavior (TypeError raised even for a bad
        # path, if postprocess was omitted).
        if postprocess is _REQUIRED:
            raise TypeError("'postprocess' argument required")

        self._postprocess = postprocess

        # litert.CompiledModel() already raises OSError for a bad path
        # -- let it propagate uncaught.
        self._env = litert.Environment()
        self._model = litert.CompiledModel(self._env, path)

        self._input_buffers = self._model.create_input_buffers()
        self._output_buffers = self._model.create_output_buffers()

        self._input_infos = [
            self._tensor_info(i, is_output=False) for i in range(len(self._input_buffers))
        ]
        self._output_infos = [
            self._tensor_info(i, is_output=True) for i in range(len(self._output_buffers))
        ]

        self.input_shape = tuple(info.shape for info in self._input_infos)
        self.input_dtype = tuple(info.typecode for info in self._input_infos)
        self.input_scale = tuple(info.scale for info in self._input_infos)
        self.input_zero_point = tuple(info.zero_point for info in self._input_infos)

        self.output_shape = tuple(info.shape for info in self._output_infos)
        self.output_dtype = tuple(info.typecode for info in self._output_infos)
        self.output_scale = tuple(info.scale for info in self._output_infos)
        self.output_zero_point = tuple(info.zero_point for info in self._output_infos)

        if labels is not None:
            self.labels = labels
        else:
            self.labels = self._load_labels(path)

    def _tensor_info(self, index, is_output):
        if is_output:
            shape, dtype = self._model.get_output_tensor_type(index)
            quant = self._model.get_output_tensor_quantization(index)
        else:
            shape, dtype = self._model.get_input_tensor_type(index)
            quant = self._model.get_input_tensor_quantization(index)
        return _TensorInfo(_resolve_shape(shape), dtype, quant)

    @staticmethod
    def _load_labels(path):
        dot = path.find(".")
        if dot == -1:
            return None
        labels_path = path[:dot] + ".txt"
        try:
            with open(labels_path, "r") as f:
                return [line.rstrip("\r\n") for line in f]
        except Exception:
            return None

    def close(self):
        for buf in self._input_buffers:
            buf.close()
        for buf in self._output_buffers:
            buf.close()
        self._model.close()
        self._env.close()

    # ------------------------------------------------------------
    # predict()
    # ------------------------------------------------------------

    def predict(self, inputs, callback=None):
        if len(inputs) != len(self._input_buffers):
            raise ValueError(
                "ml: predict() expected %d input(s), got %d" % (len(self._input_buffers), len(inputs))
            )

        for index, item in enumerate(inputs):
            self._write_input(index, item)

        self._model.run(self._input_buffers, self._output_buffers)

        handler = callback if callback is not None else self._postprocess
        if handler is not None:
            raw_outputs = [self._read_output(i, dequantize=False) for i in range(len(self._output_buffers))]
            return handler(self, inputs, raw_outputs)

        return [self._read_output(i, dequantize=True) for i in range(len(self._output_buffers))]

    def _write_input(self, index, item):
        if hasattr(item, "to_ndarray"):
            self._write_image_input(index, item)
        elif callable(item):
            self._write_callable_input(index, item)
        else:
            self._write_ndarray_input(index, item)

    def _write_ndarray_input(self, index, arr):
        info = self._input_infos[index]
        flat = _flatten_input(arr)

        tag = info.quant[0]
        if tag == "none":
            raw_values = flat
        elif tag == "per_tensor":
            scale, zp = info.quant[1], info.quant[2]
            raw_values = [_quantize_value(v, scale, zp, info.dtype) for v in flat]
        elif tag == "per_channel":
            _, quantized_dimension, scales, zero_points = info.quant
            channel_count = len(scales)
            raw_values = [
                _quantize_value(
                    v,
                    scales[_channel_index(i, info.shape, quantized_dimension, channel_count)],
                    zero_points[_channel_index(i, info.shape, quantized_dimension, channel_count)],
                    info.dtype,
                )
                for i, v in enumerate(flat)
            ]
        else:
            # block_wise: no real model produces this yet.
            raise ValueError("ml: block-wise quantization is not supported for predict() inputs")

        _write_raw_values(self._input_buffers[index], info.dtype, raw_values)

    def _write_image_input(self, index, img):
        info = self._input_infos[index]
        if info.typecode not in ("b", "B", "f"):
            raise ValueError(
                "ml: image inputs require an int8/uint8/float32 input tensor, got dtype %r" % (info.dtype,)
            )
        arr = img.to_ndarray(info.typecode)
        values = _flatten_input(arr)

        expected_n = _num_elements(info.shape)
        if len(values) != expected_n:
            raise ValueError(
                "ml: image does not match model input shape %r (got %d elements, expected %d)"
                % (info.shape, len(values), expected_n)
            )

        # Image pixel bytes at the matching dtype are already in the
        # tensor's expected representation by established convention --
        # no scale/zero_point quantization on top.
        _write_raw_values(self._input_buffers[index], info.dtype, values)

    def _write_callable_input(self, index, fn):
        # NOTE: litert.TensorBuffer has no accessor for a raw, zero-copy
        # view over its own native memory (no buffer-protocol support,
        # no "get pointer" method -- only the copying write_*/read_*
        # pairs). A truly zero-copy view as the spec describes is not
        # achievable with the building blocks available here. This
        # allocates a same-sized/-shaped/-dtype scratch buffer, lets the
        # callable populate *that*, then copies it into the tensor
        # afterwards -- behaviorally equivalent from the callable's
        # point of view (it still just gets a writable buffer sized and
        # typed to the input slot and fills it), at the cost of one
        # extra copy on our side.
        info = self._input_infos[index]
        if info.shape is None:
            raise ValueError("ml: cannot size a scratch buffer for an unranked input tensor")

        dtype = info.typecode
        itemsize = _ITEMSIZE.get(dtype, 1)
        n_elems = _num_elements(info.shape)
        raw = bytearray(n_elems * itemsize)
        view = memoryview(raw)
        try:
            typed_view = view.cast(dtype)
        except Exception:
            # Fallback if memoryview.cast() doesn't support this
            # typecode on this build -- caller gets a raw byte view
            # instead (only correct for 1-byte dtypes, but keeps this
            # from hard-crashing on an unsupported cast).
            typed_view = view

        fn(typed_view, info.shape, dtype)

        values = list(typed_view)
        _write_raw_values(self._input_buffers[index], info.dtype, values)

    def _read_output(self, index, dequantize):
        info = self._output_infos[index]
        raw_values = _read_raw_values(self._output_buffers[index], info.dtype)

        if not dequantize:
            arr = np.array(raw_values, dtype=_ulab_dtype_for(info.dtype))
            return arr.reshape(info.shape) if info.shape else arr

        tag = info.quant[0]
        if tag == "none":
            values = [float(v) for v in raw_values]
        elif tag == "per_tensor":
            scale, zp = info.quant[1], info.quant[2]
            values = [(v - zp) * scale for v in raw_values]
        elif tag == "per_channel":
            _, quantized_dimension, scales, zero_points = info.quant
            channel_count = len(scales)
            values = [
                (v - zero_points[_channel_index(i, info.shape, quantized_dimension, channel_count)])
                * scales[_channel_index(i, info.shape, quantized_dimension, channel_count)]
                for i, v in enumerate(raw_values)
            ]
        else:
            raise ValueError("ml: block-wise quantization is not supported for predict() outputs")

        arr = np.array(values, dtype=np.float)
        return arr.reshape(info.shape) if info.shape else arr
