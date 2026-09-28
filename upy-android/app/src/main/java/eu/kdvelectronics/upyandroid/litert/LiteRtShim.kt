@file:JvmName("LiteRtShim")
// Thin JNI-facing glue for litert (litert_module.cpp/
// litert_jni_bridge.cpp).
// see session-state: LiteRtShim.kt#createEnvironment
package eu.kdvelectronics.upyandroid.litert

import com.google.ai.edge.litert.Accelerator
import com.google.ai.edge.litert.CompiledModel
import com.google.ai.edge.litert.Environment
import com.google.ai.edge.litert.TensorBuffer

fun createEnvironment(): Environment = Environment.create()

// acceleratorValue is Accelerator's own .value int (NONE=0, CPU=1,
// GPU=2, NPU=3), not an enum. An Int is JNI-simple, an enum isn't.
fun createCompiledModel(env: Environment, path: String, acceleratorValue: Int): CompiledModel {
    val accelerator = Accelerator.entries.first { it.value == acceleratorValue }
    return CompiledModel.create(path, CompiledModel.Options(accelerator), env)
}

fun createInputBuffers(model: CompiledModel): Array<TensorBuffer> =
    model.createInputBuffers().toTypedArray()

fun createOutputBuffers(model: CompiledModel): Array<TensorBuffer> =
    model.createOutputBuffers().toTypedArray()

// litert (litert_module.cpp) exposes all five of TensorBuffer's own
// typed read/write pairs, literally, one JNI-facing function each.
fun writeInt8(buf: TensorBuffer, data: ByteArray) = buf.writeInt8(data)

fun readInt8(buf: TensorBuffer): ByteArray = buf.readInt8()

fun writeFloat(buf: TensorBuffer, data: FloatArray) = buf.writeFloat(data)

fun readFloat(buf: TensorBuffer): FloatArray = buf.readFloat()

fun writeInt(buf: TensorBuffer, data: IntArray) = buf.writeInt(data)

fun readInt(buf: TensorBuffer): IntArray = buf.readInt()

// Named writeBool/readBool on this JNI-facing surface (not writeBoolean/
// readBoolean) purely to keep this file's own names short; the real
// Kotlin method underneath is writeBoolean()/readBoolean().
fun writeBool(buf: TensorBuffer, data: BooleanArray) = buf.writeBoolean(data)

fun readBool(buf: TensorBuffer): BooleanArray = buf.readBoolean()

fun writeLong(buf: TensorBuffer, data: LongArray) = buf.writeLong(data)

fun readLong(buf: TensorBuffer): LongArray = buf.readLong()

fun run(model: CompiledModel, inputs: Array<TensorBuffer>, outputs: Array<TensorBuffer>) =
    model.run(inputs.toList(), outputs.toList())

// close() always goes through here. The underlying native object has
// exactly one owner (Kotlin's own AutoCloseable).
fun closeEnvironment(env: Environment) = env.close()
fun closeCompiledModel(model: CompiledModel) = model.close()
fun closeTensorBuffer(buf: TensorBuffer) = buf.close()
