/*
 * =============================================================================
 *
 *       Filename:  sect_battle_backup_metadata.h
 *        Created:  05/07/15 10:40:51
 *         Author:  Peng Wang
 *          Email:  pw2191195@gmail.com
 *    Description:  
 *
 * =============================================================================
 */

#ifndef  __SECT_BATTLE_BACKUP_METADATA_H__
#define  __SECT_BATTLE_BACKUP_METADATA_H__

#include <alpha/time_util.h>
#include <alpha/slice.h>

namespace SectBattle {
    class BackupMetadata {
        public:
            static BackupMetadata* Create(char* data, size_t size);
            static BackupMetadata* Restore(char* data, size_t size);
            static BackupMetadata Default();
            void SetBackupStartTime(alpha::TimeStamp time);
            void SetBackupEndTime(alpha::TimeStamp time);
            void SetLatestBackupPrefix(alpha::Slice prefix);
            void SetCombatantMapDataKeyNum(uint32_t num);
            void CopyTo(std::string* saved) const;
            std::string LatestBackupPrefix() const;
            alpha::TimeStamp StartTime() const;
            alpha::TimeStamp EndTime() const;
            uint32_t CombatantMapDataKeyNum() const;

        private:
            static const int kMaxBackupPrefixSize = 20;
            static const int64_t kMagic = 0x3d8e180672a78ca5;
            BackupMetadata() = default;
            int64_t magic_;
            alpha::TimeStamp backup_start_time_;
            alpha::TimeStamp backup_end_time_;
            uint32_t combatant_map_data_key_num_;
            char backup_prefix_[kMaxBackupPrefixSize];
    };
}

#endif   /* ----- #ifndef __SECT_BATTLE_BACKUP_METADATA_H__  ----- */
