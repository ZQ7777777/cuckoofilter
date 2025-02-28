#ifndef CUCKOO_FILTER_CUCKOO_FILTER_H_
#define CUCKOO_FILTER_CUCKOO_FILTER_H_
#pragma once
#include <assert.h>
#include <algorithm>

#include "debug.h"
#include "hashutil.h"
#include "packedtable.h"
#include "printutil.h"
#include "singletable.h"
#include "max64table.h"
#include "MurmurHash3.h"
namespace cuckoofilter {
// status returned by a cuckoo filter operation
enum Status {
  Ok = 0,
  NotFound = 1,
  NotEnoughSpace = 2,
  NotSupported = 3,
};

// maximum number of cuckoo kicks before claiming failure
const size_t kMaxCuckooCount = 500;

// A cuckoo filter class exposes a Bloomier filter interface,
// providing methods of Add, Delete, Contain. It takes three
// template parameters:
//   ItemType:  the type of item you want to insert
//   bits_per_item: how many bits each item is hashed into
//   TableType: the storage of table, SingleTable by default, and
// PackedTable to enable semi-sorting
template <typename ItemType, size_t bits_per_item,
          template <size_t> class TableType = Max64Table,
          // typename HashFamily = SimpleTabulation
          typename HashFamily = TwoIndependentMultiplyShift
          >
class CuckooFilter {
protected:
  // Storage of items
  // TableType<bits_per_item> *table_;
    Max64Table<4> *table_;

  // Number of items stored
  size_t num_items_;

  typedef struct {
    size_t index;
    uint32_t tag;
    bool used;
  } VictimCache;

  VictimCache victim_;

  HashFamily hasher_;

  virtual inline size_t IndexHash(uint32_t hv) const {
    // table_->num_buckets is always a power of two, so modulo can be replaced
    // with
    // bitwise-and:
    return hv & (table_->NumBuckets() - 1);
  }

  virtual inline uint32_t TagHash(uint32_t hv) const {
    uint32_t tag;
    tag = hv & ((1ULL << bits_per_item) - 1);
    // tag += (tag == 0);
    return tag;
  }

  virtual inline void GenerateIndexTagHash(const ItemType& item, size_t* index,
                                   uint32_t* tag) const {
    
    // uint64_t buf[2];
    // MurmurHash3_x64_128(&item, 8, 32776517, buf);
    // *index = IndexHash(buf[0] >> 32);
    // *tag = TagHash(buf[0]);

    const uint64_t hash = hasher_(item);
    *index = IndexHash(hash >> 32);
    *tag = TagHash(hash);
  }

  virtual inline size_t AltIndex(const size_t index, const uint32_t tag) const {
    // NOTE(binfan): originally we use:
    // index ^ HashUtil::BobHash((const void*) (&tag), 4)) & table_->INDEXMASK;
    // now doing a quick-n-dirty way:
    // 0x5bd1e995 is the hash constant from MurmurHash2
    return IndexHash((uint32_t)(index ^ (tag * 0x5bd1e995)));
  }

  virtual Status AddImpl(const size_t i, const uint32_t tag);

  // load factor is the fraction of occupancy
  // double LoadFactor() const { return 1.0 * Size() / table_->SizeInTags(); }

  double BitsPerItem() const { return 8.0 * table_->SizeInBytes() / Size(); }

 public:
  double LoadFactor() const { return 1.0 * Size() / table_->SizeInTags(); }
  CuckooFilter() {};
  explicit CuckooFilter(const size_t max_num_keys) : num_items_(0), victim_(), hasher_() {
    
    size_t assoc = 4;
    size_t num_buckets = upperpower2(std::max<uint64_t>(1, max_num_keys / assoc));
    double frac = (double)max_num_keys / num_buckets / assoc;
    if (frac > 0.96) {
      num_buckets <<= 1;
      std::cout << "----------------------------resiez happening-------------------------" << std::endl;
    }
    victim_.used = false;
  // if (std::is_same<TableType<bits_per_item>, Max64Table<bits_per_item>>::value) {
      table_ = new Max64Table<4>(bits_per_item, num_buckets);
    // } else {
    //   table_ = new TableType<bits_per_item>(num_buckets);
    // }
  }

  virtual ~CuckooFilter() { delete table_; }

  // Add an item to the filter.
  virtual Status Add(const ItemType &item);

  // Report if the item is inserted, with false positive rate.
  virtual Status Contain(const ItemType &item) const;

  // Delete an key from the filter
  virtual Status Delete(const ItemType &item);

  /* methods for providing stats  */
  // summary infomation
  std::string Info() const;

  // number of current inserted items;
  size_t Size() const { return num_items_; }

