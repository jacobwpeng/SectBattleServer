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
#if 0
        LOG_INFO << "Backup prefix = " << backup_prefix;
        std::string owner_map_key = backup_prefix + "_owner_map";
        std::string owner_map_data;
        err = client_->Get(owner_map_key, &owner_map_data);
        if (err != 0) {
            LOG_ERROR << "Get owner map data from db failed, key = " << owner_map_key
                << ", err = " << err;
            return;
        }
        LOG_INFO << "owner_map_data.size() = " << owner_map_data.size();

        auto deleter = [](FILE* fp) { if (fp) ::fclose(fp); };
        std::unique_ptr<FILE, decltype(deleter)> fp(
                fopen(owner_map_file_path_.c_str(), "wb"), deleter);
        if (fp == nullptr) {
            LOG_ERROR << "Failed to open owner map file, path = " << owner_map_file_path_;
            return;
        }

        auto nbytes = ::fwrite(owner_map_data.data(), 1, owner_map_data.size(), fp.get());
        if (nbytes != owner_map_data.size()) {
            LOG_ERROR << "fwrite to owner map file failed, path = " << owner_map_file_path_
                << ", nbytes = " << nbytes;
            return;
        }

        LOG_INFO << "Recover owner map file done";

        const std::string combatant_map_data_key_prefix = backup_prefix + "_combatant_map_";
        std::vector<std::string> combatant_map_data_keys;
        err = client_->GetForwardMatchKeys(
                combatant_map_data_key_prefix,
                std::numeric_limits<int32_t>::max(), 
                std::back_inserter(combatant_map_data_keys));
        if (err) {
            LOG_ERROR << "GetForwardMatchKeys failed, combatant_map_data_key_prefix = "
                << combatant_map_data_key_prefix;
            return;
        }
        if (combatant_map_data_keys.size() != md->CombatantMapDataKeyNum()) {
            LOG_ERROR << "Mismatch key num, combatant_map_data_keys.size() = "
                << combatant_map_data_keys.size()
                << " md->CombatantMapDataKeyNum() = " << md->CombatantMapDataKeyNum();
            return;
        }

        fp.reset (::fopen(combatant_map_file_path_.c_str(), "wb"));
        if (fp == nullptr) {
            LOG_ERROR << "Failed to open combatant map file, path = "
                << combatant_map_file_path_;
            return;
        }
        for (auto i = 0u; i < md->CombatantMapDataKeyNum(); ++i) {
            std::string part_key = combatant_map_data_key_prefix + std::to_string(i);
            std::string part_val;
            err = client_->Get(part_key, &part_val);
            if (err) {
                LOG_ERROR << "Get part of combatant map data failed, part_key = "
                    << part_key;
                return;
            }
            auto nbytes = ::fwrite(part_val.c_str(), 1, part_val.size(), fp.get());
            if (nbytes != part_val.size()) {
                LOG_ERROR << "Write part of combatant_map data failed, part_key = "
                    << part_key << ", fwrite return " << nbytes;
            }
        }

        LOG_INFO << "Recover combatant map file done";
        fp.reset(::fopen(backup_metadata_file_path_.c_str(), "wb"));

        if (fp == nullptr) {
            LOG_ERROR << "Failed to open backup metadata file, path = "
                << backup_metadata_file_path_;
            return;
        }

        nbytes = ::fwrite(saved_backup_metadata.data(), 1, saved_backup_metadata.size(),
                fp.get());
        if (nbytes != saved_backup_metadata.size()) {
            LOG_ERROR << "Write backup metadata failed, path = "
                << backup_metadata_file_path_ << ", fwrite return " << nbytes;
            return;
        }

        fp.reset();
#endif
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
