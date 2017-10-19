/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "QMEHashTable.h"
#include "mozilla/HashFunctions.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/OperatorNewExtensions.h"
#include "nsAlgorithm.h"
#include "nsPointerHashKeys.h"
#include "mozilla/Likely.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/ChaosMode.h"

using namespace mozilla;

// TODO: thread-safety checks

// Hash key types accepted by ns*Hashtable usually (assuming that ENABLE_MEMMOVE
// is defined) move entire objects via their moveEntry vtable method.  If
// ENABLE_MEMMOVE is not defined--meaning that C++ move/copy construction is
// used to implement moveEntry--many hash key types have a penchant for not
// constructing the PLDHashEntryHdr parent object during their move/copy
// constructors.  This omission means that the underlying hashtable
// implementation (PLD/QMEHashtable) needs to copy around the stored hash from
// PLDHashEntryHdr itself.  This manual copying is inelegant for ENABLE_MEMMOVE
// key types, and an efficiency loss for !ENABLE_MEMMOVE key types, since the
// latter's memcpy()-based copying will transfer the stored hash just fine.
//
// We therefore have this define to test how much of a difference omitting the
// manual hash copying makes; initial testing says "not very much", but it's
// still wasted work, and we should clean up the hash key types to work properly.
//#define BROKEN_HASH_KEYS

static bool
QSizeOfEntryStore(uint32_t aCapacity, uint32_t aEntrySize, uint32_t* aNbytes)
{
  uint64_t nbytes64 = uint64_t(aCapacity) * uint64_t(aEntrySize);
  *aNbytes = aCapacity * aEntrySize;
  return uint64_t(*aNbytes) == nbytes64;   // returns false on overflow
}

// Compute the minimum capacity (and the Log2 of that capacity) for a table
// containing |aLength| elements while respecting the following contraints:
// - table must be at most 75% full;
// - capacity must be a power of two;
// - capacity cannot be too small.
static inline void
QBestCapacity(uint32_t aLength, uint32_t* aCapacityOut,
              uint32_t* aLog2CapacityOut)
{
  // Compute the smallest capacity allowing |aLength| elements to be inserted
  // without rehashing.
  uint32_t capacity = (aLength * 8 + (7 - 1)) / 7; // == ceil(aLength * 4 / 3)
  if (capacity < PLDHashTable::kMinCapacity) {
    capacity = PLDHashTable::kMinCapacity;
  }

  // Round up capacity to next power-of-two.
  uint32_t log2 = CeilingLog2(capacity);
  capacity = 1u << log2;
  MOZ_ASSERT(capacity <= PLDHashTable::kMaxCapacity);

  *aCapacityOut = capacity;
  *aLog2CapacityOut = log2;
}

// Compute max and min load numbers (entry counts). We have a secondary max
// that allows us to overload a table reasonably if it cannot be grown further
// (i.e. if ChangeTable() fails). The table slows down drastically if the
// secondary max is too close to 1, but 0.96875 gives only a slight slowdown
// while allowing 1.3x more elements.
static inline uint32_t
QMaxLoad(uint32_t aCapacity)
{
  return aCapacity - (aCapacity >> 3);  // == aCapacity * 0.875
}

/* static */ MOZ_ALWAYS_INLINE uint32_t
QMEHashTable::HashShift(uint32_t aEntrySize, uint32_t aLength)
{
  if (aLength > kMaxInitialLength) {
    MOZ_CRASH("Initial length is too large");
  }

  uint32_t capacity, log2;
  QBestCapacity(aLength, &capacity, &log2);

  uint32_t nbytes;
  if (!QSizeOfEntryStore(capacity, aEntrySize, &nbytes)) {
    MOZ_CRASH("Initial entry store size is too large");
  }

  // Compute the hashShift value.
  return kHashBits - log2;
}

QMEHashTable::QMEHashTable(const QMEHashTableOps* aOps, uint32_t aEntrySize,
                           uint32_t aLength)
  : mOps(aOps)
  , mGeneration(0)
  , mHashShift(HashShift(aEntrySize, aLength))
  , mEntrySize(aEntrySize)
  , mEntryCount(0)
  , mEntryStore()
  , mSizeMask(CapacityFromHashShift() - 1)
{
  MOZ_RELEASE_ASSERT(aEntrySize == uint32_t(mEntrySize),
                     "Entry size is too large");
}

