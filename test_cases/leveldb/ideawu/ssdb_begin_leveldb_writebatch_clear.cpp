#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <string>

#include "leveldb/cache.h"
#include "leveldb/comparator.h"
#include "leveldb/db.h"
#include "leveldb/env.h"
#include "leveldb/filter_policy.h"
#include "leveldb/iterator.h"
#include "leveldb/options.h"
#include "leveldb/slice.h"
#include "leveldb/status.h"
#include "leveldb/table.h"
#include "leveldb/table_builder.h"
#include "leveldb/write_batch.h"

// Debug helper: prints a tagged message and flushes so output is visible
// even if the process crashes or aborts shortly afterwards.
#define DBG(...)                       \
  do {                                 \
    printf("[DEBUG] " __VA_ARGS__);    \
    printf("\n");                      \
    fflush(stdout);                    \
  } while (0)

static size_t batch_size_of_empty(const leveldb::WriteBatch& b) {
  DBG("batch_size_of_empty: calling ApproximateSize() on reference batch");
  size_t sz = b.ApproximateSize();
  DBG("batch_size_of_empty: ApproximateSize() returned %zu", sz);
  return sz;
}

static leveldb::Status CopyState(const leveldb::Status& in) {
  DBG("CopyState: input status ok=%d, ToString=\"%s\"",
      in.ok() ? 1 : 0, in.ToString().c_str());
  leveldb::Status copy(in);
  DBG("CopyState: copy constructed, ok=%d", copy.ok() ? 1 : 0);
  return copy;
}

