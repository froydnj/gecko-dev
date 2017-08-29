/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef QMEHashTable_h
#define QMEHashTable_h

#include "PLDHashTable.h"

// A PLDHashTable-alike, but implemented using Robin Hood hashing, rather
// than double hashing.

struct QMEHashTableOps;

class QMEHashTable
{
  // XXX copied from PLDHashTable
private:
  // This class maintains the invariant that every time the entry store is
  // changed, the generation is updated.
  //
  // Note: It would be natural to store the generation within this class, but
  // we can't do that without bloating sizeof(PLDHashTable) on 64-bit machines.
  // So instead we store it outside this class, and Set() takes a pointer to it
  // and ensures it is updated as necessary.
  class EntryStore
  {
  private:
    char* mEntryStore;

  public:
    EntryStore() : mEntryStore(nullptr) {}

    ~EntryStore()
    {
      free(mEntryStore);
      mEntryStore = nullptr;
    }

    char* Get() { return mEntryStore; }
    const char* Get() const { return mEntryStore; }

    void Set(char* aEntryStore, uint16_t* aGeneration)
    {
      mEntryStore = aEntryStore;
      *aGeneration += 1;
    }
  };

  const QMEHashTableOps* const mOps;  // Virtual operations; see below.
  uint16_t            mGeneration;    // The storage generation.
  uint8_t             mHashShift;     // Multiplicative hash shift.
  const uint8_t       mEntrySize;     // Number of bytes in an entry.
  uint32_t            mEntryCount;    // Number of entries in table.

  EntryStore          mEntryStore;    // (Lazy) entry storage and generation.
  uint32_t            mSizeMask;

  // During insertion, we need to swap the currently-inserting entry with an
  // entry already in the table.  Most normal hashtable implementations would
  // implement this operation in the obvious way, but in QMEHashTable, the
  // caller owns the memory for the inserted entry and therefore we can't
  // scribble over it.  So we need a small entry that is obviously ours.
  //
  // XXX: a better design for this class would obviate the need for this.
  void*               mTemporaryEntry;

#ifdef DEBUG
  mutable Checker mChecker;
#endif

public:
  // Table capacity limit; do not exceed. The max capacity used to be 1<<23 but
  // that occasionally that wasn't enough. Making it much bigger than 1<<26
  // probably isn't worthwhile -- tables that big are kind of ridiculous.
  // Also, the growth operation will (deliberately) fail if |capacity *
  // mEntrySize| overflows a uint32_t, and mEntrySize is always at least 8
  // bytes.
  static const uint32_t kMaxCapacity = ((uint32_t)1 << 26);

  static const uint32_t kMinCapacity = 8;

  // Making this half of kMaxCapacity ensures it'll fit. Nobody should need an
  // initial length anywhere nearly this large, anyway.
  static const uint32_t kMaxInitialLength = kMaxCapacity / 2;

  // This gives a default initial capacity of 8.
  static const uint32_t kDefaultInitialLength = 4;

  // Initialize the table with |aOps| and |aEntrySize|. The table's initial
  // capacity is chosen such that |aLength| elements can be inserted without
  // rehashing; if |aLength| is a power-of-two, this capacity will be
  // |2*length|. However, because entry storage is allocated lazily, this
  // initial capacity won't be relevant until the first element is added; prior
  // to that the capacity will be zero.
  //
  // This will crash if |aEntrySize| and/or |aLength| are too large.
  QMEHashTable(const QMEHashTableOps* aOps, uint32_t aEntrySize,
	       uint32_t aLength = PLDHashTable::kDefaultInitialLength);

  QMEHashTable(QMEHashTable&& aOther)
      // These two fields are |const|. Initialize them here because the
      // move assignment operator cannot modify them.
    : mOps(aOther.mOps)
    , mEntrySize(aOther.mEntrySize)
      // Initialize this field because it is required for a safe call to the
      // destructor, which the move assignment operator does.
    , mEntryStore()
#ifdef DEBUG
    , mChecker()
#endif
  {
    *this = mozilla::Move(aOther);
  }

  QMEHashTable& operator=(QMEHashTable&& aOther);

  ~QMEHashTable();

  // Shouldn't normally need this.
  const QMEHashTableOps* Ops() const { return mOps; }

  // Total number of potential entries.
  uint32_t Capacity() const
  {
    return mEntryStore.Get() ? CapacityFromHashShift() : 0;
  }

  uint32_t EntrySize()  const { return mEntrySize; }
  uint32_t EntryCount() const { return mEntryCount; }
  uint32_t Generation() const { return mGeneration; }

  PLDHashEntryHdr* Search(const void* aKey);

  PLDHashEntryHdr* Add(const void* aKey, const mozilla::fallible_t&);
  PLDHashEntryHdr* Add(const void* aKey);

  void Remove(const void* aKey);
  void RemoveEntry(PLDHashEntryHdr* aEntry);

