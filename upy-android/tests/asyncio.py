# test: required
#
# asyncio: gather runs tasks concurrently, Event wakes a waiting task,
# wait_for raises TimeoutError.
import asyncio
import time


async def worker(n, ms):
    for _ in range(n):
        await asyncio.sleep_ms(ms)
    return n * ms


async def main():
    t0 = time.ticks_ms()
    results = await asyncio.gather(worker(5, 100), worker(10, 50), worker(2, 200))
    elapsed = time.ticks_diff(time.ticks_ms(), t0)
    print("gather results:", results)
    # Concurrent: about 500 ms. One after the other: 1400 ms.
    print("gather concurrent:", elapsed < 700)

    event = asyncio.Event()

    async def setter():
        await asyncio.sleep_ms(50)
        event.set()

    asyncio.create_task(setter())
    await event.wait()
    print("event set:", event.is_set())

    async def slow():
        await asyncio.sleep(5)

    try:
        await asyncio.wait_for(slow(), 0.1)
        print("wait_for timeout: False")
    except asyncio.TimeoutError:
        print("wait_for timeout: True")


asyncio.run(main())
