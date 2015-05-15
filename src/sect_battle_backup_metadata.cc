/*
 * =============================================================================
 *
 *       Filename:  sect_battle_backup_metadata.cc
 *        Created:  05/07/15 10:48:53
 *         Author:  Peng Wang
 *          Email:  pw2191195@gmail.com
 *    Description:  
 *
 * =============================================================================
 */

#include "sect_battle_backup_metadata.h"
#include <cstring>
#include <type_traits>
#include <algorithm>
#include <alpha/logger.h>
#include <alpha/format.h>

namespace SectBattle {
    static_assert (std::is_pod<BackupMetadata>::value, "BackupMetadata must be POD type");
    BackupMetadata* BackupMetadata::Create(char* data, size_t size) {
        if (size < sizeof(BackupMetadata)) {
            return nullptr;
        }

        //TODO: alignment
        BackupMetadata* md = reinterpret_cast<BackupMetadata*>(data);
        memset(md, 0x0, sizeof(BackupMetadata));
        md->magic_ = kMagic;
        return md;
    }

    BackupMetadata* BackupMetadata::Restore(char* data, size_t size) {
        if (size < sizeof(BackupMetadata)) {
            LOG_WARNING << "Invalid size = " << size
                << ", sizeof(BackupMetadata) = " << sizeof(BackupMetadata);
            return nullptr;
        }
        BackupMetadata* md = reinterpret_cast<BackupMetadata*>(data);
        if (md->magic_ != kMagic) {
            LOG_WARNING << "Mismatch magic, md->magic_ = " << md->magic_;
            return nullptr;
        }
        auto it = std::find(std::begin(md->backup_prefix_),
                std::end(md->backup_prefix_), '\0');
        if (it == std::end(md->backup_prefix_)) {
            LOG_WARNING << "Invalid backup_prefix_\n"
                << alpha::HexDump(alpha::Slice(md->backup_prefix_,
                                sizeof(md->backup_prefix_)));
            return nullptr;
        }

        return md;
    }

    BackupMetadata BackupMetadata::Default() {
        BackupMetadata md;
        memset(&md, 0x0, sizeof(md));
        md.magic_ = kMagic;
        return md;
    }

    void BackupMetadata::SetBackupStartTime(alpha::TimeStamp time) {
        backup_start_time_ = time;
    }

    void BackupMetadata::SetBackupEndTime(alpha::TimeStamp time) {
        backup_end_time_ = time;
    }

    void BackupMetadata::SetLatestBackupPrefix(alpha::Slice prefix) {
        assert (!prefix.empty());
        assert (prefix.size() < sizeof(backup_prefix_));
        ::memcpy(backup_prefix_, prefix.data(), prefix.size());
        backup_prefix_[prefix.size()] = '\0';
    }

    std::string BackupMetadata::LatestBackupPrefix() const {
        auto it = std::find(std::begin(backup_prefix_),
                std::end(backup_prefix_), '\0');
        assert (it != std::end(backup_prefix_));
        (void)it;
        return backup_prefix_;
    }

    alpha::TimeStamp BackupMetadata::StartTime() const {
        return backup_start_time_;
    }

    alpha::TimeStamp BackupMetadata::EndTime() const {
        return backup_end_time_;
    }
    
    void BackupMetadata::SetLatestBattleFieldResetTime(alpha::TimeStamp time) {
        latest_battle_field_reset_time_ = time;
    }

    alpha::TimeStamp BackupMetadata::LatestBattleFieldResetTime() const {
        return latest_battle_field_reset_time_;
    }
}
