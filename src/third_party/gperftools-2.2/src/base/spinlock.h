// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
/* Copyright (c) 2006, Google Inc.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ---
 * Author: Sanjay Ghemawat
 */

// SpinLock is async signal safe.
// If used within a signal handler, all lock holders
// should block the signal even outside the signal handler.

#ifndef BASE_SPINLOCK_H_
#define BASE_SPINLOCK_H_

#include <config.h>
#include "base/atomicops.h"
#include "base/basictypes.h"
#include "base/dynamic_annotations.h"
#include "base/thread_annotations.h"

#include <atomic>

enum class SpinLockType {
    CentralFreeList = 0,
    MaxCentralFreeList = 88,
    PageHeap,
    SystemAlloc,
    LowLevelAllocArena,
    HookList,
    Patch,
    NewHandler,
    MemoryMap,
    MemoryMapOwner,
    Metadata,
    Crash,
    DebugAllocMap,
    DebugFreeQueue,
    DebugMallocTrace,
    HeapChecker,
    HeapCheckerAlignment,
    HeapCheckerObject,
    ProfileHandlerControl,
    ProfileHandlerSignal,
    HeapProfiler,
    CpuProfiler,
    SpinLockTypeMax,
};

const int SpinLockTypeMaxValue = 109;

#if _MSC_VAR
__declspec( align( 64 ) )
#endif
struct SpinLockStat {
    std::atomic<uint64_t> acquires;
    std::atomic<uint64_t> waits;
    std::atomic<uint64_t> wait_count;
    std::atomic<uint64_t> wait_time;
}
#ifdef __GNUC__
__attribute__ ((aligned (64)))
#endif
;

class SpinLockStats {
public:
    static SpinLockStats Static;

    void Acquire(int type) {
        stats[type].acquires++;
    }
    void Wait(int type, size_t wait_count, size_t wait_time) {
        stats[type].waits++;
        stats[type].wait_count+=wait_count;

        stats[type].wait_time+=wait_time;
    }

    size_t count() const { return SpinLockTypeMaxValue; }

    const SpinLockStat& getStat(int type) const { return stats[type]; }

private:
    SpinLockStat stats[SpinLockTypeMaxValue];
};

class LOCKABLE SpinLockBase {
 public:
  SpinLockBase() : lockword_(kSpinLockFree) { }

  // Special constructor for use with static SpinLock objects.  E.g.,
  //
  //    static SpinLock lock(base::LINKER_INITIALIZED);
  //
  // When intialized using this constructor, we depend on the fact
  // that the linker has already initialized the memory appropriately.
  // A SpinLock constructed like this can be freely used from global
  // initializers without worrying about the order in which global
  // initializers run.
  explicit SpinLockBase(base::LinkerInitialized /*x*/) {
    // Does nothing; lockword_ is already initialized
  }

  // Acquire this SpinLock.
  // TODO(csilvers): uncomment the annotation when we figure out how to
  //                 support this macro with 0 args (see thread_annotations.h)
  inline void Lock(size_t value) /*EXCLUSIVE_LOCK_FUNCTION()*/ {
    if (base::subtle::Acquire_CompareAndSwap(&lockword_, kSpinLockFree,
                                             kSpinLockHeld) != kSpinLockFree) {
      SlowLock(value);
    } else {
        SpinLockStats::Static.Acquire(value);
    }
    ANNOTATE_RWLOCK_ACQUIRED(this, 1);
  }

  // Try to acquire this SpinLock without blocking and return true if the
  // acquisition was successful.  If the lock was not acquired, false is
  // returned.  If this SpinLock is free at the time of the call, TryLock
  // will return true with high probability.
  inline bool TryLock() EXCLUSIVE_TRYLOCK_FUNCTION(true) {
    bool res =
        (base::subtle::Acquire_CompareAndSwap(&lockword_, kSpinLockFree,
                                              kSpinLockHeld) == kSpinLockFree);
    if (res) {
      ANNOTATE_RWLOCK_ACQUIRED(this, 1);
    }
    return res;
  }

  // Release this SpinLock, which must be held by the calling thread.
  // TODO(csilvers): uncomment the annotation when we figure out how to
  //                 support this macro with 0 args (see thread_annotations.h)
  inline void Unlock() /*UNLOCK_FUNCTION()*/ {
    ANNOTATE_RWLOCK_RELEASED(this, 1);
    uint64 wait_cycles = static_cast<uint64>(
        base::subtle::Release_AtomicExchange(&lockword_, kSpinLockFree));
    if (wait_cycles != kSpinLockHeld) {
      // Collect contentionz profile info, and speed the wakeup of any waiter.
      // The wait_cycles value indicates how long this thread spent waiting
      // for the lock.
      SlowUnlock(wait_cycles);
    }
  }