QMEHashTable&
QMEHashTable::operator=(QMEHashTable&& aOther)
{
  if (this == &aOther) {
    return *this;
  }

  // Destruct |this|.
  this->~QMEHashTable();

  // |mOps| and |mEntrySize| are const so we can't assign them. Instead, we
  // require that they are equal. The justification for this is that they're
  // conceptually part of the type -- indeed, if QMEHashTable was a templated
  // type like nsTHashtable, they *would* be part of the type -- so it only
  // makes sense to assign in cases where they match.
  MOZ_RELEASE_ASSERT(mOps == aOther.mOps);
  MOZ_RELEASE_ASSERT(mEntrySize == aOther.mEntrySize);

  // Move non-const pieces over.
  mHashShift = Move(aOther.mHashShift);
  mEntryCount = Move(aOther.mEntryCount);
  mEntryStore.Set(aOther.mEntryStore.Get(), &mGeneration);
  mSizeMask = Move(aOther.mSizeMask);
#ifdef DEBUG
  mChecker = Move(aOther.mChecker);
#endif

  // Clear up |aOther| so its destruction will be a no-op.
  {
#ifdef DEBUG
    AutoDestructorOp op(mChecker);
#endif
    aOther.mEntryStore.Set(nullptr, &aOther.mGeneration);
  }

  return *this;
}

QMEHashTable::~QMEHashTable()
{
#ifdef DEBUG
  AutoDestructorOp op(mChecker);
#endif

  if (!mEntryStore.Get()) {
    return;
  }

  // Clear any remaining live entries.
  char* entryAddr = mEntryStore.Get();
  char* entryLimit = entryAddr + Capacity() * mEntrySize;
  QMEHashClearEntry clear = mOps->clearEntry;
  while (entryAddr < entryLimit) {
    PLDHashEntryHdr* entry = (PLDHashEntryHdr*)entryAddr;
    if (EntryIsLive(entry)) {
      clear(this, entry);
    }
    entryAddr += mEntrySize;
  }

  // Entry storage is freed last, by ~EntryStore().
}

// Match an entry's mKeyHash against an unstored one computed from a key.
/* static */ bool
QMEHashTable::MatchEntryKeyhash(PLDHashEntryHdr* aEntry, PLDHashNumber aKeyHash)
{
  return aEntry->mKeyHash == aKeyHash;
}

PLDHashEntryHdr*
QMEHashTable::AddressEntry(uint32_t aIndex)
{
  return reinterpret_cast<PLDHashEntryHdr*>(
    mEntryStore.Get() + aIndex * mEntrySize);
}

void
QMEHashTable::ClearAndPrepareForLength(uint32_t aLength)
{
  // Get these values before the destructor clobbers them.
  const QMEHashTableOps* ops = mOps;
  uint32_t entrySize = mEntrySize;

  this->~QMEHashTable();
  new (KnownNotNull, this) QMEHashTable(ops, entrySize, aLength);
}

void
QMEHashTable::Clear()
{
  ClearAndPrepareForLength(kDefaultInitialLength);
}

uint32_t
QMEHashTable::IdealBucketIndex(PLDHashNumber aHash)
{
  return aHash & mSizeMask;
}

#if 0
class EntryIterator
{
public:
  static EntryIterator FromHash(char* aEntryStore,
                                const uint32_t& aEntrySize,
                                uint32_t& aSizeMask,
                                PLDHashNumber aKeyHash)
  {
    return EntryIterator(aEntryStore, aEntrySize, aSizeMask, aKeyHash);
  }

  EntryIterator(EntryIterator&&) = default;
  EntryIterator& operator=(EntryIterator&&) = default;

  PLDHashEntryHdr* Entry() { return mEntry; }
  uint32_t EntryIndex() { return mEntryIndex; }

  uint32_t ProbeLength(PLDHashNumber aKeyHash)
  {
    return (mEntryIndex - aKeyHash) & mSizeMask;
  }