  // size of the filter in bytes.
  size_t SizeInBytes() const { return table_->SizeInBytes(); }
};

template <typename ItemType, size_t bits_per_item,
          template <size_t> class TableType, typename HashFamily>
Status CuckooFilter<ItemType, bits_per_item, TableType, HashFamily>::Add(
    const ItemType &item) {
  size_t i;
  uint32_t tag;
  if (victim_.used) {
    return NotEnoughSpace;
  }
  GenerateIndexTagHash(item, &i, &tag);
  // std::cout << "ADD" << std::endl;
  // std::cout << "i: " << i << std::endl;
  // std::cout << "tag: " << tag << std::endl;
  return AddImpl(i, tag);
}

template <typename ItemType, size_t bits_per_item,
          template <size_t> class TableType, typename HashFamily>
Status CuckooFilter<ItemType, bits_per_item, TableType, HashFamily>::AddImpl(
    const size_t i, const uint32_t tag) {
  size_t curindex = i;
  uint32_t curtag = tag;
  uint64_t oldtag;
  for (uint32_t count = 0; count < kMaxCuckooCount; count++) {
    bool kickout = count > 0;
    oldtag = 0;
    if (table_->InsertTagToBucket(curindex, curtag, kickout, oldtag)) {
      num_items_++;
      return Ok;
    }
    if (kickout) {
      curtag = oldtag;
    }
    curindex = AltIndex(curindex, curtag);
  }

  victim_.index = curindex;
  victim_.tag = curtag;
  victim_.used = true;
  return Ok;
}

template <typename ItemType, size_t bits_per_item,
          template <size_t> class TableType, typename HashFamily>
Status CuckooFilter<ItemType, bits_per_item, TableType, HashFamily>::Contain(
    const ItemType &key) const {
  bool found = false;
  size_t i1, i2;
  uint32_t tag;

  GenerateIndexTagHash(key, &i1, &tag);
  i2 = AltIndex(i1, tag);
  // std::cout << "CONTAIN" << std::endl;
  // std::cout << "i1: " << i1 << std::endl;
  // std::cout << "tag: " << tag << std::endl;
  // std::cout << "i2: " << i2 << std::endl;
  assert(i1 == AltIndex(i2, tag));
  found = victim_.used && (tag == victim_.tag) &&
          (i1 == victim_.index || i2 == victim_.index);
  // if (std::is_same<TableType<bits_per_item>, Max64Table<bits_per_item>>::value) {
    if (found || table_->FindTagInBuckets(i1, i2, tag)) {
      return Ok;
    } else {
      return NotFound;
    }
  // } else {
  //   if (found || table_->FindTagInBuckets(i1, i2, tag)) {
  //     return Ok;
  //   } else {
  //     return NotFound;
  //   }
  // }
}

template <typename ItemType, size_t bits_per_item,
          template <size_t> class TableType, typename HashFamily>
Status CuckooFilter<ItemType, bits_per_item, TableType, HashFamily>::Delete(
    const ItemType &key) {
  size_t i1, i2;
  uint32_t tag;

  GenerateIndexTagHash(key, &i1, &tag);
  i2 = AltIndex(i1, tag);

  if (table_->DeleteTagFromBucket(i1, tag)) {
    num_items_--;
    goto TryEliminateVictim;
  } else if (table_->DeleteTagFromBucket(i2, tag)) {
    num_items_--;
    goto TryEliminateVictim;
  } else if (victim_.used && tag == victim_.tag &&
             (i1 == victim_.index || i2 == victim_.index)) {
    // num_items_--;
    victim_.used = false;
    return Ok;
  } else {
    return NotFound;
  }
TryEliminateVictim:
  if (victim_.used) {
    victim_.used = false;
    size_t i = victim_.index;
    uint32_t tag = victim_.tag;
    AddImpl(i, tag);
  }
  return Ok;
}

template <typename ItemType, size_t bits_per_item,
          template <size_t> class TableType, typename HashFamily>
std::string CuckooFilter<ItemType, bits_per_item, TableType, HashFamily>::Info() const {
  std::stringstream ss;
  ss << "CuckooFilter Status:\n"
     << "\t\t" << table_->Info() << "\n"
     << "\t\tKeys stored: " << Size() << "\n"
     << "\t\tLoad factor: " << LoadFactor() << "\n"
     << "\t\tHashtable size: " << (table_->SizeInBytes() >> 10) << " KB\n";
  if (Size() > 0) {
    ss << "\t\tbit/key:   " << BitsPerItem() << "\n";
  } else {
    ss << "\t\tbit/key:   N/A\n";
  }
  return ss.str();
}
}  // namespace cuckoofilter
#endif  // CUCKOO_FILTER_CUCKOO_FILTER_H_
