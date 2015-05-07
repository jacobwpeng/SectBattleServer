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
                    const alpha::MMapFile* owner_map_file,
                    const alpha::MMapFile* combatant_map_file,
                    BackupMetadata* md)
            :client_(client), backup_server_address_(backup_server_address),
             backup_prefix_(backup_prefix.ToString()), 
             owner_map_buffer_(owner_map_file->size()), 
             combatant_map_buffer_(combatant_map_file->size()),
             backup_metadata_(md) {

            assert (md);
            assert (client);
            ::memcpy(owner_map_buffer_.data(), owner_map_file->start(), 
                    owner_map_file->size());
            ::memcpy(combatant_map_buffer_.data(), combatant_map_file->start(), 
                    combatant_map_file->size());
    }

    void BackupCoroutine::Routine() {
        BackupMetadata md = BackupMetadata::Default();
        md.SetBackupStartTime(alpha::Now());
        md.SetLatestBackupPrefix(backup_prefix_);
        client_->SetCoroutine(this);
        const int kDataExpireTime = 5 * 60 * 1000; //5mins in milliseconds
        auto connect_start_time = alpha::Now();
        client_->Connnect(backup_server_address_);
        while (!client_->Connected()) {
            Yield();
        }
        auto now  = alpha::Now();
        if (connect_start_time + kDataExpireTime < now) {
            LOG_WARNING << "Backup data expired";
            return;
        }
        //先清空所有prefix开头的key
        std::vector<std::string> keys;
        int err = client_->GetForwardMatchKeys(backup_prefix_,
                std::numeric_limits<int32_t>::max(),
                std::back_inserter(keys));
        if (err) {
            LOG_WARNING << "GetForwardMatchKeys failed"
                << ", backup_prefix_ = " << backup_prefix_
                << ", err = " << err;
            return;
        }

        LOG_INFO << "keys.size() = " << keys.size();
        for (const auto& key : keys) {
            err = client_->Out(key);
            if (err) {
                LOG_WARNING << "Out failed, key = " << key << ", err = " << err;
                return;
            }
        }

        std::string owner_map_key = backup_prefix_ + "_owner_map";
        auto data = alpha::Slice(owner_map_buffer_.data(), owner_map_buffer_.size());
        err = client_->Put(owner_map_key, data);
        if (err) {
            LOG_WARNING << "Put owner_map to tt failed, key = "
                << owner_map_key << ", data.size() = " << data.size();
            return;
        }
        LOG_INFO << "Put owner_map to tt done, key = " << owner_map_key;

        std::string combatant_map_key = backup_prefix_ + "_combatant_map";
        data = alpha::Slice(combatant_map_buffer_.data(), combatant_map_buffer_.size());

        const int kParts = 8;
        assert (combatant_map_buffer_.size() % kParts == 0);
        auto part_size = combatant_map_buffer_.size() / kParts;
        auto buffer = combatant_map_buffer_.data();
        md.SetCombatantMapDataKeyNum(kParts);
        LOG_INFO << "combatant map part size = " << part_size;
        for (auto i = 0u; i < kParts; ++i) {
            std::string part_key = combatant_map_key + "_" + std::to_string(i);
            err = client_->Put(part_key, alpha::Slice(buffer, part_size));
            if (err) {
                LOG_WARNING << "Put combatant_map part " << i << " to tt failed, key = "
                    << part_key << ", offset = " << buffer - combatant_map_buffer_.data();
                assert (false);
                return;
            }
            LOG_INFO << "Put combatant_map part " << i << " done";
            buffer += part_size;
        }
        LOG_INFO << "Put combatant_map to tt done, key = " << owner_map_key;

        md.SetBackupEndTime(alpha::Now());
        std::string saved;
        md.CopyTo(&saved);

        err = client_->Put("backup_metadata", saved);
        if (err) {
            LOG_WARNING << "Put backup metadata failed";
            return;
        }
        LOG_INFO << "\n" << alpha::HexDump(saved);
        succeed_ = true;
        *backup_metadata_ = md;
        LOG_INFO << "Backup done, prefix = " << backup_prefix_;
    }

    bool BackupCoroutine::succeed() const {
        return succeed_;
    }
}
