/*
 * =============================================================================
 *
 *       Filename:  sect_battle_server_conf.h
 *        Created:  05/14/15 15:26:10
 *         Author:  Peng Wang
 *          Email:  pw2191195@gmail.com
 *    Description:  
 *
 * =============================================================================
 */

#ifndef  __SECT_BATTLE_SERVER_CONF_H__
#define  __SECT_BATTLE_SERVER_CONF_H__

#include <map>
#include <alpha/slice.h>
#include "sect_battle_server_def.h"

namespace SectBattle {
    class ServerConf {
        public:
            static std::unique_ptr<ServerConf> ReadFromFile(alpha::Slice file);

            Pos GetBornPos(SectType sect_type) const;
            bool InSameSeason(time_t lhs, time_t rhs) const;

        private:
            std::map<SectType, Pos> born_positions_;
    };
}

#endif   /* ----- #ifndef __SECT_BATTLE_SERVER_CONF_H__  ----- */
