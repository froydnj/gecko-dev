/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "gtest/MozGTestBench.h"
#include "nsTArray.h"
#include "mozilla/HashFunctions.h"
#include "mozilla/OperatorNewExtensions.h"
#include "mozilla/XorShift128PlusRNG.h"
#include "PLDHashTable.h"
#include "QMEHashTable.h"
#include "nsString.h"
#include <unordered_set>

using namespace mozilla;

// A trivial hash function is good enough here. It's also super-fast for the
// GrowToMaxCapacity test because we insert the integers 0.., which means it's
// collision-free.
static PLDHashNumber
TrivialHash(const void* key)
{
  return (PLDHashNumber)(size_t(key) / 4);
}

static PLDHashNumber
GenericHash(const void* key)
{
  return HashGeneric(key);
}

static void
IntegerInitEntry(PLDHashEntryHdr* aEntry, const void* aKey)
{
  auto entry = static_cast<PLDHashEntryStub*>(aEntry);
  entry->key = aKey;
}

static const PLDHashTableOps trivialPOps = {
  TrivialHash,
  PLDHashTable::MatchEntryStub,
  PLDHashTable::MoveEntryStub,
  PLDHashTable::ClearEntryStub,
  IntegerInitEntry
};

static const PLDHashTableOps genericPOps = {
  GenericHash,
  PLDHashTable::MatchEntryStub,
  PLDHashTable::MoveEntryStub,
  PLDHashTable::ClearEntryStub,
  IntegerInitEntry
};

static const size_t kHashTableSize = 1000000;

template<typename T>
struct TableTypeFromOps;

template<>
struct TableTypeFromOps<PLDHashTableOps>
{
  typedef PLDHashTable Type;
};

template<>
struct TableTypeFromOps<QMEHashTableOps>
{
  typedef QMEHashTable Type;
};

template<typename OpsType>
static void
IntegerInsert(const OpsType* aOps, const nsTArray<uint64_t>& aIntegers)
{
  typedef typename TableTypeFromOps<OpsType>::Type TableType;

  // Try to eliminate as much resizing as possible by providing a default size.
  TableType table(aOps, sizeof(PLDHashEntryStub), 2*aIntegers.Length());

  for (const auto& n : mozilla::MakeSpan(aIntegers)) {
    bool success = table.Add(reinterpret_cast<const void*>(n), fallible);
    ASSERT_TRUE(success);
  }
}

static const QMEHashTableOps trivialQOps = {
  TrivialHash,
  QMEHashTable::MatchEntryStub,
  QMEHashTable::MoveEntryStub,
  QMEHashTable::ClearEntryStub,
  IntegerInitEntry,
};

static const QMEHashTableOps genericQOps = {
  GenericHash,
  QMEHashTable::MatchEntryStub,
  QMEHashTable::MoveEntryStub,
  QMEHashTable::ClearEntryStub,
  IntegerInitEntry,
};

class HashIntegers : public ::testing::Test {
protected:
  static void SetUpTestCase();
  static void TearDownTestCase();

  static const size_t kCount = 16;
  static nsTArray<uint64_t> sSortedIntegers;
  static nsTArray<uint64_t> sRandomIntegers;
};

nsTArray<uint64_t> HashIntegers::sSortedIntegers;
nsTArray<uint64_t> HashIntegers::sRandomIntegers;

/* static */ void
HashIntegers::SetUpTestCase()
{
  mozilla::non_crypto::XorShift128PlusRNG rng(0xdeadbeeffeedface, 0xc0ffeecafeba5edd);

  sSortedIntegers.SetCapacity(kCount);
  for (size_t i = 0; i < kCount; ++i) {
    sSortedIntegers.AppendElement(i);
  }

  sRandomIntegers.SetCapacity(kCount);
  for (size_t i = 0; i < kCount; ++i) {
    sRandomIntegers.AppendElement(rng.next());
  }
}

/*static*/ void
HashIntegers::TearDownTestCase()
{
  sSortedIntegers.Clear();
  sRandomIntegers.Clear();
}

MOZ_GTEST_BENCH_F(HashIntegers, PLD_Insert_Trivial_Random, []() {
    IntegerInsert(&trivialPOps, sRandomIntegers);
  });
MOZ_GTEST_BENCH_F(HashIntegers, QME_Insert_Trivial_Random, []() {
    IntegerInsert(&trivialQOps, sRandomIntegers);
  });