  EntryIterator& operator++()
  {
    UpdateEntryInformation(mEntryIndex + 1);
    return *this;
  }

private:
  void UpdateEntryInformation(uint32_t aNewEntryIndex)
  {
    mEntryIndex = aNewEntryIndex & mSizeMask;
    mEntry = reinterpret_cast<PLDHashEntryHdr*>(mEntryStore + mEntryIndex * mEntrySize);
  }

  EntryIterator(char* aEntryStore, const uint32_t& aEntrySize,
                uint32_t aSizeMask, PLDHashNumber aKeyHash)
    : mEntryStore(aEntryStore)
    , mEntrySize(aEntrySize)
    , mSizeMask(aSizeMask)
  {
    UpdateEntryInformation(aKeyHash);
  }

  // XXX
  char*& mEntryStore;
  const uint32_t mEntrySize;
  uint32_t& mSizeMask;

  uint32_t mEntryIndex;
  PLDHashEntryHdr* mEntry;
};
#else
class EntryIterator
{
public:
  static EntryIterator FromHash(char* aEntryStore,
                                const uint32_t& aEntrySize,
                                uint32_t& aSizeMask,
                                PLDHashNumber aKeyHash)
  {
    return EntryIterator(aEntryStore, aEntrySize, aSizeMask, aKeyHash);
  }

  EntryIterator(EntryIterator&&) = default;
  EntryIterator& operator=(EntryIterator&&) = default;

  PLDHashEntryHdr* Entry() { return mEntry; }
  uint32_t EntryIndex() { return mEntryIndex; }

  uint32_t ProbeLength(PLDHashNumber aKeyHash)
  {
    return (mEntryIndex - aKeyHash) & mSizeMask;
  }

  EntryIterator& operator++()
  {
    mEntryIndex = (mEntryIndex + 1) & mSizeMask;
    char* newEntry = reinterpret_cast<char*>(mEntry) + mEntrySize;
    if (newEntry >= mEntryStoreEnd) {
      newEntry = mEntryStore;
    }
    mEntry = reinterpret_cast<PLDHashEntryHdr*>(newEntry);
    return *this;
  }

private:
  void UpdateEntryInformation(uint32_t aNewEntryIndex)
  {
    mEntryIndex = aNewEntryIndex & mSizeMask;
    mEntry = reinterpret_cast<PLDHashEntryHdr*>(mEntryStore + mEntryIndex * mEntrySize);
  }

  EntryIterator(char*& aEntryStore, const uint32_t& aEntrySize,
                uint32_t& aSizeMask, PLDHashNumber aKeyHash)
    : mEntryStore(aEntryStore)
    , mEntrySize(aEntrySize)
    , mSizeMask(aSizeMask)
  {
    mEntryStoreEnd = mEntryStore + mEntrySize * (mSizeMask + 1);
    UpdateEntryInformation(aKeyHash);
  }

  // XXX
  char*& mEntryStore;
  const uint32_t mEntrySize;
  uint32_t& mSizeMask;

  char* mEntryStoreEnd;
  uint32_t mEntryIndex;
  PLDHashEntryHdr* mEntry;
};
#endif

