#ifndef CUCKOO_FILTER_ADPPERFECTCF_H_
#define CUCKOO_FILTER_ADPPERFECTCF_H_
#pragma once

#include <assert.h>
#include <algorithm>

#include "debug.h"
#include "hashutil.h"
#include "packedtable.h"
#include "printutil.h"
#include "singletable.h"

// #include "cuckoofilter.h"
#include "crc.h"
#include "max64table.h"
#include "lchash.h"
#include "MurmurHash3.h"
#include "cuckoofilter.h"
	// template <typename T>  
// uint32_t CRC32Hash(T key, uint32_t seed) 
// {
//     char* k= (char*) malloc(sizeof(T));
//     k = (char*) memcpy(k,&key,sizeof(T));

//     uint32_t r=crc32(k,sizeof(T),seed);
//     free(k);
//     return (uint32_t) r;
// }

namespace cuckoofilter {

// enum Status {
//   Ok = 0,
//   NotFound = 1,
//   NotEnoughSpace = 2,
//   NotSupported = 3,
// };

// maximum number of cuckoo kicks before claiming failure
// const size_t kMaxCuckooCount = 500;

template <typename ItemType, 
          size_t totalbits = 32, size_t cntSize = 1>
class AdpPerfectCF {

  Max64Table<4> *table_;

  // Number of items stored
  size_t num_items_;
  size_t bits_per_item;
  typedef struct {
    size_t index;
    uint64_t tag;
    bool used;
  } VictimCache2;

  VictimCache2 victim_;


  inline void GenerateIndexTagHash64(const ItemType& item, size_t* index, uint64_t* tag) const {
    // const uint32_t hash = CRC32Hash<ItemType>(item,0);
    const uint64_t hash = LC64((uint64_t)item);
    // uint64_t hash;
    // MurmurHash3_x64_128(&item, sizeof(uint64_t), 0, &hash);
    *index = hash >> (bits_per_item - 1 - cntSize);
    *tag = hash & ((1ULL << (bits_per_item - 1 - cntSize)) - 1);
  }

  inline void GenerateIndexTagHash32(const ItemType& item, size_t* index, uint32_t* tag) const {
    // const uint32_t hash = CRC32Hash<ItemType>(item,0);
    const uint32_t hash = LC32((uint32_t)item);
    // uint32_t hash;
    // MurmurHash3_x64_128(&item, sizeof(uint32_t), 0, &hash);
    *index = hash >> (bits_per_item - 1 - cntSize);
    *tag = hash & ((1 << (bits_per_item - 1 - cntSize)) - 1);
  }

  inline size_t AltIndex(const size_t index, const uint32_t tag) const {
    // NOTE(binfan): originally we use:
    // index ^ HashUtil::BobHash((const void*) (&tag), 4)) & table_->INDEXMASK;
    // now doing a quick-n-dirty way:
    // 0x5bd1e995 is the hash constant from MurmurHash2
    return (size_t)(index ^ ((tag& ((1ULL << (bits_per_item - 1 - cntSize)) - 1)) * 0x5bd1e995))& (this->table_->NumBuckets() - 1);
  }

public:
  explicit AdpPerfectCF(const size_t max_num_keys) {
    size_t assoc = 4;
    // this->bits_per_item = static_cast<int>(totalbits - std::ceil(std::log2(max_num_keys)) + 3);
    this->bits_per_item = static_cast<int>(totalbits - std::ceil(std::log2(max_num_keys)) + 3 + cntSize);
    size_t num_buckets = 1ULL << (totalbits + 1 + cntSize - bits_per_item);
    double frac = (double)max_num_keys / num_buckets / assoc;
    std::cout << "frac: " << frac << std::endl;
    // if (frac < 0.62) {
    //   bits_per_item += 1;
    //   num_buckets >>= 1;
    // }
    // if (frac > 0.5) {
    //   bits_per_item -= 1;
    //   num_buckets <<= 1;
    // }
    this->num_items_ = 0;
    victim_.used = false;
    this->table_ = new Max64Table<4>(bits_per_item, num_buckets);
  }

  ~AdpPerfectCF() {
    delete table_;
  }

  Status Add32(const ItemType &item) {
    if (victim_.used) {
      Adp(item);
      return NotEnoughSpace;
    }
    // if (LoadFactor() > 0.95) {
    //   return NotEnoughSpace;
    // }
    size_t i;
    uint32_t tag;
    GenerateIndexTagHash32(item, &i, &tag);
    return AddImpl(i, tag);
  }