MOZ_GTEST_BENCH_F(HashIntegers, QME_Insert_Trivial_Sorted, []() {
    IntegerInsert(&trivialQOps, sSortedIntegers);
  });
MOZ_GTEST_BENCH_F(HashIntegers, PLD_Insert_Trivial_Sorted, []() {
    IntegerInsert(&trivialPOps, sSortedIntegers);
  });

MOZ_GTEST_BENCH_F(HashIntegers, QME_Insert_Generic_Random, []() {
    IntegerInsert(&genericQOps, sRandomIntegers);
  });
MOZ_GTEST_BENCH_F(HashIntegers, PLD_Insert_Generic_Random, []() {
    IntegerInsert(&genericPOps, sRandomIntegers);
  });
MOZ_GTEST_BENCH_F(HashIntegers, QME_Insert_Generic_Sorted, []() {
    IntegerInsert(&genericQOps, sSortedIntegers);
  });
MOZ_GTEST_BENCH_F(HashIntegers, PLD_Insert_Generic_Sorted, []() {
    IntegerInsert(&genericPOps, sSortedIntegers);
  });

static void
UnorderedSetIntegerInsert(const nsTArray<uint64_t>& aIntegers)
{
  const uint64_t* n = aIntegers.Elements();
  size_t count = aIntegers.Length();

  std::unordered_set<uint64_t> set{count * 2};

  for (size_t i = 0; i < count; ++i) {
    set.insert(n[i]);
  }
}

MOZ_GTEST_BENCH_F(HashIntegers, STD_Insert_Default_Random, []() {
    UnorderedSetIntegerInsert(sRandomIntegers);
  });
MOZ_GTEST_BENCH_F(HashIntegers, STD_Insert_Default_Sorted, []() {
    UnorderedSetIntegerInsert(sSortedIntegers);
  });

// Also test with mozilla::HashGeneric to see if we do any better.

struct hash_Generic
{
  size_t
  operator()(uint64_t val) const noexcept
  {
    return mozilla::HashGeneric(val);
  }
};

static void
UnorderedSetIntegerInsertMozilla(const nsTArray<uint64_t>& aIntegers)
{
  const uint64_t* n = aIntegers.Elements();
  size_t count = aIntegers.Length();

  std::unordered_set<uint64_t, hash_Generic> set{count * 2};

  for (size_t i = 0; i < count; ++i) {
    set.insert(n[i]);
  }
}

MOZ_GTEST_BENCH_F(HashIntegers, STD_Insert_Generic_Random, []() {
    UnorderedSetIntegerInsertMozilla(sRandomIntegers);
  });
MOZ_GTEST_BENCH_F(HashIntegers, STD_Insert_Generic_Sorted, []() {
    UnorderedSetIntegerInsertMozilla(sSortedIntegers);
  });

class HashStrings : public ::testing::Test {
protected:
  static void SetUpTestCase();
  static void TearDownTestCase();

  static const size_t kCount = 32;
  static nsTArray<nsCString> sSortedStrings;
  static nsTArray<nsCString> sRandomStrings;
};

nsTArray<nsCString> HashStrings::sSortedStrings;
nsTArray<nsCString> HashStrings::sRandomStrings;

static nsCString
GenerateRandomString(mozilla::non_crypto::XorShift128PlusRNG& rng, size_t aLength)
{
  nsAutoCStringN<32> s;
  union {
    uint64_t n;
    char c[8];
  } buffer;
  size_t bufferIndex = 0;

  s.SetCapacity(aLength);
  for (size_t i = 0; i < aLength; ++i) {
    if (i % 8 == 0) {
      buffer.n = rng.next();
      bufferIndex = 0;
    }
    s.Append(buffer.c[bufferIndex++]);
  }

  return s;
}

/*static*/ void
HashStrings::SetUpTestCase()
{
  mozilla::non_crypto::XorShift128PlusRNG rng(0xc0ffeecafeba5edd, 0xdeadbeeffeedface);

  sRandomStrings.SetCapacity(kCount);
  for (size_t i = 0; i < kCount; ++i) {
    sRandomStrings.AppendElement(GenerateRandomString(rng, 32));
  }

  sSortedStrings.AppendElements(sRandomStrings);
  sSortedStrings.Sort();
}

/*static*/ void
HashStrings::TearDownTestCase()
{
  sSortedStrings.Clear();
  sRandomStrings.Clear();
}

