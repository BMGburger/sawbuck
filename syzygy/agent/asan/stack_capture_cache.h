// Copyright 2012 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SYZYGY_AGENT_ASAN_STACK_CAPTURE_CACHE_H_
#define SYZYGY_AGENT_ASAN_STACK_CAPTURE_CACHE_H_

#include "base/hash_tables.h"
#include "base/synchronization/lock.h"
#include "syzygy/agent/asan/stack_capture.h"

namespace agent {
namespace asan {

// Forward declaration.
class AsanLogger;
class StackCapture;

// A class which manages a thread-safe cache of unique stack traces, by ID.
class StackCaptureCache {
 public:
  // The size of a page of stack captures, in bytes. This should be in the
  // hundreds of KB or low MBs so that we have an efficient pooled allocator
  // that can store hundreds to thousands of stack captures, yet whose
  // incremental growth is not too large.
  static const size_t kCachePageSize = 1024 * 1024;

  // The type used to uniquely identify a stack.
  typedef StackCapture::StackId StackId;

  // Forward declaration.
  class CachePage;

  // TODO(chrisha): Plumb a command-line parameter through to control the
  //     max depth of stack traces in the StackCaptureCache. This should get us
  //     significant memory savings in the stack trace cache.

  // Initializes a new stack capture cache.
  // @param logger The logger to use.
  // @param max_num_frames The maximum number of frames to be used by the
  //     StackCapture objects in this cache.
  explicit StackCaptureCache(AsanLogger* logger);
  StackCaptureCache(AsanLogger* logger, size_t max_num_frames);

  // Destroys a stack capture cache.
  ~StackCaptureCache();

  // Static initialisation of StackCaptureCache context.
  static void Init();

  // @returns the current maximum number of frames supported by saved stack
  //     traces.
  size_t max_num_frames() const { return max_num_frames_; }

  // Sets the current maximum number of frames supported by saved stack traces.
  // @param max_num_frames The maximum number of frames to set.
  void set_max_num_frames(size_t max_num_frames) {
    max_num_frames_ = max_num_frames;
  }

  // @returns the default compression reporting period value.
  static size_t GetDefaultCompressionReportingPeriod() {
    return kDefaultCompressionReportingPeriod;
  }

  // Sets a new (global) compression reporting period value. Note that this
  // method is not thread safe. It is expected to be called once at startup,
  // or not at all.
  static void set_compression_reporting_period(size_t period) {
    compression_reporting_period_ = period;
  }

  // @returns the current (global) compression reporting period value. It is
  //     expected that this value is a constant after initialization.
  static size_t compression_reporting_period() {
    return compression_reporting_period_;
  }

  // Save (or retrieve) the stack capture (the first @p num_frames elements
  // from  @p frames) into the cache using @p stack_id as the key.
  // @param stack_id a unique identifier for this stack trace. It is expected
  //     that identical stack traces will have the same @p stack_id.
  // @param frames an array of stack frame pointers.
  // @param num_frames the number of valid elements in @p frames. Note that
  //     at most StackCapture::kMaxNumFrames will be saved.
  // @param stack_capture The initialized stack capture to save.
  // @returns a pointer to the saved stack capture.
  const StackCapture* SaveStackTrace(StackId stack_id,
                                     const void* const* frames,
                                     size_t num_frames);
  const StackCapture* SaveStackTrace(const StackCapture& stack_capture);

  // Releases a previously referenced stack trace. This decrements the reference
  // count and potentially cleans up the stack trace.
  // @param stack_capture The stack capture to be released.
  void ReleaseStackTrace(const StackCapture* stack_capture);

  // Logs the current stack capture cache statistics. This method is thread
  // safe.
  void LogStatistics() const;

 protected:
  // The container type in which we store the cached stacks. This enforces
  // uniqueness based on their hash value, nothing more.
  typedef base::hash_set<StackCapture*,
                         StackCapture::HashCompare> StackSet;

  // Used for shuttling around statistics about this cache.
  struct Statistics {
    // The total number of stacks currently in the cache. This isn't actually
    // updated in realtime, as this is the same as known_stacks_.size(). It is
    // populated when we take a snapshot of the statistics with GetStatistics.
    size_t cached;
    // The current total size of the stack cache, in bytes.
    size_t size;
    // The total number of reference-saturated stack captures. These will never
    // be able to be removed from the cache.
    size_t saturated;
    // The number of currently unreferenced stack captures. These are pending
    // cleanup.
    size_t unreferenced;