  void Delete64(const ItemType &item) {
    
    size_t i1, i2;
    uint64_t tag;
    GenerateIndexTagHash64(item, &i1, &tag);
    i2 = AltIndex(i1, tag);
    // std::cout << "victim used:  " <<victim_.used<< std::endl;
          bool found = victim_.used && ( (tag == victim_.tag && i1 == victim_.index) || 
      ((tag ^ (1ULL << (bits_per_item-1))) == victim_.tag && i2 == victim_.index) );
      if (found) {
        victim_.used = false;

      }
    table_->DeleteTagFromBucket_PCF(i1, i2, tag);


  }

    void testGenerateIndexTagHash64(const ItemType& item) const {
    // const uint32_t hash = CRC32Hash<ItemType>(item,0);
    const uint64_t hash = LC64((uint64_t)item);
    
    // uint64_t hash;
    // MurmurHash3_x64_128(&item, sizeof(uint64_t), 0, &hash);
    size_t index = hash >> (bits_per_item - 1);
    size_t tag = hash & ((1ULL << (bits_per_item - 1)) - 1);
    size_t i2 = AltIndex(index, tag);
    if (tag == 1882033792155458) {
      std::cout << "hash: " << hash << std::endl;
      std ::cout << "index: " << index << std::endl;
      std ::cout << "tag: " << tag << std::endl;
    }
      bool found = victim_.used && ( (tag == victim_.tag && index == victim_.index) || 
      ((tag ^ (1ULL << (bits_per_item-1))) == victim_.tag && i2 == victim_.index) );
    if (found) {
      std::cout << "find" << std::endl;
    }
  }
  Status Add64(const ItemType &item) {
    size_t i;
    uint64_t tag;
    if (victim_.used) {
      return NotEnoughSpace;
    }
    GenerateIndexTagHash64(item, &i, &tag);
    return AddImpl(i, tag);
  }

Status Adp(const ItemType &item)  {
  size_t i1, i2;
  uint32_t tag;
  GenerateIndexTagHash32(item, &i1, &tag);
  i2 = AltIndex(i1, tag);
  this->table_->Adp_ADPPCF(i1, i2, tag, cntSize);
  return Ok;
}

Status AddImpl(const size_t i, const uint64_t tag)  {
  size_t curindex = i;
  uint64_t curtag = tag | (1ULL << (bits_per_item-cntSize));
  uint64_t oldtag;
  for (uint32_t count = 0; count < kMaxCuckooCount; count++) {
    // std::cout << "count:   " << count << std::endl;
    bool kickout = count > 0;
    oldtag = 0;
    if (this->table_->InsertTagToBucket(curindex, curtag, kickout, oldtag)) {
      this->num_items_++;
      return Ok;
    }
    if (kickout) {
      curtag = oldtag;
    }
    curindex = AltIndex(curindex, curtag);
    curtag ^= 1ULL << (bits_per_item-1-cntSize);
  }
  // test();
  victim_.index = curindex;
  victim_.tag = curtag & ((1ULL << (bits_per_item - cntSize)) - 1);
  victim_.used = true;
  return Ok;
}

Status Contain64(const ItemType &key) const {  
  bool found = false;
  size_t i1, i2;
  uint64_t tag;
  GenerateIndexTagHash64(key, &i1, &tag);
  i2 = AltIndex(i1, tag);
  found = victim_.used && ( (tag == victim_.tag && i1 == victim_.index) || 
  ((tag ^ (1ULL << (bits_per_item-1))) == victim_.tag && i2 == victim_.index) );
  if (found || this->table_->FindTagInBuckets_PCF(i1, i2, tag)) {
    return Ok;
  } else {
    return NotFound;
  }
}

Status Contain32(const ItemType &key) const {  
  bool found = false;
  size_t i1, i2;
  uint32_t tag;
  GenerateIndexTagHash32(key, &i1, &tag);
  i2 = AltIndex(i1, tag);
  found = victim_.used && ( (tag == victim_.tag && i1 == victim_.index) || 
  ((tag ^ (1ULL << (bits_per_item-1-cntSize))) == victim_.tag && i2 == victim_.index) );
  if (found || this->table_->FindTagInBuckets_ADPPCF(i1, i2, tag, cntSize)) {
    return Ok;
  } else {
    return NotFound;
  }
}
  /* methods for providing stats  */
  // load factor is the fraction of occupancy
  double LoadFactor() const { return 1.0 * Size() / table_->SizeInTags(); }

  double BitsPerItem() const { return 8.0 * table_->SizeInBytes() / Size(); }

  // number of current inserted items;
  size_t Size() const { return num_items_; }

  // size of the filter in bytes.
  size_t SizeInBytes() const { return table_->SizeInBytes(); }

  // summary infomation
  std::string Info() const {
    std::stringstream ss;
    ss << "PerfectCuckooFilter Status:\n"
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

};


}  // namespace cuckoofilter
#endif  


