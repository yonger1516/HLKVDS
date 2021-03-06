#ifndef _HLKVDS_SEGMENT_H_
#define _HLKVDS_SEGMENT_H_

#include <string>
#include <sys/types.h>

#include <list>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include "KeyDigestHandle.h"
#include "Db_Structure.h"
#include "Utils.h"

namespace hlkvds {

class DataHeader;
class DataHeaderOffset;
class HashEntry;
class IndexManager;
class Volume;
class SegForReq;

class SegHeaderOnDisk {
public:
    uint64_t timestamp;
    uint64_t trx_id;
    uint32_t trx_segs;
    uint32_t checksum_data;
    uint32_t checksum_length;
    uint32_t number_keys;
public:
    SegHeaderOnDisk();
    ~SegHeaderOnDisk();
    SegHeaderOnDisk(const SegHeaderOnDisk& toBeCopied);
    SegHeaderOnDisk& operator=(const SegHeaderOnDisk& toBeCopied);

    SegHeaderOnDisk(uint64_t ts, uint64_t id, uint32_t segs, uint32_t data, uint32_t len, uint32_t keys_num);
};

class KVSlice {
public:
    KVSlice();
    ~KVSlice();
    KVSlice(const KVSlice& toBeCopied);
    KVSlice& operator=(const KVSlice& toBeCopied);

    KVSlice(const char* key, int key_len, const char* data, int data_len, bool deep_copy = false);
    KVSlice(Kvdb_Digest *digest, const char* key, int key_len,
            const char* data, int data_len);

    const Kvdb_Digest& GetDigest() const {
        return *digest_;
    }

    const char* GetKey() const {
        return key_;
    }

    const char* GetData() const {
        return data_;
    }

    std::string GetKeyStr() const;
    std::string GetDataStr() const;

    uint16_t GetKeyLen() const {
        return keyLength_;
    }
    uint16_t GetDataLen() const {
        return dataLength_;
    }

    bool IsAlignedData() const{
        return GetDataLen() == ALIGNED_SIZE;
    }

    HashEntry& GetHashEntry() const {
        return *entry_;
    }

    HashEntry& GetHashEntryBeforeGC() const {
        return *entryGC_;
    }

    uint32_t GetSegId() const {
        return segId_;
    }

    void SetKeyValue(const char* key, int key_len, const char* data, int data_len);
    void SetHashEntry(const HashEntry *hash_entry);
    void SetHashEntryBeforeGC(const HashEntry *hash_entry);
    void SetSegId(uint32_t seg_id);

private:
    const char* key_;
    uint32_t keyLength_;
    const char* data_;
    uint16_t dataLength_;
    Kvdb_Digest *digest_;
    HashEntry *entry_;
    uint32_t segId_;
    bool deepCopy_;
    HashEntry *entryGC_;

    void copy_helper(const KVSlice& toBeCopied);
    void calcDigest();

};

class Request {
public:
    enum ReqStat {
        INIT = 0,
        FAIL,
        SUCCESS
    };

public:
    Request();
    ~Request();
    Request(const Request& toBeCopied);
    Request& operator=(const Request& toBeCopied);
    Request(KVSlice& slice);

    KVSlice& GetSlice() const {
        return *slice_;
    }

    bool GetWriteStat() const {
        return stat_ == ReqStat::SUCCESS;
    }

    void SetWriteStat(bool stat);

    void SetSeg(SegForReq *seg) {
        segPtr_ = seg;
    }

    SegForReq* GetSeg() {
        return segPtr_;
    }

    void SetShardsWQId(int shards_id) {
        shardsWqId_ = shards_id;
    }

    int GetShardsWQId() {
        return shardsWqId_;
    }

    void Wait();
    void Signal();

private:
    bool done_;
    ReqStat stat_;
    KVSlice *slice_;
    mutable std::mutex mtx_;
    std::condition_variable cv_;

    SegForReq *segPtr_;
    int shardsWqId_;
};

class SegBase {
public:
    SegBase();
    ~SegBase();
    SegBase(const SegBase& toBeCopied);
    SegBase& operator=(const SegBase& toBeCopied);
    SegBase(Volume* vol);

    bool TryPut(KVSlice* slice);
    void Put(KVSlice* slice);
    bool TryPutList(std::list<KVSlice*> &slice_list);
    void PutList(std::list<KVSlice*> &slice_list);
    bool WriteSegToDevice();
    uint32_t GetFreeSize() const {
        return tailPos_ - headPos_;
    }

    int32_t GetSegId() const {
        return segId_;
    }
    void SetSegId(int32_t seg_id) {
        segId_ = seg_id;
    }

