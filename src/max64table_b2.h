#ifndef CUCKOO_FILTER_MAX64_TABLE_B2_H_
#define CUCKOO_FILTER_MAX64_TABLE_B2_H_


#include <assert.h>

#include <sstream>
#include <cstdint>

#include "bitsutil.h"
#include "debug.h"
#include "printutil.h"

namespace cuckoofilter {

// the most naive table implementation: one huge bit array
// template <size_t bits_per_tag>
class Max64Table {
  static const size_t kTagsPerBucket = 2;
  static const size_t kBytesPerBucket = 16;
      // (bits_per_tag * kTagsPerBucket + 7) >> 3;
  size_t bits_per_tag;
  size_t kTagMask;
  uint64_t kEmptyCell;
  // NOTE: accomodate extra buckets if necessary to avoid overrun
  // as we always read a uint64
  // static const size_t kPaddingBuckets =
  //   ((((kBytesPerBucket + 7) / 8) * 8) - 1) / kBytesPerBucket;

  struct Bucket {
    char bits_[kBytesPerBucket];
  } __attribute__((__packed__));

  // using a pointer adds one more indirection
  Bucket *buckets_;
  size_t num_buckets_;

 public:
  explicit Max64Table(const size_t bpt,const size_t num) : bits_per_tag(bpt), num_buckets_(num) {
    kTagMask = (1ULL << bits_per_tag) - 1;
    kEmptyCell = 1ULL << bits_per_tag;  
    buckets_ = new Bucket[num_buckets_];
    // memset(buckets_, 0, kBytesPerBucket * num_buckets_);
    for (size_t i = 0; i < num_buckets_; ++i) {
      for (size_t j = 0; j < kTagsPerBucket; ++j) {
        ((size_t *)(buckets_[i].bits_))[j] = kEmptyCell;
      }
    }
  }

  ~Max64Table() { 
    delete[] buckets_;
  }

  size_t NumBuckets() const {
    return num_buckets_;
  }

  size_t SizeInBytes() const { 
    return bits_per_tag * kTagsPerBucket  * num_buckets_ >> 3; 
  }

  size_t SizeInTags() const { 
    return kTagsPerBucket * num_buckets_; 
  }

  std::string Info() const {
    std::stringstream ss;
    ss << "SingleHashtable with tag size: " << bits_per_tag << " bits \n";
    ss << "\t\tAssociativity: " << kTagsPerBucket << "\n";
    ss << "\t\tTotal # of rows: " << num_buckets_ << "\n";
    ss << "\t\tTotal # slots: " << SizeInTags() << "\n";
    return ss.str();
  }

  // read tag from pos(i,j)
  inline size_t ReadTag(const size_t i, const size_t j) const {
    const char *p = buckets_[i].bits_;
    size_t tag;
    tag = ((size_t *)p)[j];
    return tag & kTagMask;
  }

  // read 64 from pos(i,j)
  inline size_t Read64(const size_t i, const size_t j) const {
    const char *p = buckets_[i].bits_;
    size_t tag;
    tag = ((size_t *)p)[j];
    return tag;
  }

  // write tag to pos(i,j)
  inline void WriteTag(const size_t i, const size_t j, const size_t t) {
    char *p = buckets_[i].bits_;
    size_t tag = t & kTagMask;
    ((size_t *)p)[j] = tag;
  }

  // inline bool FindTagInBuckets(const size_t i1, const size_t i2,
  //                              const uint32_t tag) const {
  //   const char *p1 = buckets_[i1].bits_;
  //   const char *p2 = buckets_[i2].bits_;

  //   uint64_t v1 = *((uint64_t *)p1);
  //   uint64_t v2 = *((uint64_t *)p2);
  //   for (size_t j = 0; j < kTagsPerBucket; j++) {
  //     if ((ReadTag(i1, j) == tag) || (ReadTag(i2, j) == tag)) {
  //       return true;
  //   }
  //     return false;
  //   }
  // }

  inline bool FindTagInBuckets_PCF(const size_t i1, const size_t i2,
                               const size_t tag) const {
    const char *p1 = buckets_[i1].bits_;
    const char *p2 = buckets_[i2].bits_;
    size_t tag2 = tag ^ (1ULL << (bits_per_tag-1));
    // std::cout << "tag: " << tag << std::endl;
    // std::cout << "i1: " << i1 << std::endl;
    for (size_t j = 0; j < kTagsPerBucket; j++) {
      if ((ReadTag(i1, j) == tag && Read64(i1, j) != kEmptyCell ) || (ReadTag(i2, j) == tag2 && Read64(i2, j) != kEmptyCell)) {
        return true;
      }
    }
    return false;
  }

  // inline bool FindTagInBucket(const size_t i, const size_t tag) const {
  //   // caution: unaligned access & assuming little endian
  //   for (size_t j = 0; j < kTagsPerBucket; j++) {
  //     if (ReadTag(i, j) == tag) {
  //       return true;
  //     }
  //   }
  //   return false;
  // }

  // inline bool DeleteTagFromBucket(const size_t i, const size_t tag) {
  //   for (size_t j = 0; j < kTagsPerBucket; j++) {
  //     if (ReadTag(i, j) == tag) {
  //       assert(FindTagInBucket(i, tag) == true);
  //       WriteTag(i, j, 0);
  //       return true;
  //     }
  //   }
  //   return false;
  // }

  inline bool InsertTagToBucket(const size_t i, const size_t tag,
                                const bool kickout, size_t &oldtag) {
    for (size_t j = 0; j < kTagsPerBucket; j++) {
      if (Read64(i, j) == kEmptyCell) {
        // std::cout << "insert tag: " << tag << std::endl;
        // std::cout << "insert to: " << i << std::endl;
        // std::cout << "insert to: " << j << std::endl;
        WriteTag(i, j, tag);

        return true;
      }
    }
    
    if (kickout) {
      size_t r = rand() % kTagsPerBucket;
      oldtag = ReadTag(i, r);
      WriteTag(i, r, tag);
    }
    return false;
  }

  inline size_t NumTagsInBucket(const size_t i) const {
    size_t num = 0;
    for (size_t j = 0; j < kTagsPerBucket; j++) {
      if (Read64(i, j) != kEmptyCell) {
        num++;
      }
    }
    return num;
  }
};
}  // namespace cuckoofilter
#endif  // CUCKOO_FILTER_MAX64_TABLE_H_