int Test_begin() {
  DBG("Test_begin: START");

  // Clear
  // Documentation: "Clear all updates buffered in this batch."
  // Expected: Count() == 0 and the serialized representation is back to the
  // empty-batch size, so ApproximateSize() matches a freshly constructed batch.
  DBG("Section [Clear]: constructing reference empty WriteBatch");
  leveldb::WriteBatch empty_ref;
  DBG("Section [Clear]: reference WriteBatch constructed at %p", (void*)&empty_ref);
  const size_t empty_size = batch_size_of_empty(empty_ref);
  DBG("Section [Clear]: empty_size=%zu", empty_size);
  (void)empty_size;

  DBG("Section [Clear]: constructing test WriteBatch 'batch'");
  leveldb::WriteBatch batch;
  DBG("Section [Clear]: test batch constructed at %p", (void*)&batch);
  DBG("Section [Clear]: batch.ApproximateSize()=%zu, empty_ref.ApproximateSize()=%zu",
      batch.ApproximateSize(), empty_ref.ApproximateSize());
  if (batch.ApproximateSize() != empty_ref.ApproximateSize()) {
    DBG("Conditional: fresh batch size != reference size -> TRUE, failing");
    printf("FAIL: fresh WriteBatch ApproximateSize() differs from reference empty batch\n");
    return -1;
  }
  DBG("Conditional: fresh batch size == reference size -> FALSE, continuing");

  DBG("Section [Clear]: calling batch.Put(\"key\", \"value\")");
  batch.Put("key", "value");
  DBG("Section [Clear]: Put(\"key\") returned");
  DBG("Section [Clear]: calling batch.Put(\"key2\", \"value2\")");
  batch.Put("key2", "value2");
  DBG("Section [Clear]: Put(\"key2\") returned");
  printf("WriteBatch::Put x2 done, ApproximateSize()=%zu\n",
         batch.ApproximateSize());
  DBG("Section [Clear]: batch.ApproximateSize()=%zu, empty_ref.ApproximateSize()=%zu",
      batch.ApproximateSize(), empty_ref.ApproximateSize());
  if (batch.ApproximateSize() <= empty_ref.ApproximateSize()) {
    DBG("Conditional: size did not grow after Put -> TRUE, failing");
    printf("FAIL: expected ApproximateSize() to grow after Put()\n");
    return -1;
  }
  DBG("Conditional: size grew after Put -> FALSE, continuing");

  DBG("Section [Clear]: calling batch.Clear() (first time)");
  batch.Clear();
  DBG("Section [Clear]: batch.Clear() returned");
  printf("WriteBatch::Clear() called, ApproximateSize()=%zu\n",
         batch.ApproximateSize());
  DBG("Section [Clear]: batch.ApproximateSize()=%zu, empty_ref=%zu",
      batch.ApproximateSize(), empty_ref.ApproximateSize());
  if (batch.ApproximateSize() != empty_ref.ApproximateSize()) {
    DBG("Conditional: size after Clear != empty size -> TRUE, failing");
    printf("FAIL: expected ApproximateSize() to equal empty batch size after Clear()\n");
    return -1;
  }
  DBG("Conditional: size after Clear == empty size -> FALSE, continuing");

  // Verify the batch is still usable after Clear()
  DBG("Section [Clear]: calling batch.Put(\"after\", \"clear\") after Clear()");
  batch.Put("after", "clear");
  DBG("Section [Clear]: Put(\"after\") returned");
  printf("WriteBatch::Put after Clear() done\n");

  DBG("Section [Clear]: calling batch.Clear() (second time)");
  batch.Clear();
  DBG("Section [Clear]: second batch.Clear() returned");
  printf("WriteBatch::Clear() called again, ApproximateSize()=%zu\n",
         batch.ApproximateSize());
  DBG("Section [Clear]: batch.ApproximateSize()=%zu, empty_ref=%zu",
      batch.ApproximateSize(), empty_ref.ApproximateSize());
  if (batch.ApproximateSize() != empty_ref.ApproximateSize()) {
    DBG("Conditional: size after second Clear != empty size -> TRUE, failing");
    printf("FAIL: expected ApproximateSize() to equal empty batch size after second Clear()\n");
    return -1;
  }
  DBG("Conditional: size after second Clear == empty size -> FALSE, continuing");

  // Clear on an already-empty batch must be a no-op and remain safe.
  DBG("Section [Clear]: calling batch.Clear() on already-empty batch");
  batch.Clear();
  DBG("Section [Clear]: Clear() on empty batch returned");
  printf("WriteBatch::Clear() on empty batch, ApproximateSize()=%zu\n", batch.ApproximateSize());
  DBG("Section [Clear]: batch.ApproximateSize()=%zu, empty_ref=%zu",
      batch.ApproximateSize(), empty_ref.ApproximateSize());
  if (batch.ApproximateSize() != empty_ref.ApproximateSize()) {
    DBG("Conditional: Clear() on empty batch changed size -> TRUE, failing");
    printf("FAIL: Clear() on empty batch changed state\n");
    return -1;
  }
  DBG("Conditional: Clear() on empty batch was a no-op -> FALSE, continuing");

  // ~FilterPolicy
  DBG("Section [~FilterPolicy]: calling leveldb::NewBloomFilterPolicy(10)");
  const leveldb::FilterPolicy* filter = leveldb::NewBloomFilterPolicy(10);
  DBG("Section [~FilterPolicy]: NewBloomFilterPolicy returned %p", (const void*)filter);
  printf("NewBloomFilterPolicy(10) returned %p\n", (const void*)filter);
  DBG("Section [~FilterPolicy]: calling delete on filter policy");
  delete filter;
  DBG("Section [~FilterPolicy]: delete filter returned");
  printf("~FilterPolicy done\n");

  // ~Comparator: TODO commented out, requires subclassing Comparator (missing typeinfo at link time)
  DBG("Section [~Comparator]: skipped (TODO, requires subclassing Comparator)");

  // CopyState
  DBG("Section [CopyState #1]: calling CopyState(Status::OK())");
  leveldb::Status s = CopyState(leveldb::Status::OK());
  DBG("Section [CopyState #1]: CopyState returned, s.ok()=%d", s.ok() ? 1 : 0);
  printf("CopyState done, ok=%d\n", s.ok() ? 1 : 0);

  // WriteBatch
  DBG("Section [WriteBatch heap]: calling new leveldb::WriteBatch()");
  leveldb::WriteBatch* batch2 = new leveldb::WriteBatch();
  DBG("Section [WriteBatch heap]: new returned %p", (void*)batch2);
  printf("WriteBatch (heap) created at %p\n", (void*)batch2);

  // CopyState
  DBG("Section [CopyState #2]: calling CopyState(s) with s.ok()=%d", s.ok() ? 1 : 0);
  s = CopyState(s);
  DBG("Section [CopyState #2]: CopyState returned, s.ok()=%d", s.ok() ? 1 : 0);
  printf("CopyState done, ok=%d\n", s.ok() ? 1 : 0);

  // ~WriteBatch
  DBG("Section [~WriteBatch]: calling delete batch2 (%p)", (void*)batch2);
  delete batch2;
  DBG("Section [~WriteBatch]: delete batch2 returned");
  printf("~WriteBatch done\n");

  // CopyState
  DBG("Section [CopyState #3]: calling CopyState(s) with s.ok()=%d", s.ok() ? 1 : 0);
  s = CopyState(s);
  DBG("Section [CopyState #3]: CopyState returned, s.ok()=%d", s.ok() ? 1 : 0);
  printf("CopyState done, ok=%d\n", s.ok() ? 1 : 0);

  // Setup for TableBuilder
  DBG("Section [TableBuilder setup]: calling leveldb::Env::Default()");
  leveldb::Env* env = leveldb::Env::Default();
  DBG("Section [TableBuilder setup]: Env::Default() returned %p", (void*)env);
  printf("Env::Default() returned %p\n", (void*)env);
  leveldb::WritableFile* file = nullptr;
  DBG("Section [TableBuilder setup]: calling env->NewWritableFile(\"/tmp/test_table_clear.ldb\")");
  s = env->NewWritableFile("/tmp/test_table_clear.ldb", &file);
  DBG("Section [TableBuilder setup]: NewWritableFile returned, ok=%d, status=\"%s\", file=%p",
      s.ok() ? 1 : 0, s.ToString().c_str(), (void*)file);
  printf("NewWritableFile status ok=%d\n", s.ok() ? 1 : 0);
  if (!s.ok()) {
    DBG("Conditional: NewWritableFile failed -> TRUE, returning -1");
    return -1;
  }
  DBG("Conditional: NewWritableFile succeeded -> FALSE, continuing");

  DBG("Section [TableBuilder setup]: constructing leveldb::Options");
  leveldb::Options options;
  DBG("Section [TableBuilder setup]: Options constructed at %p", (void*)&options);
  DBG("Section [TableBuilder setup]: calling new leveldb::TableBuilder(options, file=%p)",
      (void*)file);
  leveldb::TableBuilder* tb = new leveldb::TableBuilder(options, file);
  DBG("Section [TableBuilder setup]: TableBuilder constructor returned %p", (void*)tb);
  printf("TableBuilder created at %p\n", (void*)tb);

  // Abandon
  DBG("Section [Abandon]: calling tb->Abandon()");
  tb->Abandon();
  DBG("Section [Abandon]: tb->Abandon() returned");
  printf("TableBuilder::Abandon() done\n");

  // Finish
  // Finish() after Abandon() on the same builder asserts, so use a fresh builder.
  leveldb::WritableFile* file2 = nullptr;
  s = env->NewWritableFile("/tmp/test_table_clear2.ldb", &file2);
  if (!s.ok()) {
    return -1;
  }
  leveldb::TableBuilder* tb2 = new leveldb::TableBuilder(options, file2);
  DBG("Section [Finish]: calling tb2->Finish() on a fresh builder");
  s = tb2->Finish();
  delete tb2;
  if (file2 != nullptr) {
    file2->Close();
  }
  delete file2;
  DBG("Section [Finish]: tb->Finish() returned, ok=%d, status=\"%s\"",
      s.ok() ? 1 : 0, s.ToString().c_str());
  printf("TableBuilder::Finish() returned ok=%d\n", s.ok() ? 1 : 0);

  // CopyState
  DBG("Section [CopyState #4]: calling CopyState(s) with s.ok()=%d", s.ok() ? 1 : 0);
  s = CopyState(s);
  DBG("Section [CopyState #4]: CopyState returned, s.ok()=%d", s.ok() ? 1 : 0);
  printf("CopyState done, ok=%d\n", s.ok() ? 1 : 0);

  // FileSize
  DBG("Section [FileSize]: calling tb->FileSize()");
  uint64_t size = tb->FileSize();
  DBG("Section [FileSize]: tb->FileSize() returned %llu", (unsigned long long)size);
  printf("TableBuilder::FileSize() returned %llu\n", (unsigned long long)size);
  (void)size;

  // ~TableBuilder
  DBG("Section [~TableBuilder]: calling delete tb (%p)", (void*)tb);
  delete tb;
  DBG("Section [~TableBuilder]: delete tb returned");
  printf("~TableBuilder done\n");

  // CopyState
  DBG("Section [CopyState #5]: calling CopyState(s) with s.ok()=%d", s.ok() ? 1 : 0);
  s = CopyState(s);
  DBG("Section [CopyState #5]: CopyState returned, s.ok()=%d", s.ok() ? 1 : 0);
  printf("CopyState done, ok=%d\n", s.ok() ? 1 : 0);

  DBG("Section [WritableFile cleanup]: calling delete file (%p)", (void*)file);
  delete file;
  DBG("Section [WritableFile cleanup]: delete file returned");
  printf("WritableFile deleted\n");

  DBG("Test_begin: END, returning 0");
  return 0;
}

int main() {
  DBG("main: START, calling Test_begin()");
  int rc = Test_begin();
  DBG("main: Test_begin() returned %d", rc);
  DBG("main: END");
  return rc;
}