/*
 * =============================================================================
 *
 *       Filename:  sect_battle_backup_coroutine.cc
 *        Created:  05/06/15 17:08:22
 *         Author:  Peng Wang
 *          Email:  pw2191195@gmail.com
 *    Description:  
 *
 * =============================================================================
 */

#include "sect_battle_backup_coroutine.h"
#include <alpha/logger.h>
#include "tt_client.h"
#include "sect_battle_backup_metadata.h"

namespace SectBattle {
    BackupCoroutine::BackupCoroutine(tokyotyrant::Client* client, 
                    const alpha::NetAddress& backup_server_address, 
                    alpha::Slice backup_prefix, 
                    const MMapedFileMap& mmaped_files,
                    BackupMetadata* md)
            :client_(client), backup_server_address_(backup_server_address),
             backup_prefix_(backup_prefix.ToString()), 
             backup_metadata_(md) {
            assert (md);
            assert (client);

            for (const auto& p : mmaped_files) {
                Buffer buf(p.second->size());
                ::memcpy(buf.data(), p.second->start(), p.second->size());
                mmaped_file_copies_.emplace(p.first, buf);
            }
    }

    void BackupCoroutine::Routine() {
        Buffer & buffer = mmaped_file_copies_.at(kBackupMetaDataKey);
        BackupMetadata * md = BackupMetadata::Restore(buffer.data(), buffer.size());
        if (md == nullptr) {
            LOG_ERROR << "Restore BackupMetadata from mmaped_file_copies_ failed";
            return;
        }
        md->SetBackupStartTime(alpha::Now());
        md->SetLatestBackupPrefix(backup_prefix_);
        client_->SetCoroutine(this);
        const int kDataExpireTime = 5 * 60 * 1000; //5mins in milliseconds
        auto connect_start_time = alpha::Now();
        client_->Connnect(backup_server_address_);
        while (!client_->Connected()) {
            Yield();
        }
        static int backup_times = 0;
        if (backup_times && backup_times % 4 == 0) {
            //TT好挫居然不会自己压缩占用的文件大小，只好我们手动来了
            LOG_INFO << "Do optimize, backup_times = " << backup_times;
            int err = client_->Optimize();
            LOG_INFO_IF(err == 0) << "Optimize done";
            if (err) {
                LOG_ERROR << "Optimize failed, err = " << err;
                return;
            }
        }

        auto now  = alpha::Now();
        if (connect_start_time + kDataExpireTime < now) {
            LOG_WARNING << "Backup data expired";
            return;
        }

        if (DeletePreviousBackup() == false) {
            LOG_WARNING << "DeletePreviousBackup failed";
            return;
        }

        if (BackupMMapedFiles(false) == false) {
            LOG_WARNING << "BackupMMapedFiles without backup metadata failed";
            return;
        }
        md->SetBackupEndTime(alpha::Now());

        if(BackupMMapedFiles(true) == false) {
            LOG_WARNING << "Backup metadata failed";
            return;
        }

        ++backup_times;
        succeed_ = true;
        *backup_metadata_ = *md;
        LOG_INFO << "Backup done, prefix = " << backup_prefix_;
    }

    bool BackupCoroutine::succeed() const {
        return succeed_;
    }

    bool BackupCoroutine::DeletePreviousBackup() {
        //先清空所有prefix开头的key
        std::vector<std::string> keys;
        int err = client_->GetForwardMatchKeys(backup_prefix_,
                std::numeric_limits<int32_t>::max(),
                std::back_inserter(keys));
        if (err) {
            LOG_WARNING << "GetForwardMatchKeys failed"
                << ", backup_prefix_ = " << backup_prefix_
                << ", err = " << err;
            return false;
        }

        LOG_INFO << "keys.size() = " << keys.size();
        for (const auto& key : keys) {
            err = client_->Out(key);
            if (err) {
                LOG_WARNING << "Out failed, key = " << key << ", err = " << err;
                return false;
            }
        }
        return true;
    }

    bool BackupCoroutine::BackupMMapedFiles(bool update_backup_metadata) {
        //TT其实是有value大小限制的
        const size_t kMaxValueSize = 1 << 24;
        for (const auto& p : mmaped_file_copies_) {
            int parts = 0;
            std::string key = backup_prefix_ + "_" + p.first;

            if (!update_backup_metadata && p.first == kBackupMetaDataKey) {
                //不备份metadata
                continue;
            }
            if (update_backup_metadata && p.first != kBackupMetaDataKey) {
                //只备份metadata
                continue;
            }

            if (p.first == kBackupMetaDataKey) {
                //metadata只有一份, 所以不需要前缀
                key = p.first;
            }

            const Buffer& buffer = p.second;
            alpha::Slice data = alpha::Slice(buffer.data(), buffer.size());
            if (buffer.size() > kMaxValueSize) {
                parts = buffer.size() / kMaxValueSize + 1;
            }
            LOG_INFO << "key = " << key << ", buffer.size() = " << buffer.size()
                << ", parts = " << parts;

            //可以一次性备份的
            if (parts == 0 && !BackupMMapedFilePart(key, data)) {
                return false;
            }

            //需要多次备份的
            if (parts != 0) {
                for (int i = 0; i < parts; ++i) {
                    std::string part_key = key + "_" + std::to_string(i + 1);
                    alpha::Slice part_data = data.subslice(0, kMaxValueSize);
                    if (!BackupMMapedFilePart(part_key, part_data)) {
                        return false;
                    } else {
                        data = data.RemovePrefix(std::min(kMaxValueSize, data.size()));
                    }
                }
                assert (data.empty());
            }
        }
        return true;
    }

    bool BackupCoroutine::BackupMMapedFilePart(alpha::Slice key, alpha::Slice data) {
        LOG_INFO << "key = " << key.data();
        int err = client_->Put(key, data);
        LOG_ERROR_IF(err != 0) << "Put failed, key = " << key.ToString()
            << ", err = " << err;
        return err == 0;
    }
}
