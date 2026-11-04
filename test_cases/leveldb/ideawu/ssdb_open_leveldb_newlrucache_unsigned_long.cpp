#include "leveldb/env.h"
#include "leveldb/iterator.h"
#include "leveldb/cache.h"
#include "leveldb/filter_policy.h"
#include <stdint.h>
#include <cstdio>
#include <cstddef>
#include <string>
#include <vector>

// Sequence selected: Options, NewBloomFilterPolicy, NewLRUCache, CopyState
// Note: CopyState is not a leveldb API, so it is not called here.

// ---------------------------------------------------------------------------
// Debug helpers (write to stdout, flushed immediately so ordering is preserved)
// ---------------------------------------------------------------------------
#define DBG(...)                       \
  do {                                 \
    printf("[DBG] " __VA_ARGS__);      \
    printf("\n");                      \
    fflush(stdout);                    \
  } while (0)

static const char *B(bool b) { return b ? "true" : "false"; }

// Local options holder that mirrors the cache_size field used by the original test.
struct Options {
  size_t cache_size;
};

// Counts how many cached entries have been destroyed (erased, evicted, or cache deleted).
static size_t g_deleted_count = 0;

// No-op deleter: the cached value is owned by the test, so only bookkeeping is done.
static void NoopDeleter(const leveldb::Slice &key, void *value) {
  DBG("NoopDeleter invoked: key='%.*s' value_ptr=%p (g_deleted_count before increment=%zu)",
      (int)key.size(), key.data(), value, g_deleted_count);
  (void)key;
  (void)value;
  g_deleted_count++;
  DBG("NoopDeleter: g_deleted_count now = %zu", g_deleted_count);
}