class StringBenchEntry : public PLDHashEntryHdr
{
public:
  StringBenchEntry(const nsCString* aStr)
    : mStr(*aStr)
  {};

  StringBenchEntry(StringBenchEntry&& aOther)
    : PLDHashEntryHdr(mozilla::Move(aOther))
    , mStr(mozilla::Move(aOther.mStr))
  {}
  ~StringBenchEntry() = default;

  bool Equals(const nsCString* aStr) const
  {
    return mStr.Equals(*aStr);
  }

private:
  nsCString mStr;
};

static PLDHashNumber
StringBenchHash(const void* key)
{
  auto* s = static_cast<const nsCString*>(key);
  return mozilla::HashString(s->get(), s->Length());
}

static bool
StringBenchMatch(const PLDHashEntryHdr* aEntry, const void* aKey)
{
  return static_cast<const StringBenchEntry*>(aEntry)->Equals(static_cast<const nsCString*>(aKey));
}

template<typename HashTableType>
static void
StringBenchMove(HashTableType* aTable, const PLDHashEntryHdr* aFrom,
                PLDHashEntryHdr* aTo)
{
  StringBenchEntry* fromEntry = const_cast<StringBenchEntry*>(static_cast<const StringBenchEntry*>(aFrom));

  new (mozilla::KnownNotNull, aTo) StringBenchEntry(mozilla::Move(*fromEntry));

  fromEntry->~StringBenchEntry();
}

template<typename HashTableType>
static void
StringBenchClear(HashTableType* aTable, PLDHashEntryHdr* aEntry)
{
  static_cast<StringBenchEntry*>(aEntry)->~StringBenchEntry();
}

static void
StringBenchInit(PLDHashEntryHdr* aEntry, const void* aKey)
{
  new (mozilla::KnownNotNull, aEntry) StringBenchEntry(static_cast<const nsCString*>(aKey));
}

static const PLDHashTableOps stringPOps = {
  StringBenchHash,
  StringBenchMatch,
  StringBenchMove<PLDHashTable>,
  StringBenchClear<PLDHashTable>,
  StringBenchInit,
};

static const QMEHashTableOps stringQOps = {
  StringBenchHash,
  StringBenchMatch,
  StringBenchMove<QMEHashTable>,
  StringBenchClear<QMEHashTable>,
  StringBenchInit,
};

template<typename OpsType>
static void
StringInsert(const OpsType* aOps, const nsTArray<nsCString>& aStrings)
{
  typedef typename TableTypeFromOps<OpsType>::Type TableType;

  size_t count = aStrings.Length();
  TableType table(aOps, sizeof(StringBenchEntry), count * 2);

  for (const auto& s : mozilla::MakeSpan(aStrings)) {
    bool success = table.Add(&s, fallible);
    ASSERT_TRUE(success);
  }
}

MOZ_GTEST_BENCH_F(HashStrings, PLD_Insert_Random, []() {
    StringInsert(&stringPOps, sRandomStrings);
  });

MOZ_GTEST_BENCH_F(HashStrings, QME_Insert_Random, []() {
    StringInsert(&stringQOps, sRandomStrings);
  });

namespace std {

template<>
struct hash<nsCString>
{
  size_t operator()(const nsCString& str) const noexcept
  {
    return mozilla::HashString(str.get(), str.Length());
  }
};

} // namespace std

static void
UnorderedStringSetBench(const nsTArray<nsCString>& aStrings)
{
  size_t count = aStrings.Length();

  std::unordered_set<nsCString> set{count*2};

  for (const auto& s : mozilla::MakeSpan(aStrings)) {
    set.insert(s);
  }
}

MOZ_GTEST_BENCH_F(HashStrings, STD_Insert_Random, []() {
    UnorderedStringSetBench(sRandomStrings);
  });

template<typename OpsType>
static void
StringSearch(const OpsType* aOps, const nsTArray<nsCString>& aStrings)
{
  typedef typename TableTypeFromOps<OpsType>::Type TableType;

  // Try to eliminate as much resizing as possible by providing a default size.
  TableType table(aOps, sizeof(StringBenchEntry), 2*aStrings.Length());

  for (const auto& s : mozilla::MakeSpan(aStrings)) {
    bool success = table.Add(&s, fallible);
    ASSERT_TRUE(success);
  }

  for (const auto& s : mozilla::MakeSpan(aStrings)) {
    ASSERT_TRUE(!!table.Search(&s));
  }
}

