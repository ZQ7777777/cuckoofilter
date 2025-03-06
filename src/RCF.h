//probelm:
//1. 重写cuckoofilter中涉及比较的部分，使其能够实现完美的cuckoofilter √
//2. table_存在两次构造的问题
//3. 任意bits_per_item
//4. tag为0的情况
#ifndef CUCKOO_FILTER_RCF_H_
#define CUCKOO_FILTER_RCF_H_
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

// 	template <typename T>  
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
          size_t totalbits = 64>
class RCF {

  Max64Table<4> *table_;
  Max64Table<4> *suffixCache;
  // CuckooFilter

  // Number of items stored
  size_t h1, h2, h3, s;
  size_t num_items_;
  size_t bits_per_item;
  typedef struct {
    bool used;
  } VictimCache_RCF;

  VictimCache_RCF victim_;


  inline void GenerateIndexTagHash(const ItemType& item, size_t* index, size_t* tag, size_t* hashSuffix) const {
    // const uint32_t hash = CRC32Hash<ItemType>(item,0);
    uint64_t  hash;
    if (totalbits == 64) hash = LC64((uint64_t)item);
    else if (totalbits == 32) hash = LC32((uint32_t)item);
    // uint64_t hash;
    // MurmurHash3_x64_128(&item, sizeof(uint64_t), 0, &hash);
    *hashSuffix = hash & ((1ULL << (h3)) - 1);
    hash >>= h3;
    *index = hash >> h2;
    *tag = hash & ((1ULL << h2) - 1);
    
  }


  inline size_t AltIndex(const size_t index, const uint32_t tag) const {
    // NOTE(binfan): originally we use:
    // index ^ HashUtil::BobHash((const void*) (&tag), 4)) & table_->INDEXMASK;
    // now doing a quick-n-dirty way:
    // 0x5bd1e995 is the hash constant from MurmurHash2
    return (size_t)(index ^ ((tag& ((1ULL << h2) - 1)) ))& (this->table_->NumBuckets() - 1);
  }

public:
  explicit RCF(const size_t max_num_keys, const size_t tagsize, const size_t s) {
    size_t assoc = 4;
    this->h1 = std::ceil(std::log2(max_num_keys)) - 2;
    this->h2 = tagsize <= totalbits - 1 - h1 ? tagsize : totalbits - 1 - h1;
    size_t num_buckets = 1ULL << h1;
    double frac = (double)max_num_keys / num_buckets / assoc;
    std::cout << "frac: " << frac << std::endl;
    if (frac > 0.96) {
      std::cout << "----------------------------resiez happening-------------------------" << std::endl;
      num_buckets <<= 1;
    }
    this->bits_per_item = 1 + h2;
    this->h3 = totalbits - h1 - h2;
    this->num_items_ = 0;
    victim_.used = false;
    this->table_ = new Max64Table<4>(bits_per_item, num_buckets);
    this->s = s;
    // size_t layer1Bits = (1ULL << h1) * 4 * (h2+1);
    // size_t layer2Bits = layer1Bits * 0.01;
    // while ((1ULL << this->s) * 4 * (h1 + h3 + 1 - this->s) <= layer2Bits) { 
    //   this->s++;
    // }
    // this->s--;
    std::cout << "s: " << this->s << std::endl;
      // std::cout << "layer1: " << (1ULL << h1) * 4 * (h2+1) << std::endl;
      // std::cout << "PRE layer2: " << (1ULL << s) * (h1 + h3 + 1 - s) << std::endl;

    size_t num_buckets_suffixCache = 1ULL << this->s;
    size_t bits_per_item_suffixCache = h1 + h3 + 1 - this->s;
    this->suffixCache = new Max64Table<4>(bits_per_item_suffixCache, num_buckets_suffixCache);
  }

  ~RCF() {
    delete table_;
    delete suffixCache;
  }
  
  Status Add(const ItemType &item) {
    size_t i;
    size_t tag;
    size_t hs;
    if (victim_.used) {
      return NotEnoughSpace;
    }
    GenerateIndexTagHash(item, &i, &tag, &hs);
    return AddImpl(i, tag);
  }

  Status Delete(const ItemType &item) {
    size_t i1;
    size_t tag;
    size_t hs;
    GenerateIndexTagHash(item, &i1, &tag, &hs);
    size_t i2 = AltIndex(i1, tag);
    size_t i = i1 >> (h1-s);
    size_t indexSuffix = i1 & ((1ULL << (h1-s)) - 1);
    size_t combine = (indexSuffix << h3) | hs;
    table_->DeleteTagFromBucket_PCF(i1, i2, tag);
    suffixCache->DeleteTagFromBucket_suffixCache(i, combine);
    return Ok;
  }


Status AddImpl(const size_t i, const uint64_t tag)  {
  size_t curindex = i;
  uint64_t curtag = tag;
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
    curtag ^= 1ULL << h2;
  }
  victim_.used = true;
  return Ok;
}

