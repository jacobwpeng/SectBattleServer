/*
 * =============================================================================
 *
 *       Filename:  sect_battle_backup_coroutine.h
 *        Created:  05/06/15 17:06:54
 *         Author:  Peng Wang
 *          Email:  pw2191195@gmail.com
 *    Description:  
 *
 * =============================================================================
 */

#ifndef  __SECT_BATTLE_BACKUP_COROUTINE_H__
#define  __SECT_BATTLE_BACKUP_COROUTINE_H__

#include <map>
#include <alpha/coroutine.h>
#include <alpha/mmap_file.h>
#include <alpha/time_util.h>
#include <alpha/net_address.h>
#include "sect_battle_server_def.h"
#include "sect_battle_backup_metadata.h"

namespace tokyotyrant {
    class Client;
}

namespace SectBattle {
    class BackupCoroutine final : public alpha::Coroutine {
        public:
            BackupCoroutine(tokyotyrant::Client* client, 
                    const alpha::NetAddress& backup_server_address, 
                    alpha::Slice backup_prefix, 
                    const MMapedFileMap& mmaped_files,
                    BackupMetadata* md);
            virtual void Routine() override;
            bool succeed() const;

        private:
            using Buffer = std::vector<char>;

            bool DeletePreviousBackup();
            bool BackupMMapedFiles(bool only_backup_metadata);
            bool BackupMMapedFilePart(alpha::Slice key, alpha::Slice data);
            tokyotyrant::Client* client_;
            alpha::NetAddress backup_server_address_;
            std::string backup_prefix_;
            std::map<std::string, Buffer> mmaped_file_copies_;
            BackupMetadata* backup_metadata_;
            bool succeed_ = false;
    };
}

#endif   /* ----- #ifndef __SECT_BATTLE_BACKUP_COROUTINE_H__  ----- */