MOZ_GTEST_BENCH_F(HashStrings, PLD_Search_Random, []() {
    StringSearch(&stringPOps, sRandomStrings);
  });
MOZ_GTEST_BENCH_F(HashStrings, QME_Search_Random, []() {
    StringSearch(&stringQOps, sRandomStrings);
  });

// XXX should we separate out the insertion from the searching?
template<typename OpsType>
static void
SearchBench(const OpsType* aOps, const nsTArray<uint64_t>& aIntegers)
{
  typedef typename TableTypeFromOps<OpsType>::Type TableType;

  // Try to eliminate as much resizing as possible by providing a default size.
  TableType table(aOps, sizeof(PLDHashEntryStub), 2*aIntegers.Length());

  for (const auto& n : mozilla::MakeSpan(aIntegers)) {
    bool success = table.Add(reinterpret_cast<const void*>(n), fallible);
    ASSERT_TRUE(success);
  }

  for (const auto& n : mozilla::MakeSpan(aIntegers)) {
    ASSERT_TRUE(!!table.Search(reinterpret_cast<const void*>(n)));
  }
}

MOZ_GTEST_BENCH_F(HashIntegers, QME_Search_Generic_Random, []() {
    SearchBench(&genericQOps, sRandomIntegers);
  });
MOZ_GTEST_BENCH_F(HashIntegers, PLD_Search_Generic_Random, []() {
    SearchBench(&genericPOps, sRandomIntegers);
  });

// Test failing searches as well.
template<typename OpsType>
static void
FailingSearchBench(const OpsType* aOps, const nsTArray<uint64_t>& aIntegers)
{
  typedef typename TableTypeFromOps<OpsType>::Type TableType;

  size_t count = aIntegers.Length();
  TableType table(aOps, sizeof(PLDHashEntryStub), count);

  for (const auto& n : mozilla::MakeSpan(aIntegers).From(count/2)) {
    bool success = table.Add(reinterpret_cast<const void*>(n), fallible);
    ASSERT_TRUE(success);
  }

  for (const auto& n : mozilla::MakeSpan(aIntegers).To(count / 2)) {
    ASSERT_TRUE(!table.Search(reinterpret_cast<const void*>(n)));
  }
}

MOZ_GTEST_BENCH_F(HashIntegers, QME_FailingSearch_Generic_Random, []() {
    FailingSearchBench(&genericQOps, sRandomIntegers);
  });
MOZ_GTEST_BENCH_F(HashIntegers, PLD_FailingSearch_Generic_Random, []() {
    FailingSearchBench(&genericPOps, sRandomIntegers);
  });

template<typename OpsType>
static void
RemoveBench(const OpsType* aOps, const nsTArray<uint64_t>& aIntegers)
{
  typedef typename TableTypeFromOps<OpsType>::Type TableType;

  // Try to eliminate as much resizing as possible by providing a default size.
  TableType table(aOps, sizeof(PLDHashEntryStub), 2*aIntegers.Length());

  for (const auto& n : mozilla::MakeSpan(aIntegers)) {
    bool success = table.Add(reinterpret_cast<const void*>(n), fallible);
    ASSERT_TRUE(success);
  }

  for (const auto& n : mozilla::MakeSpan(aIntegers)) {
    table.Remove(reinterpret_cast<const void*>(n));
  }
}

MOZ_GTEST_BENCH_F(HashIntegers, QME_Remove_Generic_Random, []() {
    RemoveBench(&genericQOps, sRandomIntegers);
  });
MOZ_GTEST_BENCH_F(HashIntegers, PLD_Remove_Generic_Random, []() {
    RemoveBench(&genericPOps, sRandomIntegers);
  });


template<typename OpsType>
static void
RawRemoveBench(const OpsType* aOps, const nsTArray<uint64_t>& aIntegers)
{
  typedef typename TableTypeFromOps<OpsType>::Type TableType;

  // Try to eliminate as much resizing as possible by providing a default size.
  TableType table(aOps, sizeof(PLDHashEntryStub), 2*aIntegers.Length());

  for (const auto& n : mozilla::MakeSpan(aIntegers)) {
    bool success = table.Add(reinterpret_cast<const void*>(n), fallible);
    ASSERT_TRUE(success);
  }

  for (const auto& n : mozilla::MakeSpan(aIntegers)) {
    auto* e = table.Search(reinterpret_cast<const void*>(n));
    ASSERT_TRUE(!!e);
    table.RawRemove(e);
  }
}