template<QMEHashTable::SearchReason Reason>
PLDHashEntryHdr*
QMEHashTable::SearchTable(const void* aKey, PLDHashNumber aKeyHash)
{
  bool addingEntry = Reason == ForAdd || Reason == ForAddDuringResize;
  // Try to ensure that we don't add new reasons without handling them.
  MOZ_ASSERT_IF(!addingEntry, Reason == ForSearchOrRemove);

  // QMEHashTable employs linear probing with Robin Hood-style hashing.
  // We will compute an initial "bucket" that the given key should be
  // placed at, given its hash.  We'll probe linearly from that bucket
  // to find an empty bucket to store the entry in.
  //
  // As we go along, if there's a bucket whose distance from *its*
  // desired initial bucket is lower than the distance we've probed from
  // our desired initial bucket, we'll put our inserted entry there, and
  // continue inserting the now-vacated element.
  //
  // We are guaranteed to find a new entry.

  uint32_t probeLength = 0;
  void* temporaryStorage[256 / sizeof(void*)];
  PLDHashEntryHdr* temporary = reinterpret_cast<PLDHashEntryHdr*>(&temporaryStorage[0]);
  bool reinserting = false;
  PLDHashEntryHdr* retval = nullptr;

  EntryIterator iter = EntryIterator::FromHash(mEntryStore.Get(), mEntrySize,
                                               mSizeMask, aKeyHash);

  for (;;) {
    //MOZ_RELEASE_ASSERT(bucketIndex < CapacityFromHashShift());

    PLDHashEntryHdr* entry = iter.Entry();

    // If our current bucket is free, then we are all done.
    if (EntryIsFree(entry)) {
      // If we have been shuffling entries around, this is the stop for
      // the temporary entry.
      if (reinserting) {
        MOZ_ASSERT(addingEntry);
        MOZ_ASSERT(retval);
#ifdef BROKEN_HASH_KEYS
        PLDHashNumber tempHash = temporary->mKeyHash;
#endif
        mOps->moveEntry(this, temporary, entry);
#ifdef BROKEN_HASH_KEYS
        entry->mKeyHash = tempHash;
#endif
        return retval;
      }
      return (addingEntry) ? entry : nullptr;
    }

    // What we do here depends on what state we're in:
    //
    // - If we are reinserting some entry from the table, then we do nothing,
    //   because the reinserted entry cannot possibly match any of the other
    //   entries in the hashtable.
    //
    // - If we are adding an entry not during resize, or just searching
    //   through the table, then we need to check for a match.
    //
    // - If we are adding an entry during a resize operation, then we do
    //   nothing, because the new entry cannot possibly match any of the
    //   other entries in the hashtable.
    //
    // XXX we ought to be able to skip the check for reinsertion more
    // elegantly; the compiler might be able to optimize this check out
    // (e.g. for searches), but it would be nice if we didn't have to make the
    // check: we can easily distinguish cases where we are reinserting from
    // those where we are not.  The question is whether the extra codesize
    // would be worth skipping the conditional branch.
    if (reinserting) {
      ;
    } else if (Reason != ForAddDuringResize) {
      PLDHashMatchEntry matchEntry = mOps->matchEntry;
      if (MatchEntryKeyhash(entry, aKeyHash) &&
          matchEntry(entry, aKey)) {
        return entry;
      }
    } else {
      // We don't have access to keys during resize, so we shouldn't be trying
      // to pretend like we do.
      MOZ_ASSERT(aKey == nullptr);
    }

    // The normal Robin Hood hashing algorithm would call for us to swap
    // the (key, value) pair we're currently inserting with the (key,
    // value) pair we're examining and continue insertion as usual.  We aren't
    // inserting a (key, value) pair here, though; we just have a key initially
    // and the object this hash table is keyed on may not even be stored in
    // what will be the eventual entry.
    //
    // So, what are we going to do?  Well, we have a temporary entry that has
    // been allocated before we started inserting things into the hashtable.
    //
    // 1. If we have not started reinserting elements, then:
    // 2a. Move the offending element into the temporary storage.
    // 2b. Initialize the entry with our key that's getting inserted.
    // 2c. Remember this entry, as we're going to need to return it.  But
    //     we still have work to do, so continue through the loop, with
    //     a new entry.
    // 3. We are re-inserting elements, so:
    // 3a. Swap the temporary entry with the offending element.
    // 3b. Continue inserting the new temporary entry.
    if (addingEntry) {
      // What is the current element's probe length?
      uint32_t existingProbeLength = iter.ProbeLength(entry->mKeyHash);

      if (existingProbeLength < probeLength) {
        if (!reinserting) {
          reinserting = true;
#ifdef BROKEN_HASH_KEYS
          PLDHashNumber entryHash = entry->mKeyHash;
#endif
          mOps->moveEntry(this, entry, temporary);
#ifdef BROKEN_HASH_KEYS
          temporary->mKeyHash = entryHash;
#endif
          // Entry is now destroyed and we can use it.
          retval = entry;
        } else {
          // We have two entries that we need to swap: the current entry,
          // `entry` and the temporary entry.  We *also* have a free entry
          // in `retval` that we can use for temporary storage.
          //
          // XXX it's not clear whether the three function calls we require
          // here could be more efficiently exposed by having a `swapEntries`
          // method and calling that, thus enabling the swap to be "inlined".
#ifdef BROKEN_HASH_KEYS
          PLDHashNumber entryHash = entry->mKeyHash;
          PLDHashNumber temporaryHash = temporary->mKeyHash;
#endif
          QMEHashMoveEntry move = mOps->moveEntry;
          move(this, temporary, retval);
          move(this, entry, temporary);
          move(this, retval, entry);
          // XXX moveEntry should ensure this happens, really.  We do this
          // after the actual moves rather than before in case moveEntry
          // does implement moving mKeyHash, so we can make sure that
          // the hashes wind up where they are supposed to.
#ifdef BROKEN_HASH_KEYS
          entry->mKeyHash = temporaryHash;
          temporary->mKeyHash = entryHash;
#endif
        }

        MarkEntryFree(retval);
        // Either way, we are now re-inserting the entry stored at `temporary`.
        // It's important to note here that we *don't* need the original key of
        // temporary; indeed, some hash table entries may not even store the
        // key they are inserted with.  We only needed the key to determine if
        // the entry we wanted to insert collided with other entries in the
        // table, but we know by construction that this entry, which was already
        // stored in the hash table, does not collide.  So we will skip the
        // check that would require the key.  We will, however, use the hash
        // of the temporary entry to determine if other entries in this chain
        // need to be moved around.
        aKeyHash = temporary->mKeyHash;
        probeLength = existingProbeLength;
      }
    } else {
      MOZ_ASSERT(Reason == ForSearchOrRemove);
      // What if we're just searching?  Then we had an element removed
      // from the chain, and we need to keep probing until we find a
      // free entry.  So we fall through here.
    }

    ++iter;
    ++probeLength;
  }

  // NOTREACHED
  return nullptr;
}