  void RawRemove(PLDHashEntryHdr* aEntry);

  // This function is equivalent to
  // ClearAndPrepareForLength(kDefaultInitialLength).
  void Clear();

  // This function clears the table's contents and frees its entry storage,
  // leaving it in a empty state ready to be used again. Afterwards, when the
  // first element is added the entry storage that gets allocated will have a
  // capacity large enough to fit |aLength| elements without rehashing.
  //
  // It's conceptually the same as calling the destructor and then re-calling
  // the constructor with the original |aOps| and |aEntrySize| arguments, and
  // a new |aLength| argument.
  void ClearAndPrepareForLength(uint32_t aLength);

  // Measure the size of the table's entry storage. If the entries contain
  // pointers to other heap blocks, you have to iterate over the table and
  // measure those separately; hence the "Shallow" prefix.
  size_t ShallowSizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf) const;

  // Like ShallowSizeOfExcludingThis(), but includes sizeof(*this).
  size_t ShallowSizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf) const;

#ifdef DEBUG
  // Mark a table as immutable for the remainder of its lifetime. This
  // changes the implementation from asserting one set of invariants to
  // asserting a different set.
  void MarkImmutable();
#endif

  // TODO: stub operations

  // The individual stub operations in StubOps().
  static PLDHashNumber HashVoidPtrKeyStub(const void* aKey);
  static bool MatchEntryStub(const PLDHashEntryHdr* aEntry, const void* aKey);
  static void MoveEntryStub(QMEHashTable* aTable, const PLDHashEntryHdr* aFrom,
                            PLDHashEntryHdr* aTo);
  static void ClearEntryStub(QMEHashTable* aTable, PLDHashEntryHdr* aEntry);

  // This is an iterator for QMEHashtable. Assertions will detect some, but not
  // all, mid-iteration table modifications that might invalidate (e.g.
  // reallocate) the entry storage.
  //
  // Any element can be removed during iteration using Remove(). If any
  // elements are removed, the table may be resized once iteration ends.
  //
  // Example usage:
  //
  //   for (auto iter = table.Iter(); !iter.Done(); iter.Next()) {
  //     auto entry = static_cast<FooEntry*>(iter.Get());
  //     // ... do stuff with |entry| ...
  //     // ... possibly call iter.Remove() once ...
  //   }
  //
  // or:
  //
  //   for (QMEHashTable::Iterator iter(&table); !iter.Done(); iter.Next()) {
  //     auto entry = static_cast<FooEntry*>(iter.Get());
  //     // ... do stuff with |entry| ...
  //     // ... possibly call iter.Remove() once ...
  //   }
  //
  // The latter form is more verbose but is easier to work with when
  // making subclasses of Iterator.
  //
  class Iterator
  {
  public:
    explicit Iterator(QMEHashTable* aTable);
    Iterator(Iterator&& aOther);
    ~Iterator();

    // Have we finished?
    bool Done() const { return mNexts == mNextsLimit; }

    // Get the current entry.
    PLDHashEntryHdr* Get() const
    {
      MOZ_ASSERT(!Done());

      PLDHashEntryHdr* entry = reinterpret_cast<PLDHashEntryHdr*>(mCurrent);
      MOZ_ASSERT(EntryIsLive(entry));
      return entry;
    }

    // Advance to the next entry.
    void Next();

    // Remove the current entry. Must only be called once per entry, and Get()
    // must not be called on that entry afterwards.
    void Remove();

  protected:
    QMEHashTable* mTable;             // Main table pointer.

  private:
    char* mStart;                     // The first entry.
    char* mLimit;                     // One past the last entry.
    char* mCurrent;                   // Pointer to the current entry.
    uint32_t mNexts;                  // Number of Next() calls.
    uint32_t mNextsLimit;             // Next() call limit.

    bool mHaveRemoved;                // Have any elements been removed?
    bool mRemovedCurrentElement;      // Did we just remove the element we were on?

    bool IsOnNonLiveEntry() const;
    void MoveToNextEntry();

    Iterator() = delete;
    Iterator(const Iterator&) = delete;
    Iterator& operator=(const Iterator&) = delete;
    Iterator& operator=(const Iterator&&) = delete;
  };

  Iterator Iter() { return Iterator(this); }

  // Use this if you need to initialize an Iterator in a const method. If you
  // use this case, you should not call Remove() on the iterator.
  Iterator ConstIter() const
  {
    return Iterator(const_cast<QMEHashTable*>(this));
  }