int Test_open() {
  DBG("==== Test_open START ====");
  bool ok = true;
  DBG("Initial ok = %s", B(ok));

  printf("Step 1: Initialise options\n");
  DBG("Entering Step 1");
  Options opt;
  opt.cache_size = 8;
  printf("  cache_size (MB) = %zu\n", opt.cache_size);
  DBG("Step 1 complete: opt.cache_size = %zu", opt.cache_size);

  printf("Step 2: Call leveldb::NewBloomFilterPolicy(10)\n");
  DBG("Calling leveldb::NewBloomFilterPolicy(10)");
  const leveldb::FilterPolicy *bloom_filter_policy = leveldb::NewBloomFilterPolicy(10);
  printf("  bloom_filter_policy = %p\n", (const void *)bloom_filter_policy);
  DBG("NewBloomFilterPolicy returned %p", (const void *)bloom_filter_policy);

  printf("Step 3: Call leveldb::NewLRUCache(unsigned long)\n");
  const int kBytesPerMegabyte = 1048576;
  size_t capacity = opt.cache_size * kBytesPerMegabyte;
  printf("  capacity (bytes) = %zu\n", capacity);
  DBG("Computed capacity = %zu * %d = %zu bytes", opt.cache_size, kBytesPerMegabyte, capacity);
  DBG("Calling leveldb::NewLRUCache(%zu)", capacity);
  leveldb::Cache *block_cache = leveldb::NewLRUCache(capacity);
  printf("NewLRUCache(%zu) was executed, return value = %p\n",
         capacity, (void *)block_cache);
  DBG("NewLRUCache returned %p", (void *)block_cache);

  // Documented behaviour: a new non-null cache object is returned.
  bool null_check = (block_cache == nullptr || bloom_filter_policy == nullptr);
  DBG("Condition [block_cache == nullptr || bloom_filter_policy == nullptr] = %s",
      B(null_check));
  if (null_check) {
    DBG("BRANCH TAKEN: null-pointer failure path");
    printf("FAIL: NewLRUCache or NewBloomFilterPolicy returned nullptr\n");
    DBG("Deleting bloom_filter_policy (%p) on failure path", (const void *)bloom_filter_policy);
    delete bloom_filter_policy;
    DBG("Deleted bloom_filter_policy on failure path");
    printf("Test_open result = -1\n");
    DBG("==== Test_open END (early return) result = -1 ====");
    return -1;
  } else {
    DBG("BRANCH TAKEN: non-null pointers, continuing");
  }

  // Documented behaviour: a freshly created cache holds no charge.
  DBG("Calling block_cache->TotalCharge() for initial check");
  size_t initial_charge = block_cache->TotalCharge();
  DBG("TotalCharge() returned %zu", initial_charge);
  bool initial_charge_bad = (initial_charge != 0);
  DBG("Condition [TotalCharge() != 0] = %s", B(initial_charge_bad));
  if (initial_charge_bad) {
    DBG("BRANCH TAKEN: initial charge mismatch");
    printf("FAIL: new cache TotalCharge = %zu, expected 0\n", initial_charge);
    ok = false;
    DBG("ok set to false");
  } else {
    DBG("BRANCH TAKEN: initial charge is 0 (expected)");
    printf("PASS: new cache TotalCharge is 0\n");
  }

  printf("Step 4: Insert an entry into the cache via Insert()\n");
  DBG("Entering Step 4");
  std::string key_str = "test_key";
  leveldb::Slice key(key_str);
  DBG("Created key slice: '%s' (size=%zu)", key_str.c_str(), key.size());
  int value = 42;
  DBG("Calling block_cache->Insert(key, &value=%p, charge=1, NoopDeleter)", (void *)&value);
  leveldb::Cache::Handle *handle = block_cache->Insert(key, &value, 1, &NoopDeleter);
  DBG("Insert returned handle = %p", (void *)handle);
  bool handle_null = (handle == nullptr);
  DBG("Condition [handle == nullptr] = %s", B(handle_null));
  if (handle_null) {
    DBG("BRANCH TAKEN: Insert returned null handle");
    printf("FAIL: Insert returned null handle\n");
    ok = false;
    DBG("ok set to false");
  } else {
    DBG("BRANCH TAKEN: Insert returned valid handle");
    printf("  Insert succeeded, total charge = %zu\n", block_cache->TotalCharge());
    DBG("Calling TotalCharge() after insert");
    size_t charge_after_insert = block_cache->TotalCharge();
    DBG("TotalCharge() returned %zu", charge_after_insert);
    bool charge_bad = (charge_after_insert != 1);
    DBG("Condition [TotalCharge() != 1] = %s", B(charge_bad));
    if (charge_bad) {
      DBG("BRANCH TAKEN: charge after insert mismatch");
      printf("FAIL: TotalCharge after insert = %zu, expected 1\n", charge_after_insert);
      ok = false;
      DBG("ok set to false");
    } else {
      DBG("BRANCH TAKEN: charge after insert is 1 (expected)");
    }
    DBG("Calling Release(handle=%p) for insert handle", (void *)handle);
    block_cache->Release(handle);
    printf("  Released insert handle\n");
    DBG("Release(insert handle) complete");
  }

  printf("Step 5: Lookup the entry and verify its value\n");
  DBG("Entering Step 5");
  DBG("Calling block_cache->Lookup(key='%s')", key_str.c_str());
  leveldb::Cache::Handle *found = block_cache->Lookup(key);
  DBG("Lookup returned handle = %p", (void *)found);
  bool found_null = (found == nullptr);
  DBG("Condition [found == nullptr] = %s", B(found_null));
  if (found_null) {
    DBG("BRANCH TAKEN: lookup failed");
    printf("FAIL: Lookup failed: entry not found\n");
    ok = false;
    DBG("ok set to false");
  } else {
    DBG("BRANCH TAKEN: lookup succeeded");
    DBG("Calling Value(found=%p)", (void *)found);
    int *stored = static_cast<int *>(block_cache->Value(found));
    DBG("Value returned pointer %p", (void *)stored);
    if (stored) {
      DBG("Stored value dereferenced = %d", *stored);
    }
    printf("  Lookup found value = %d\n", (stored ? *stored : -1));
    bool value_bad = (stored == nullptr || *stored != 42);
    DBG("Condition [stored == nullptr || *stored != 42] = %s", B(value_bad));
    if (value_bad) {
      DBG("BRANCH TAKEN: value mismatch");
      printf("FAIL: Value mismatch\n");
      ok = false;
      DBG("ok set to false");
    } else {
      DBG("BRANCH TAKEN: value is 42 (expected)");
    }
    DBG("Calling Release(found=%p)", (void *)found);
    block_cache->Release(found);
    printf("  Released lookup handle\n");
    DBG("Release(lookup handle) complete");
  }

  printf("Step 6: Erase the entry and verify it is gone\n");
  DBG("Entering Step 6");
  size_t deleted_before_erase = g_deleted_count;
  DBG("deleted_before_erase = %zu", deleted_before_erase);
  DBG("Calling block_cache->Erase(key='%s')", key_str.c_str());
  block_cache->Erase(key);
  DBG("Erase returned");
  DBG("Calling block_cache->Lookup(key='%s') to verify removal", key_str.c_str());
  leveldb::Cache::Handle *after_erase = block_cache->Lookup(key);
  DBG("Lookup after erase returned handle = %p", (void *)after_erase);
  bool still_present = (after_erase != nullptr);
  DBG("Condition [after_erase != nullptr] = %s", B(still_present));
  if (still_present) {
    DBG("BRANCH TAKEN: entry still present after erase");
    printf("FAIL: Entry still present after Erase\n");
    DBG("Calling Release(after_erase=%p)", (void *)after_erase);
    block_cache->Release(after_erase);
    DBG("Release(after_erase) complete");
    ok = false;
    DBG("ok set to false");
  } else {
    DBG("BRANCH TAKEN: entry absent after erase (expected)");
    printf("  Entry successfully erased\n");
  }
  DBG("Calling g_deleted_count check: g_deleted_count=%zu, expected=%zu",
      g_deleted_count, deleted_before_erase + 1);
  bool deleter_not_called = (g_deleted_count != deleted_before_erase + 1);
  DBG("Condition [g_deleted_count != deleted_before_erase + 1] = %s", B(deleter_not_called));
  if (deleter_not_called) {
    DBG("BRANCH TAKEN: deleter not invoked exactly once on Erase");
    printf("FAIL: deleter not invoked on Erase\n");
    ok = false;
    DBG("ok set to false");
  } else {
    DBG("BRANCH TAKEN: deleter invoked exactly once (expected)");
  }
  DBG("Calling TotalCharge() after erase");
  size_t charge_after_erase = block_cache->TotalCharge();
  DBG("TotalCharge() returned %zu", charge_after_erase);
  bool erase_charge_bad = (charge_after_erase != 0);
  DBG("Condition [TotalCharge() != 0] = %s", B(erase_charge_bad));
  if (erase_charge_bad) {
    DBG("BRANCH TAKEN: charge after erase nonzero");
    printf("FAIL: TotalCharge after erase = %zu, expected 0\n", charge_after_erase);
    ok = false;
    DBG("ok set to false");
  } else {
    DBG("BRANCH TAKEN: charge after erase is 0 (expected)");
  }

  printf("Step 7: Verify capacity is enforced by evicting least recently used entries\n");
  DBG("Entering Step 7");
  const size_t kEntryCharge = 1024;
  const size_t kNumEntries = (capacity / kEntryCharge) * 2;  // total inserted charge = 2x capacity
  DBG("kNumEntries=%zu, kEntryCharge=%zu, total inserted charge=%zu, capacity=%zu",
      kNumEntries, kEntryCharge, kNumEntries * kEntryCharge, capacity);
  DBG("NOTE: capacity is %zu bytes (cache_size=%zu MiB). The comment assumes 1 MiB.",
      capacity, opt.cache_size);
  std::vector<int> values(kNumEntries);
  DBG("Allocated values vector with %zu elements", values.size());
  size_t deleted_before_fill = g_deleted_count;
  DBG("deleted_before_fill = %zu", deleted_before_fill);
  for (size_t i = 0; i < kNumEntries; i++) {
    DBG("Loop iteration i=%zu / %zu", i, kNumEntries);
    values[i] = static_cast<int>(i);
    std::string k = "evict_" + std::to_string(i);
    leveldb::Slice s(k);
    DBG("Built key '%s' (size=%zu)", k.c_str(), s.size());
    DBG("Calling Insert(key='%s', value=%d, charge=%zu, NoopDeleter)", k.c_str(), values[i], kEntryCharge);
    leveldb::Cache::Handle *h = block_cache->Insert(s, &values[i], kEntryCharge, &NoopDeleter);
    DBG("Insert returned handle = %p", (void *)h);
    bool insert_null = (h == nullptr);
    DBG("Condition [h == nullptr] = %s", B(insert_null));
    if (insert_null) {
      DBG("BRANCH TAKEN: Insert returned null at index %zu", i);
      printf("FAIL: Insert returned null handle at index %zu\n", i);
      ok = false;
      DBG("ok set to false; breaking out of loop");
      break;
    }
    DBG("BRANCH TAKEN: Insert returned valid handle at index %zu", i);
    DBG("Calling Release(h=%p)", (void *)h);
    block_cache->Release(h);
    DBG("Release complete for index %zu", i);
    // Periodic checkpoint so long runs remain readable.
    if (i % 256 == 0 || i == kNumEntries - 1) {
      DBG("Checkpoint at i=%zu: TotalCharge() = %zu, g_deleted_count = %zu",
          i, block_cache->TotalCharge(), g_deleted_count);
    }
  }
  DBG("Fill loop finished");
  printf("  Inserted %zu entries, total charge = %zu, capacity = %zu\n",
         kNumEntries, block_cache->TotalCharge(), capacity);
  DBG("Calling TotalCharge() after fill");
  size_t charge_after_fill = block_cache->TotalCharge();
  DBG("TotalCharge() returned %zu", charge_after_fill);
  bool over_capacity = (charge_after_fill > capacity);
  DBG("Condition [TotalCharge() > capacity] = %s (%zu > %zu)",
      B(over_capacity), charge_after_fill, capacity);
  if (over_capacity) {
    DBG("BRANCH TAKEN: total charge exceeds capacity");
    printf("FAIL: TotalCharge %zu exceeds capacity %zu\n", charge_after_fill, capacity);
    ok = false;
    DBG("ok set to false");
  } else {
    DBG("BRANCH TAKEN: total charge within capacity");
  }
  bool no_eviction = (g_deleted_count <= deleted_before_fill);
  DBG("Condition [g_deleted_count <= deleted_before_fill] = %s (%zu <= %zu)",
      B(no_eviction), g_deleted_count, deleted_before_fill);
  if (no_eviction) {
    DBG("BRANCH TAKEN: no eviction occurred");
    printf("FAIL: no eviction occurred although inserted charge exceeds capacity\n");
    ok = false;
    DBG("ok set to false");
  } else {
    DBG("BRANCH TAKEN: evictions occurred, deleted %zu entries",
        g_deleted_count - deleted_before_fill);
  }

  std::string last_key = "evict_" + std::to_string(kNumEntries - 1);
  DBG("Built last_key '%s'", last_key.c_str());
  leveldb::Slice last_slice(last_key);
  DBG("Calling Lookup(last_key='%s')", last_key.c_str());
  leveldb::Cache::Handle *last = block_cache->Lookup(last_slice);
  DBG("Lookup returned handle = %p", (void *)last);
  bool last_null = (last == nullptr);
  DBG("Condition [last == nullptr] = %s", B(last_null));
  if (last_null) {
    DBG("BRANCH TAKEN: most recent entry missing");
    printf("FAIL: most recently inserted entry was evicted\n");
    ok = false;
    DBG("ok set to false");
  } else {
    DBG("BRANCH TAKEN: most recent entry found");
    DBG("Calling Value(last=%p)", (void *)last);
    int *stored = static_cast<int *>(block_cache->Value(last));
    DBG("Value returned pointer %p", (void *)stored);
    if (stored) {
      DBG("Stored value dereferenced = %d (expected %d)", *stored, static_cast<int>(kNumEntries - 1));
    }
    bool last_value_bad = (stored == nullptr || *stored != static_cast<int>(kNumEntries - 1));
    DBG("Condition [stored == nullptr || *stored != %d] = %s",
        static_cast<int>(kNumEntries - 1), B(last_value_bad));
    if (last_value_bad) {
      DBG("BRANCH TAKEN: most recent entry has wrong value");
      printf("FAIL: most recent entry has wrong value\n");
      ok = false;
      DBG("ok set to false");
    } else {
      DBG("BRANCH TAKEN: most recent entry value correct");
    }
    DBG("Calling Release(last=%p)", (void *)last);
    block_cache->Release(last);
    DBG("Release(last) complete");
  }

  printf("Step 8: Cleanup\n");
  DBG("Entering Step 8");
  DBG("Deleting block_cache (%p)", (void *)block_cache);
  delete block_cache;
  printf("  Deleted block_cache\n");
  DBG("delete block_cache complete; g_deleted_count = %zu", g_deleted_count);
  DBG("Deleting bloom_filter_policy (%p)", (const void *)bloom_filter_policy);
  delete bloom_filter_policy;
  printf("  Deleted bloom_filter_policy\n");
  DBG("delete bloom_filter_policy complete");

  int result = ok ? 0 : -1;
  DBG("Final ok = %s, result = %d", B(ok), result);
  printf("Test_open result = %d\n", result);
  DBG("==== Test_open END ====");
  return result;
}

int main() {
  DBG("==== main START ====");
  int rc = Test_open();
  DBG("Test_open returned %d", rc);
  DBG("==== main END ====");
  return rc;
}