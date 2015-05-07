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
#include "sect_battle_backup_metadata.h"

namespace SectBattle {
    RecoverCoroutine::RecoverCoroutine(tokyotyrant::Client* client,
                        const alpha::NetAddress& backup_server_address,
                        alpha::Slice backup_metadata_file_path,
                        alpha::Slice owner_map_file_path,
                        alpha::Slice combatant_map_file_path)
        :client_(client), backup_server_address_(backup_server_address),
        backup_metadata_file_path_(backup_metadata_file_path.ToString()),
        owner_map_file_path_(owner_map_file_path.ToString()),
        combatant_map_file_path_(combatant_map_file_path.ToString()) {

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
        int err = client_->Get("backup_metadata", &saved_backup_metadata);
        if (err) {
            LOG_ERROR << "Get backup_metadata from server failed, err = " << err;
            return;
        }

        BackupMetadata* md = BackupMetadata::Restore(&saved_backup_metadata[0],
                saved_backup_metadata.size());

        if (md == nullptr) {
            LOG_ERROR << "Restore BackupMetadata failed, saved_backup_metadata.size() = "
                << saved_backup_metadata.size();
            LOG_ERROR << '\n' << alpha::HexDump(saved_backup_metadata);
            return;
        }

        LOG_INFO << "Last backup start time = " << md->StartTime()
            << ", end time = " << md->EndTime()
            << ", prefix = " << md->LatestBackupPrefix()
            << ", combatant map parts = " << md->CombatantMapDataKeyNum();

        const std::string backup_prefix = md->LatestBackupPrefix();

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
        LOG_INFO << "Recover from db done";
    }
}