Status Contain(const ItemType &key) const {  
  size_t i1, i2, tag, hashSuffix;
  GenerateIndexTagHash(key, &i1, &tag, &hashSuffix);
  i2 = AltIndex(i1, tag);
  // found = victim_.used && ( (tag == victim_.tag && i1 == victim_.index) || 
  // ((tag ^ (1ULL << (bits_per_item-1))) == victim_.tag && i2 == victim_.index) );
  int res = this->table_->FindTagInBuckets_RF(i1, i2, tag);
  if (res == 0) {
    return NotFound;
  } else {
    if (res == 2) {
      size_t i = i1 >> (h1-s);
      size_t indexSuffix = i1 & ((1ULL << (h1-s)) - 1);
      size_t combine = (indexSuffix << h3) | hashSuffix;
      if (this->suffixCache->FindTagInBucket_suffixCache(i, combine)) {
        return NotFound;
      }
    }
    return Ok;
  }
}
  void testGenerateIndexTagHash(const ItemType& item) const {
    // const uint32_t hash = CRC32Hash<ItemType>(item,0);
    uint64_t  hash;
    if (totalbits == 64) hash = LC64((uint64_t)item);
    else if (totalbits == 32) hash = LC32((uint32_t)item);
    // uint64_t hash;
    // MurmurHash3_x64_128(&item, sizeof(uint64_t), 0, &hash);
    size_t hashSuffix = hash & ((1ULL << (h3)) - 1);
    hash >>= h3;
    size_t index = hash >> h2;
    size_t tag = hash & ((1ULL << h2) - 1);
    size_t i2 = AltIndex(index, tag);
    std::cout << index << "  " << tag << "  " << hashSuffix<<"  " << i2 << std::endl;
    
  }
Status testContain(const ItemType &key) const {  
  size_t i1, i2, tag, hashSuffix;
  GenerateIndexTagHash(key, &i1, &tag, &hashSuffix);
  i2 = AltIndex(i1, tag);
  // found = victim_.used && ( (tag == victim_.tag && i1 == victim_.index) || 
  // ((tag ^ (1ULL << (bits_per_item-1))) == victim_.tag && i2 == victim_.index) );
  int res = this->table_->FindTagInBuckets_RF(i1, i2, tag);
  if (res == 0) {
    std::cout << "not found" << std::endl;
    return NotFound;
  } else {
    if (res == 2) {
      size_t i = i1 >> (h1-s);
      size_t indexSuffix = i1 & ((1ULL << (h1-s)) - 1);
      size_t combine = (indexSuffix << h3) | hashSuffix;
      if (this->suffixCache->FindTagInBucket_suffixCache(i, combine)) {
        std::cout << "FindTagInBucket_suffixCache" << std::endl;
        return NotFound;
      }
    }
    std::cout << "found" << std::endl;
    return Ok;
  }
}

bool adp(const ItemType &key) const { 
  size_t i1, i2, tag, hashSuffix;
  GenerateIndexTagHash(key, &i1, &tag, &hashSuffix);
  i2 = AltIndex(i1, tag);
  // table_->adp(i1, i2, tag);
  size_t i = i1 >> (h1-s);
  size_t indexSuffix = i1 & ((1ULL << (h1-s)) - 1);
  size_t combine = (indexSuffix << h3) | hashSuffix;
  size_t start = i1 & ((1ULL << 2) - 1);
  suffixCache->adp_suffixCache(i, combine, start);
  return table_->adp(i1, i2, tag);
}

bool testshow(const ItemType &key) const { 
  size_t i1, i2, tag, hashSuffix;
  GenerateIndexTagHash(key, &i1, &tag, &hashSuffix);
  i2 = AltIndex(i1, tag);
  // table_->adp(i1, i2, tag);
  // size_t i = i1 >> (h1-s);
  // size_t indexSuffix = i1 & ((1ULL << (h1-s)) - 1);
  // size_t combine = (indexSuffix << h3) | hashSuffix;
  // size_t start = i1 & ((1ULL << 2) - 1);
  // suffixCache->adp_suffixCache(i, combine, start);
  table_->show(i1);
  table_->show(i2);
  return true;
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
    ss << "RCF Status:\n"
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