PLDHashNumber
QMEHashTable::ComputeKeyHash(const void* aKey)
{
  PLDHashNumber keyHash = mOps->hashKey(aKey);
  keyHash *= kGoldenRatio;

  // Avoid a 0 hash code, it indicates free.
  if (keyHash == 0) {
    keyHash -= 1;
  }

  return keyHash;
}

PLDHashEntryHdr*
QMEHashTable::Search(const void* aKey)
{
#ifdef DEBUG
  AutoReadOp op(mChecker);
#endif

  PLDHashEntryHdr* entry = mEntryStore.Get()
                         ? SearchTable<ForSearchOrRemove>(aKey,
                                                          ComputeKeyHash(aKey))
                         : nullptr;
  return entry;
}

PLDHashEntryHdr*
QMEHashTable::Add(const void* aKey, const mozilla::fallible_t&)
{
#ifdef DEBUG
  AutoWriteOp op(mChecker);
#endif

  // Allocate the entry storage if it hasn't already been allocated.
  if (!mEntryStore.Get()) {
    uint32_t nbytes;
    // We already checked this in the constructor, so it must still be true.
    MOZ_RELEASE_ASSERT(QSizeOfEntryStore(CapacityFromHashShift(), mEntrySize,
                                         &nbytes));
    mEntryStore.Set((char*)calloc(1, nbytes), &mGeneration);
    if (!mEntryStore.Get()) {
      return nullptr;
    }
  }

  // TODO: adjust load factor calculations; our Robin Hood hashing strategy
  // should be able to handle a larger load factor than PLDHashTable.

  // If alpha is >= .75, grow or compress the table. If aKey is already in the
  // table, we may grow once more than necessary, but only if we are on the
  // edge of being overloaded.
  uint32_t capacity = Capacity();
  if (mEntryCount >= QMaxLoad(capacity)) {
    int deltaLog2 = 1;

    // Grow or compress the table. If ChangeTable() fails, allow overloading up
    // to the secondary max. Once we hit the secondary max, return null.
    if (!ChangeTable(deltaLog2) &&
        mEntryCount >= MaxLoadOnGrowthFailure(capacity)) {
      return nullptr;
    }
  }

  PLDHashNumber keyHash = ComputeKeyHash(aKey);
  PLDHashEntryHdr* entry = SearchTable<ForAdd>(aKey, keyHash);
  MOZ_ASSERT(entry);
  if (!EntryIsLive(entry)) {
    mOps->initEntry(entry, aKey);
    entry->mKeyHash = keyHash;
    ++mEntryCount;
  }

  return entry;
}

