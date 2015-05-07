/*
 * =============================================================================
 *
 *       Filename:  sect_battle_recover_coroutine.h
 *        Created:  05/07/15 11:57:47
 *         Author:  Peng Wang
 *          Email:  pw2191195@gmail.com
 *    Description:  
 *
 * =============================================================================
 */

#ifndef  __SECT_BATTLE_RECOVER_COROUTINE_H__
#define  __SECT_BATTLE_RECOVER_COROUTINE_H__

#include <alpha/coroutine.h>
#include <alpha/net_address.h>
namespace tokyotyrant {
    class Client;
}

namespace SectBattle {
    class BackupMetadata;
    class RecoverCoroutine final : public alpha::Coroutine {
        public:
            RecoverCoroutine(tokyotyrant::Client* client,
                    const alpha::NetAddress& backup_server_address,
                    alpha::Slice backup_metadata_file_path,
                    alpha::Slice owner_map_file_path,
                    alpha::Slice combatant_map_file_path);

            virtual void Routine() override;

        private:
            tokyotyrant::Client* client_;
            alpha::NetAddress backup_server_address_;
            std::string backup_metadata_file_path_;
            std::string owner_map_file_path_;
            std::string combatant_map_file_path_;
    };
}

#endif   /* ----- #ifndef __SECT_BATTLE_RECOVER_COROUTINE_H__  ----- */