    int32_t GetKeyNum() const {
        return keyNum_;
    }

    std::list<KVSlice *>& GetSliceList() {
        return sliceList_;
    }

    Volume* GetSelfVolume() {
        return vol_;
    }

public:
    static inline size_t SizeOfSegOnDisk() {
        return sizeof(SegHeaderOnDisk);
    }

private:
    void copyHelper(const SegBase& toBeCopied);
    void fillEntryToSlice();
    bool _writeDataToDevice();
    void copyToDataBuf();

private:
    int32_t segId_;
    Volume* vol_;
    int32_t segSize_;

    uint32_t headPos_;
    uint32_t tailPos_;

    int32_t keyNum_;
    int32_t keyAlignedNum_;

    std::list<KVSlice *> sliceList_;

    char *dataBuf_;
};

class SegForReq : public SegBase {
public:
    SegForReq();
    ~SegForReq();
    SegForReq(const SegForReq& toBeCopied);
    SegForReq& operator=(const SegForReq& toBeCopied);

    SegForReq(Volume* vol, IndexManager* im, uint32_t timeout);

    bool TryPut(Request* req);
    void Put(Request* req);
    void Completion();
    void Notify(bool stat);
    bool IsExpired();

    int32_t CommitedAndGetNum() {
        return --reqCommited_;
    }

    void CleanDeletedEntry();

private:
    IndexManager* idxMgr_;
    uint32_t timeout_;
    KVTime startTime_;
    KVTime persistTime_;

    bool isCompletion_;
    bool hasReq_;

    std::atomic<int32_t> reqCommited_;
    std::list<Request *> reqList_;

    mutable std::mutex mtx_;
    std::list<HashEntry> delReqList_;
};

class SegForSlice : public SegBase {
public:
    SegForSlice();
    ~SegForSlice();
    SegForSlice(const SegForSlice& toBeCopied);
    SegForSlice& operator=(const SegForSlice& toBeCopied);

    SegForSlice(Volume* vol, IndexManager* im);

    void UpdateToIndex();
private:
    IndexManager* idxMgr_;
};

//class SegForGC : public SegBase {
//public:
//    SegForGC();
//    ~SegForGC();
//    SegForGC(const SegForGC& toBeCopied);
//    SegForGC& operator=(const SegForGC& toBeCopied);
//
//    SegForGC(Volume* vol, IndexManager* im);
//
//private:
//    IndexManager* idxMgr_;
//};

class SegForMigrate : public SegBase {
public:
    SegForMigrate();
    ~SegForMigrate();
    SegForMigrate(const SegForMigrate& toBeCopied);
    SegForMigrate& operator=(const SegForMigrate& toBeCopied);

    SegForMigrate(Volume* vol, IndexManager* im);

    void UpdateToIndex();
private:
    IndexManager* idxMgr_;
};

class SegLatencyFriendly {
public:
    SegLatencyFriendly();
    ~SegLatencyFriendly();
    SegLatencyFriendly(const SegLatencyFriendly& toBeCopied);
    SegLatencyFriendly& operator=(const SegLatencyFriendly& toBeCopied);
    SegLatencyFriendly(Volume* vol, IndexManager* idx);

    bool TryPut(KVSlice *slice);
    void Put(KVSlice *slice);
    bool TryPutList(std::list<KVSlice*> &slice_list);
    void PutList(std::list<KVSlice*> &slice_list);

    bool WriteSegToDevice();

    void UpdateToIndex();

    uint32_t GetFreeSize() const {
        return segSize_ - SegBase::SizeOfSegOnDisk() - checksumSize_;
    }

    int32_t GetSegId() const {
        return segId_;
    }

    void SegSegId(int32_t seg_id) {
        segId_ = seg_id;
    }

    int32_t GetKeyNum() const {
        return keyNum_;
    }

    std::list<KVSlice *>& GetSliceList() {
        return sliceList_;
    }

    Volume* GetSelfVolume() {
        return vol_;
    }

private:
    void copyHelper(const SegLatencyFriendly& toBeCopied);
    void fillEntryToSlice();
    bool _writeDataToDevice();
    void copyToDataBuf();

private:
    Volume* vol_;
    IndexManager* idxMgr_;

    int32_t segId_;
    int32_t segSize_;

    uint32_t headPos_;

    int32_t keyNum_;

    uint32_t checksumSize_;

    std::list<KVSlice *> sliceList_;

    char *dataBuf_;

};

}
#endif //#ifndef _HLKVDS_SEGMENT_H_