PLDHashEntryHdr*
QMEHashTable::Add(const void* aKey)
{
  PLDHashEntryHdr* entry = Add(aKey, fallible);
  if (!entry) {
    if (!mEntryStore.Get()) {
      // We OOM'd while allocating the initial entry storage.
      uint32_t nbytes;
      (void) QSizeOfEntryStore(CapacityFromHashShift(), mEntrySize, &nbytes);
      NS_ABORT_OOM(nbytes);
    } else {
      // We failed to resize the existing entry storage, either due to OOM or
      // because we exceeded the maximum table capacity or size; report it as
      // an OOM. The multiplication by 2 gets us the size we tried to allocate,
      // which is double the current size.
      NS_ABORT_OOM(2 * EntrySize() * EntryCount());
    }
  }
  return entry;
}

bool
QMEHashTable::ChangeTable(int32_t aDeltaLog2)
{
  MOZ_ASSERT(mEntryStore.Get());

  // Look, but don't touch, until we succeed in getting new entry store.
  int32_t oldLog2 = kHashBits - mHashShift;
  int32_t newLog2 = oldLog2 + aDeltaLog2;
  uint32_t newCapacity = 1u << newLog2;
  if (newCapacity > kMaxCapacity) {
    return false;
  }

  uint32_t nbytes;
  if (!QSizeOfEntryStore(newCapacity, mEntrySize, &nbytes)) {
    return false;   // overflowed
  }

  char* newEntryStore = (char*)calloc(1, nbytes);
  if (!newEntryStore) {
    return false;
  }

  // We can't fail from here on, so update table parameters.
  mHashShift = kHashBits - newLog2;
  mSizeMask = newCapacity - 1;

  // Assign the new entry store to table.
  char* oldEntryStore;
  char* oldEntryAddr;
  oldEntryAddr = oldEntryStore = mEntryStore.Get();
  mEntryStore.Set(newEntryStore, &mGeneration);
  QMEHashMoveEntry moveEntry = mOps->moveEntry;

  // Copy live entries to the new table.
  uint32_t oldCapacity = 1u << oldLog2;
  for (uint32_t i = 0; i < oldCapacity; ++i) {
    PLDHashEntryHdr* oldEntry = (PLDHashEntryHdr*)oldEntryAddr;
    if (EntryIsLive(oldEntry)) {
      // oldEntry isn't a key, so we shouldn't pass it, and adding things
      // during resize shouldn't require a key anyway, so just pass nullptr.
      PLDHashEntryHdr* newEntry = SearchTable<ForAddDuringResize>(nullptr, oldEntry->mKeyHash);
      MOZ_ASSERT(EntryIsFree(newEntry), "should have found a completely new entry");
      moveEntry(this, oldEntry, newEntry);
#ifdef BROKEN_HASH_KEYS
      newEntry->mKeyHash = oldEntry->mKeyHash;
#endif
    }
    oldEntryAddr += mEntrySize;
  }

  free(oldEntryStore);
  return true;
}

// TODO: Rust's hashtables have very nice abstractions that hide a lot of
// this; maybe we can rewrite everything to use them instead of all this
// nasty pointer arithmetic?
uint32_t
QMEHashTable::EntryToBucketIndex(PLDHashEntryHdr* aEntry)
{
  // TODO: Can we avoid this divide with some sort of shift?
  return (reinterpret_cast<char*>(aEntry) - mEntryStore.Get()) / mEntrySize;
}