MOZ_GTEST_BENCH_F(HashIntegers, QME_RawRemove_Generic_Random, []() {
    RawRemoveBench(&genericQOps, sRandomIntegers);
  });
MOZ_GTEST_BENCH_F(HashIntegers, PLD_RawRemove_Generic_Random, []() {
    RawRemoveBench(&genericPOps, sRandomIntegers);
  });

template<typename OpsType>
static void
IterateBench(const OpsType* aOps, const nsTArray<uint64_t>& aIntegers)
{
  typedef typename TableTypeFromOps<OpsType>::Type TableType;

  // Try to eliminate as much resizing as possible by providing a default size.
  TableType table(aOps, sizeof(PLDHashEntryStub), 2*aIntegers.Length());

  for (const auto& n : mozilla::MakeSpan(aIntegers)) {
    bool success = table.Add(reinterpret_cast<const void*>(n), fallible);
    ASSERT_TRUE(success);
  }

  for (auto iter = table.Iter(); !iter.Done(); iter.Next()) {
    auto* e = iter.Get();
    ASSERT_TRUE(!!e);
  }
}

MOZ_GTEST_BENCH_F(HashIntegers, QME_Iterate_Generic_Random, []() {
    IterateBench(&genericQOps, sRandomIntegers);
  });
MOZ_GTEST_BENCH_F(HashIntegers, PLD_Iterate_Generic_Random, []() {
    IterateBench(&genericPOps, sRandomIntegers);
  });

template<typename OpsType>
static void
IterateRemoveBench(const OpsType* aOps, const nsTArray<uint64_t>& aIntegers)
{
  typedef typename TableTypeFromOps<OpsType>::Type TableType;

  // Try to eliminate as much resizing as possible by providing a default size.
  TableType table(aOps, sizeof(PLDHashEntryStub), 2*aIntegers.Length());

  for (const auto& n : mozilla::MakeSpan(aIntegers)) {
    bool success = table.Add(reinterpret_cast<const void*>(n), fallible);
    ASSERT_TRUE(success);
  }

  for (auto iter = table.Iter(); !iter.Done(); iter.Next()) {
    auto* e = iter.Get();
    ASSERT_TRUE(!!e);
    iter.Remove();
  }
}

MOZ_GTEST_BENCH_F(HashIntegers, QME_IterateRemove_Generic_Random, []() {
    IterateRemoveBench(&genericQOps, sRandomIntegers);
  });
MOZ_GTEST_BENCH_F(HashIntegers, PLD_IterateRemove_Generic_Random, []() {
    IterateRemoveBench(&genericPOps, sRandomIntegers);
  });

TEST(QMEHashTable, Insertion)
{
  QMEHashTable t(&trivialQOps, sizeof(PLDHashEntryStub));

  // First, we insert 64 items, which results in a capacity of 128, and a load
  // factor of 50%.
  for (intptr_t i = 0; i < 64; i++) {
    t.Add((const void*)i);
  }
  ASSERT_EQ(t.EntryCount(), 64u);
  ASSERT_EQ(t.Capacity(), 128u);

  // Verify that we can find everything.
  for (intptr_t i = 0; i < 64; ++i) {
    PLDHashEntryHdr* e = t.Search((const void*)i);
    EXPECT_TRUE(e) << "Couldn't find value " << i;
  }

  // Insert another 256 items.
  for (intptr_t i = 64; i < 320; i++) {
    t.Add((const void*)i);
  }
  ASSERT_EQ(t.EntryCount(), 320u);

  for (intptr_t i = 64; i < 320; ++i) {
    PLDHashEntryHdr* e = t.Search((const void*)i);
    ASSERT_TRUE(e);
  }

  // Test that removal works.
  for (intptr_t i = 0; i < 320; ++i) {
    t.Remove((const void*)i);

    PLDHashEntryHdr* e = t.Search((const void*)i);
    ASSERT_FALSE(e);

    // Every so often, make sure that we can find everything else in the hash
    // table that we haven't deleted, to ensure that Remove() hasn't
    // completely botched the table structure.
    for (intptr_t j = i + 1; j < 320; ++j) {
      PLDHashEntryHdr* e = t.Search((const void*)j);
      ASSERT_TRUE(e);
    }

    // Also make sure we can't find anything else.
    for (intptr_t j = 0; j < i; ++j) {
      PLDHashEntryHdr* e = t.Search((const void*)j);
      ASSERT_FALSE(e);
    }
  }
}
