import time

### simple asyncio scheduler

class sleep:
    def __init__(self, t):
        self.t = t
    def __await__(self):
        yield self

class Sched:
    def __init__(self):
        self.tasks = []
        self.cnt = 0

    def call_at(self, t, coro):
        #print('call at', time.time(), t, coro)
        self.tasks.append((t, self.cnt, coro))
        self.cnt += 1
        self.tasks.sort()

    def run(self):
        while self.tasks:
            t, _, coro = self.tasks.pop(0)
            #print(t, coro)
            delay = t - time.time()
            if delay > 0:
                time.sleep(delay)
            try:
                ret = next(coro)
                if isinstance(ret, sleep):
                    self.call_at(time.time() + ret.t, coro)
                else:
                    assert False
            except StopIteration:
                pass

### a task

t0 = time.time()

async def tick(msg, t):
    for i in range(4):
        print(int(time.time() - t0), msg, i)
        await sleep(t)
    print(int(time.time() - t0), msg, 'done')

### schedule and run tasks

sched = Sched()
sched.call_at(0, tick('T1', 1))
sched.call_at(0, tick('T2', 2))
sched.run()