private:
  // Multiplicative hash uses an unsigned 32 bit integer and the golden ratio,
  // expressed as a fixed-point 32-bit fraction.
  static const uint32_t kHashBits = 32;
  static const uint32_t kGoldenRatio = 0x9E3779B9U;

  static uint32_t HashShift(uint32_t aEntrySize, uint32_t aLength);

  static bool EntryIsFree(PLDHashEntryHdr* aEntry)
  {
    return aEntry->mKeyHash == 0;
  }
  static bool EntryIsLive(PLDHashEntryHdr* aEntry)
  {
    return !EntryIsFree(aEntry);
  }

  static void MarkEntryFree(PLDHashEntryHdr* aEntry)
  {
    aEntry->mKeyHash = 0;
  }

  uint32_t IdealBucketIndex(PLDHashNumber aHash);

  static bool MatchEntryKeyhash(PLDHashEntryHdr* aEntry, PLDHashNumber aHash);
  PLDHashEntryHdr* AddressEntry(uint32_t aIndex);
  uint32_t EntryToBucketIndex(PLDHashEntryHdr* aEntry);

  // We store mHashShift rather than sizeLog2 to optimize the collision-free
  // case in SearchTable.
  uint32_t CapacityFromHashShift() const
  {
    return ((uint32_t)1 << (kHashBits - mHashShift));
  }

  PLDHashNumber ComputeKeyHash(const void* aKey);

  enum SearchReason { ForSearchOrRemove, ForAdd, ForAddDuringResize };

  template <SearchReason Reason>
  PLDHashEntryHdr*
    SearchTable(const void* aKey, PLDHashNumber aKeyHash);

  PLDHashEntryHdr* FindFreeEntry(PLDHashNumber aKeyHash);

  bool ChangeTable(int aDeltaLog2);

  void ShrinkIfAppropriate();

  QMEHashTable(const QMEHashTable& aOther) = delete;
  QMEHashTable& operator=(const QMEHashTable& aOther) = delete;
};

// Compute the hash code for a given key to be looked up, added, or removed.
// A hash code may have any QMEHashNumber value.
typedef PLDHashNumber (*QMEHashHashKey)(const void* aKey);

// Compare the key identifying aEntry with the provided key parameter. Return
// true if keys match, false otherwise.
typedef bool (*QMEHashMatchEntry)(const PLDHashEntryHdr* aEntry,
                                  const void* aKey);

// Copy the data starting at aFrom to the new entry storage at aTo. Do not add
// reference counts for any strong references in the entry, however, as this
// is a "move" operation: the old entry storage at from will be freed without
// any reference-decrementing callback shortly.
typedef void (*QMEHashMoveEntry)(QMEHashTable* aTable,
                                 const PLDHashEntryHdr* aFrom,
                                 PLDHashEntryHdr* aTo);

// Clear the entry and drop any strong references it holds. This callback is
// invoked by Remove(), but only if the given key is found in the table.
typedef void (*QMEHashClearEntry)(QMEHashTable* aTable,
                                  PLDHashEntryHdr* aEntry);

// Initialize a new entry, apart from mKeyHash. This function is called when
// Add() finds no existing entry for the given key, and must add a new one. At
// that point, |aEntry->mKeyHash| is not set yet, to avoid claiming the last
// free entry in a severely overloaded table.
typedef void (*QMEHashInitEntry)(PLDHashEntryHdr* aEntry, const void* aKey);

// Swap the entries given in aLhs and aRhs.
typedef void (*QMEHashSwapEntry)(PLDHashEntryHdr* aLhs, PLDHashEntryHdr* aRhs);

// Finally, the "vtable" structure for QMEHashTable. The first four hooks
// must be provided by implementations; they're called unconditionally by the
// generic QMEHashTable.cpp code. Hooks after these may be null.
//
// Summary of allocation-related hook usage with C++ placement new emphasis:
//  initEntry           Call placement new using default key-based ctor.
//  moveEntry           Call placement new using copy ctor, run dtor on old
//                      entry storage.  Many hooks will blithely ignore
//                      constness and call placement new using move ctor.
//  clearEntry          Run dtor on entry.
//
// Note the reason why initEntry is optional: the default hooks (stubs) clear
// entry storage:  On successful Add(tbl, key), the returned entry pointer
// addresses an entry struct whose mKeyHash member has been set non-zero, but
// all other entry members are still clear (null). Add() callers can test such
// members to see whether the entry was newly created by the Add() call that
// just succeeded. If placement new or similar initialization is required,
// define an |initEntry| hook. Of course, the |clearEntry| hook must zero or
// null appropriately.
//
// XXX assumes 0 is null for pointer types.
struct QMEHashTableOps
{
  // Mandatory hooks. All implementations must provide these.
  QMEHashHashKey      hashKey;
  QMEHashMatchEntry   matchEntry;
  QMEHashMoveEntry    moveEntry;
  QMEHashClearEntry   clearEntry;

  // Optional hooks start here. If null, these are not called.
  QMEHashInitEntry    initEntry;
};

// A minimal entry is a subclass of QMEHashEntryHdr and has a void* key pointer.
struct QMEHashEntryStub : public PLDHashEntryHdr
{
  const void* key;
};

#endif // QMEHashTable_h
