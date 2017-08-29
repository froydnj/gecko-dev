/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "gtest/MozGTestBench.h"
#include "PLDHashTable.h"
#include "QMEHashTable.h"

using namespace mozilla;

// A trivial hash function is good enough here. It's also super-fast for the
// GrowToMaxCapacity test because we insert the integers 0.., which means it's
// collision-free.
static PLDHashNumber
TrivialHash(const void *key)
{
  return (PLDHashNumber)(size_t(key) / 4);
}

static void
TrivialInitEntry(PLDHashEntryHdr* aEntry, const void* aKey)
{
  auto entry = static_cast<PLDHashEntryStub*>(aEntry);
  entry->key = aKey;
}

static const PLDHashTableOps trivialPOps = {
  TrivialHash,
  PLDHashTable::MatchEntryStub,
  PLDHashTable::MoveEntryStub,
  PLDHashTable::ClearEntryStub,
  TrivialInitEntry
};

static const size_t kHashTableSize = 1000000;

static void PLDHashBench()
{
  PLDHashTable table(&trivialPOps, sizeof(PLDHashEntryStub), kHashTableSize);

  for (size_t i = 0; i < kHashTableSize; ++i) {
    bool success = table.Add(reinterpret_cast<const void*>(i), fallible);
    ASSERT_TRUE(success);
  }
}

static const QMEHashTableOps trivialQOps = {
  TrivialHash,
  QMEHashTable::MatchEntryStub,
  QMEHashTable::MoveEntryStub,
  QMEHashTable::ClearEntryStub,
  TrivialInitEntry,
};

static void QMEHashBench()
{
  QMEHashTable table(&trivialQOps, sizeof(PLDHashEntryStub), kHashTableSize);

  for (size_t i = 0; i < kHashTableSize; ++i) {
    bool success = table.Add(reinterpret_cast<const void*>(i), fallible);
    ASSERT_TRUE(success);
  }
}

MOZ_GTEST_BENCH(XPCOM, XPCOM_QMEHashTable_Bench, QMEHashBench);
MOZ_GTEST_BENCH(XPCOM, XPCOM_PLDHashTable_Bench, PLDHashBench);

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