    // We use 64-bit integers for the following because they can overflow a
    // 32-bit value for long running processes.

    // The total number of stacks requested over the lifetime of the stack
    // cache.
    uint64 requested;
    // The total number of stacks that have had to be allocated. This is not
    // necessarily the same as |cached| as the stack cache can reclaim
    // unreferenced stacks.
    uint64 allocated;
    // The total number of active references to stack captures.
    uint64 references;
  };

  // Gets the current cache statistics. This must be called under lock_.
  // @param statistics Will be populated with current cache statistics.
  void GetStatisticsUnlocked(Statistics* statistics) const;

  // Implementation function for logging statistics.
  // @param report The statistics to be reported.
  void LogStatisticsImpl(const Statistics& statistics) const;

  // The default number of iterations between each compression ratio report.
  // Zero (0) means do not report.
  static const size_t kDefaultCompressionReportingPeriod = 0;

  // The number of allocations between reports of the stack trace cache
  // compression ratio. Zero (0) means do not report. Values like 1 million
  // seem to be pretty good with Chrome.
  static size_t compression_reporting_period_;

  // Logger instance to which to report the compression ratio.
  AsanLogger* const logger_;

  // A lock to protect the known stacks set from concurrent access.
  mutable base::Lock lock_;

  // The max depth of the stack traces to allocate. This can change, but it
  // doesn't really make sense to do so.
  size_t max_num_frames_;

  // The set of known stacks. Accessed under lock_.
  StackSet known_stacks_;

  // The current page from which new stack captures are allocated.
  // Accessed under lock_.
  CachePage* current_page_;

  // Aggregate statistics about the cache. Accessed under lock_.
  Statistics statistics_;

 private:
  DISALLOW_COPY_AND_ASSIGN(StackCaptureCache);
};

// A page of preallocated stack trace capture objects to be populated
// and stored in the known stacks cache set.
class StackCaptureCache::CachePage {
 public:
  explicit CachePage(CachePage* link) : next_page_(link), bytes_used_(0) {
  }

  ~CachePage();

  // Allocates a stack capture from this cache page if possible.
  // @param max_num_frames The maximum number of frames the object needs to be
  //     able to store.
  // @returns a new StackCapture, or NULL if the page is full.
  StackCapture* GetNextStackCapture(size_t max_num_frames);

  // Releases the most recently allocated stack capture back to the page.
  // @param stack_capture The stack capture to return. This must be the most
  //     recently allocated capture as returned by GetNextStackCapture.
  void ReleaseStackCapture(StackCapture* stack_capture);

  // @returns the number of bytes used in this page. This is mainly a hook
  //     for unittesting.
  size_t bytes_used() const { return bytes_used_; }

 protected:
  // The cache pages from a linked list, which allows for easy cleanup
  // when the cache is destroyed.
  CachePage* next_page_;

  // The number of bytes used, also equal to the byte offset of the next
  // StackCapture object to be allocated.
  size_t bytes_used_;

  // A page's worth of data, which will be allocated as StackCapture objects.
  // NOTE: Using offsetof would be ideal, but we can't do that on an incomplete
  //       type. Thus, this needs to be maintained.
  static const size_t kDataSize = kCachePageSize - sizeof(CachePage*)
      - sizeof(size_t);
  COMPILE_ASSERT(kDataSize < kCachePageSize,
                 kCachePageSize_must_be_big_enough_for_CachePage_header);
  uint8 data_[kDataSize];

 private:
  DISALLOW_COPY_AND_ASSIGN(CachePage);
};
COMPILE_ASSERT(sizeof(StackCaptureCache::CachePage) ==
                   StackCaptureCache::kCachePageSize,
               kDataSize_calculation_needs_to_be_updated);
COMPILE_ASSERT(StackCaptureCache::kCachePageSize % 4096 == 0,
               kCachePageSize_should_be_a_multiple_of_the_page_size);

}  // namespace asan
}  // namespace agent

#endif  // SYZYGY_AGENT_ASAN_STACK_CAPTURE_CACHE_H_