void
QMEHashTable::RawRemove(PLDHashEntryHdr* aEntry)
{
  // Unfortunately, we can only do weak checking here. That's because
  // RawRemove() can be called legitimately while an Enumerate() call is
  // active, which doesn't fit well into how Checker's mState variable works.
  MOZ_ASSERT(mChecker.IsWritable());

  MOZ_ASSERT(mEntryStore.Get());

  MOZ_ASSERT(EntryIsLive(aEntry), "EntryIsLive(aEntry)");

  // One technique for implementing removes is to use "tombstones": special
  // markers to indicate that the entry has been removed, but this entry was
  // part of a chain and subsequent searches may need to continue probing to
  // find the entry they are looking for.
  //
  // Instead of doing this, our implementation uses backward shift deletion:
  //
  // http://codecapsule.com/2013/11/17/robin-hood-hashing-backward-shift-deletion/
  //
  // When we remove an entry, we'll scan forwards to find an entry that is
  // either empty, or in its desired place.  Then we'll shift all entries
  // between our removed entry and the found entry backwards, lowering their
  // distance from their desired entry slot.  Doing this improves lookup and
  // insertion performance.
  //
  // What about accidentally quadratic concerns here?  For instance, what if
  // we had a *really* long probe chain?  Such chains are unlikely, given our
  // Robin Hood insertion strategy above.  XXX more robust explanation.

  mOps->clearEntry(this, aEntry);
  MarkEntryFree(aEntry);

  // Look for an free entry or an entry that is exactly where it wants to be.
  uint32_t emptyBucket = EntryToBucketIndex(aEntry);
  PLDHashEntryHdr* emptyEntry = aEntry;
  for (;;) {
    MOZ_ASSERT(EntryIsFree(emptyEntry));

    uint32_t nextBucket = (emptyBucket + 1) & mSizeMask;
    PLDHashEntryHdr* nextEntry = AddressEntry(nextBucket);

    // We are all done if the next entry is free.
    if (EntryIsFree(nextEntry)) {
      break;
    }

    // Calculate how far the next entry is from its desired bucket.
    uint32_t distance = (nextBucket - nextEntry->mKeyHash) & mSizeMask;

    // If our next entry is at its preferred location, then we are all done.
    if (distance == 0) {
      break;
    }

    // We need to shift the next entry into the empty bucket, and then
    // repeat the process.
    mOps->moveEntry(this, nextEntry, emptyEntry);
    // XXX moveEntry should really do this for us.
#ifdef BROKEN_HASH_KEYS
    emptyEntry->mKeyHash = nextEntry->mKeyHash;
#endif
    MarkEntryFree(nextEntry);

    MOZ_ASSERT(EntryIsLive(emptyEntry));

    emptyBucket = nextBucket;
    emptyEntry = nextEntry;
  }

  mEntryCount--;
}

// Shrink or compress if a quarter or more of all entries are removed, or if the
// table is underloaded according to the minimum alpha, and is not minimal-size
// already.
void
QMEHashTable::ShrinkIfAppropriate()
{
  uint32_t capacity = Capacity();
  if (capacity > kMinCapacity && mEntryCount <= MinLoad(capacity)) {
    uint32_t log2;
    QBestCapacity(mEntryCount, &capacity, &log2);

    int32_t deltaLog2 = log2 - (kHashBits - mHashShift);
    MOZ_ASSERT(deltaLog2 <= 0);

    (void) ChangeTable(deltaLog2);
  }
}

void
QMEHashTable::Remove(const void* aKey)
{
#ifdef DEBUG
  AutoWriteOp op(mChecker);
#endif

  PLDHashEntryHdr* entry = mEntryStore.Get()
                         ? SearchTable<ForSearchOrRemove>(aKey,
                                                          ComputeKeyHash(aKey))
                         : nullptr;
  if (entry) {
    RawRemove(entry);
    ShrinkIfAppropriate();
  }
}

void
QMEHashTable::RemoveEntry(PLDHashEntryHdr* aEntry)
{
#ifdef DEBUG
  AutoWriteOp op(mChecker);
#endif

  RawRemove(aEntry);
  ShrinkIfAppropriate();
}

