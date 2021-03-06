/*
 * =============================================================================
 *
 *       Filename:  sect_battle_recover_coroutine.cc
 *        Created:  05/07/15 12:03:26
 *         Author:  Peng Wang
 *          Email:  pw2191195@gmail.com
 *    Description:  
 *
 * =============================================================================
 */

#include "sect_battle_recover_coroutine.h"
#include <alpha/format.h>
#include "tt_client.h"
#include "sect_battle_server_def.h"
#include "sect_battle_backup_metadata.h"

namespace SectBattle {
    RecoverCoroutine::RecoverCoroutine(tokyotyrant::Client* client,
                        const alpha::NetAddress& backup_server_address,
                        alpha::Slice backup_metadata_file_path,
                        alpha::Slice owner_map_file_path,
                        alpha::Slice combatant_map_file_path,
                        alpha::Slice opponent_map_file_path)
        :client_(client), backup_server_address_(backup_server_address),
        backup_metadata_file_path_(backup_metadata_file_path.ToString()),
        owner_map_file_path_(owner_map_file_path.ToString()),
        combatant_map_file_path_(combatant_map_file_path.ToString()),
        opponent_map_file_path_(opponent_map_file_path.ToString()) {

        assert (client_);
    }

    void RecoverCoroutine::Routine() {
        client_->SetCoroutine(this);
        client_->Connnect(backup_server_address_);
        while (!client_->Connected()) {
            Yield();
        }

        LOG_INFO << "Recovery started";
        std::string saved_backup_metadata;
        auto md = RecoverBackupMetaData(&saved_backup_metadata);
        if (md == nullptr) {
            LOG_ERROR << "RecoverBackupMetaData failed";
            return;
        }

        LOG_INFO << "Last backup start time = " << md->StartTime()
            << ", end time = " << md->EndTime()
            << ", prefix = " << md->LatestBackupPrefix();

        const std::string backup_prefix = md->LatestBackupPrefix();

        if (!RecoverFile(backup_prefix, kOwnerMapDataKey, owner_map_file_path_)) {
            LOG_ERROR << "Recover file failed, path = " << owner_map_file_path_.data();
            return;
        }

        if (!RecoverFile(backup_prefix, kCombatantMapDataKey, combatant_map_file_path_)) {
            LOG_ERROR << "Recover file failed, path = " << combatant_map_file_path_.data();
            return;
        }

        if (!RecoverFile(backup_prefix, kOpponentMapDataKey, opponent_map_file_path_)) {
            LOG_ERROR << "Recover file failed, path = " << opponent_map_file_path_.data();
            return;
        }

        if (!SaveBackupMetaData(saved_backup_metadata)) {
            LOG_ERROR << "SaveBackupMetaData failed";
            return;
        }
        LOG_INFO << "Recover from db done";
    }

    BackupMetadata* RecoverCoroutine::RecoverBackupMetaData(std::string* buffer) {
        assert (buffer);
        int err = client_->Get(kBackupMetaDataKey, buffer);
        if (err) {
            LOG_ERROR << "Get failed, key = " << kBackupMetaDataKey
                << ", err = " << err;
            return nullptr;
        }

        BackupMetadata* res = BackupMetadata::Restore(
                (char*)(buffer->data()), buffer->size());
        LOG_ERROR_IF(res == nullptr) << "Restore BackupMetadata failed"
            ", buffer->size() = " << buffer->size();
        return res;
    }

    bool RecoverCoroutine::RecoverFile(alpha::Slice backup_prefix, alpha::Slice key,
            alpha::Slice path) {
        std::string prefix_key = backup_prefix.ToString() + "_" + key.ToString();
        std::vector<std::string> keys;
        LOG_INFO << "prefix_key = " << prefix_key << '\n';
        int err = client_->GetForwardMatchKeys(prefix_key,
                std::numeric_limits<int32_t>::max(),
                std::back_inserter(keys));
        if (err) {
            LOG_ERROR << "GetForwardMatchKeys failed, key = " << key.ToString()
                << ", err = " << err;
            return false;
        }
        auto deleter = [](FILE* fp) { if (fp) ::fclose(fp); };
        std::unique_ptr<FILE, decltype(deleter)> fp(fopen(path.data(), "wb"), deleter);
        if (fp == nullptr) {
            LOG_ERROR << "fopen failed, path = " << path.data();
            return false;
        }
        LOG_INFO << "prefix_key = " << prefix_key.data() 
            << ", keys.size() = " << keys.size();
        const int keys_size = keys.size();
        for (int i = 0; i < keys_size; ++i) {
            std::string real_key;
            if (keys_size == 1) {
                real_key = GetRealKey(backup_prefix, key);
            } else {
                real_key = GetRealKey(backup_prefix, key, i + 1);
            }
            std::string val;
            err = client_->Get(real_key, &val);
            if (err) {
                LOG_ERROR << "Get failed, key = " << real_key;
                return false;
            }
            auto nbytes = ::fwrite(val.data(), 1, val.size(), fp.get());
            if (nbytes != val.size()) {
                LOG_ERROR << "fwrite failed, key = " << real_key
                    << ", size = " << val.size()
                    << ", nbytes = " << nbytes;
                return false;
            }
            LOG_INFO << "write " << nbytes << " to " << path.data();
        }
        return true;
    }

    bool RecoverCoroutine::SaveBackupMetaData(alpha::Slice backup_metadata) {
        const alpha::Slice path = backup_metadata_file_path_;
        auto deleter = [](FILE* fp) { if (fp) ::fclose(fp); };
        std::unique_ptr<FILE, decltype(deleter)> fp(fopen(path.data(), "wb"), deleter);
        if (fp == nullptr) {
            LOG_ERROR << "fopen failed, path = " << path.data();
            return false;
        }
        auto nbytes = ::fwrite(backup_metadata.data(), 1, backup_metadata.size(), fp.get());
        LOG_ERROR_IF(nbytes != backup_metadata.size()) << "fwrite failed"
            << ", size = " << backup_metadata.size()
            << ", nbytes = " << nbytes;
        return nbytes == backup_metadata.size();
    }

    std::string RecoverCoroutine::GetRealKey(alpha::Slice prefix, alpha::Slice key,
            int part) {
        std::string res = prefix.ToString() + "_" + key.ToString();
        if (part == 0) {
            return res;
        } else {
            return res + "_" + std::to_string(part);
        }
    }
}