  // Determine if the lock is held.  When the lock is held by the invoking
  // thread, true will always be returned. Intended to be used as
  // CHECK(lock.IsHeld()).
  inline bool IsHeld() const {
    return base::subtle::NoBarrier_Load(&lockword_) != kSpinLockFree;
  }

  static const base::LinkerInitialized LINKER_INITIALIZED;  // backwards compat
 private:
  enum { kSpinLockFree = 0 };
  enum { kSpinLockHeld = 1 };
  enum { kSpinLockSleeper = 2 };

  volatile Atomic32 lockword_;

  void SlowLock(size_t value);
  void SlowUnlock(uint64 wait_cycles);
  Atomic32 SpinLoop(int64 initial_wait_timestamp, Atomic32* wait_cycles);
  inline int32 CalculateWaitCycles(int64 wait_start_time);

  DISALLOW_COPY_AND_ASSIGN(SpinLockBase);
};

template< SpinLockType SpinLockTypeValue>
class LOCKABLE SpinLock : public SpinLockBase {
 public:
  SpinLock() { }

  // Special constructor for use with static SpinLock objects.  E.g.,
  //
  //    static SpinLock lock(base::LINKER_INITIALIZED);
  //
  // When intialized using this constructor, we depend on the fact
  // that the linker has already initialized the memory appropriately.
  // A SpinLock constructed like this can be freely used from global
  // initializers without worrying about the order in which global
  // initializers run.
  explicit SpinLock(base::LinkerInitialized /*x*/) {
    // Does nothing; lockword_ is already initialized
  }

    // Acquire this SpinLock.
  // TODO(csilvers): uncomment the annotation when we figure out how to
  //                 support this macro with 0 args (see thread_annotations.h)
  inline void Lock() /*EXCLUSIVE_LOCK_FUNCTION()*/ {
    SpinLockBase::Lock(static_cast<size_t>(SpinLockTypeValue));
  }

  inline void Lock(size_t value) /*EXCLUSIVE_LOCK_FUNCTION()*/ {
    SpinLockBase::Lock(value);
  }

  // Try to acquire this SpinLock without blocking and return true if the
  // acquisition was successful.  If the lock was not acquired, false is
  // returned.  If this SpinLock is free at the time of the call, TryLock
  // will return true with high probability.
  inline bool TryLock() EXCLUSIVE_TRYLOCK_FUNCTION(true) {
    return       SpinLockBase::TryLock();
;
  }

  // Release this SpinLock, which must be held by the calling thread.
  // TODO(csilvers): uncomment the annotation when we figure out how to
  //                 support this macro with 0 args (see thread_annotations.h)
  inline void Unlock() /*UNLOCK_FUNCTION()*/ {
      SpinLockBase::Unlock();
  }

    // thread, true will always be returned. Intended to be used as
  // CHECK(lock.IsHeld()).
  inline bool IsHeld() const {
    return SpinLockBase::IsHeld();
  }

  static const base::LinkerInitialized LINKER_INITIALIZED;  // backwards compat
 private:
  DISALLOW_COPY_AND_ASSIGN(SpinLock);
};




// Corresponding locker object that arranges to acquire a spinlock for
// the duration of a C++ scope.
class SCOPED_LOCKABLE SpinLockHolderId {
 private:
  SpinLockBase* lock_;
 public:
  inline explicit SpinLockHolderId(SpinLockBase* l, size_t id) EXCLUSIVE_LOCK_FUNCTION(l)
      : lock_(l) {
    l->Lock(id);
  }
  // TODO(csilvers): uncomment the annotation when we figure out how to
  //                 support this macro with 0 args (see thread_annotations.h)
  inline ~SpinLockHolderId() /*UNLOCK_FUNCTION()*/ { lock_->Unlock(); }
};

// Corresponding locker object that arranges to acquire a spinlock for
// the duration of a C++ scope.
class SCOPED_LOCKABLE SpinLockHolder {
 private:
  SpinLockBase* lock_;
 public:
template< SpinLockType SpinLockTypeValue>
  inline explicit SpinLockHolder(SpinLock<SpinLockTypeValue>* l) EXCLUSIVE_LOCK_FUNCTION(l)
      : lock_(l) {
    l->Lock();
  }
  // TODO(csilvers): uncomment the annotation when we figure out how to
  //                 support this macro with 0 args (see thread_annotations.h)
  inline ~SpinLockHolder() /*UNLOCK_FUNCTION()*/ { lock_->Unlock(); }
};

// Catch bug where variable name is omitted, e.g. SpinLockHolder (&lock);
#define SpinLockHolder(x) COMPILE_ASSERT(0, spin_lock_decl_missing_var_name)

#endif  // BASE_SPINLOCK_H_