size_t
QMEHashTable::ShallowSizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const
{
#ifdef DEBUG
  AutoReadOp op(mChecker);
#endif

  return aMallocSizeOf(mEntryStore.Get());
}

QMEHashTable::Iterator::Iterator(QMEHashTable* aTable)
  : mTable(aTable)
  , mStart(mTable->mEntryStore.Get())
  , mLimit(mTable->mEntryStore.Get() + mTable->Capacity() * mTable->mEntrySize)
  , mCurrent(mTable->mEntryStore.Get())
  , mNexts(0)
  , mNextsLimit(mTable->EntryCount())
  , mHaveRemoved(false)
  , mRemovedCurrentElement(false)
{
#ifdef DEBUG
  mTable->mChecker.StartReadOp();
#endif

  if (ChaosMode::isActive(ChaosFeature::HashTableIteration) &&
      mTable->Capacity() > 0) {
    // Start iterating at a random entry. It would be even more chaotic to
    // iterate in fully random order, but that's harder.
    mCurrent += ChaosMode::randomUint32LessThan(mTable->Capacity()) *
                mTable->mEntrySize;
  }

  // Advance to the first live entry, if there is one.
  if (!Done()) {
    while (IsOnNonLiveEntry()) {
      MoveToNextEntry();
    }
  }
}

QMEHashTable::Iterator::~Iterator()
{
  if (mTable) {
    if (mHaveRemoved) {
      mTable->ShrinkIfAppropriate();
    }
#ifdef DEBUG
    mTable->mChecker.EndReadOp();
#endif
  }
}

MOZ_ALWAYS_INLINE bool
QMEHashTable::Iterator::IsOnNonLiveEntry() const
{
  MOZ_ASSERT(!Done());
  return !EntryIsLive(reinterpret_cast<PLDHashEntryHdr*>(mCurrent));
}

MOZ_ALWAYS_INLINE void
QMEHashTable::Iterator::MoveToNextEntry()
{
  mCurrent += mTable->mEntrySize;
  if (mCurrent == mLimit) {
    mCurrent = mStart;  // Wrap-around. Possible due to Chaos Mode.
  }
}

void
QMEHashTable::Iterator::Next()
{
  MOZ_ASSERT(!Done());

  mNexts++;

  // Advance to the next live entry, if there is one.
  if (!Done()) {
    // If we removed the current element, then we might have moved a live
    // element into the current slot, and we need to check the current slot
    // first.
    if (mRemovedCurrentElement) {
      while (IsOnNonLiveEntry()) {
        MoveToNextEntry();
      }
    } else {
      do {
        MoveToNextEntry();
      } while(IsOnNonLiveEntry());
    }

    mRemovedCurrentElement = false;
  }
}

void
QMEHashTable::Iterator::Remove()
{
  // This cast is needed for the same reason as the one in the destructor.
  mTable->RawRemove(Get());
  mHaveRemoved = true;
  mRemovedCurrentElement = true;
}

#ifdef DEBUG
void
QMEHashTable::MarkImmutable()
{
  mChecker.SetNonWritable();
}
#endif

size_t
QMEHashTable::ShallowSizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const
{
  return aMallocSizeOf(this) + ShallowSizeOfExcludingThis(aMallocSizeOf);
}

/* static */ PLDHashNumber
QMEHashTable::HashVoidPtrKeyStub(const void* aKey)
{
  return nsPtrHashKey<void>::HashKey(aKey);
}

/* static */ bool
QMEHashTable::MatchEntryStub(const PLDHashEntryHdr* aEntry, const void* aKey)
{
  const PLDHashEntryStub* stub = (const PLDHashEntryStub*)aEntry;

  return stub->key == aKey;
}

/* static */ void
QMEHashTable::MoveEntryStub(QMEHashTable* aTable,
                            const PLDHashEntryHdr* aFrom,
                            PLDHashEntryHdr* aTo)
{
  memcpy(aTo, aFrom, aTable->mEntrySize);
}

/* static */ void
QMEHashTable::ClearEntryStub(QMEHashTable* aTable, PLDHashEntryHdr* aEntry)
{
  memset(aEntry, 0, aTable->mEntrySize);
}
