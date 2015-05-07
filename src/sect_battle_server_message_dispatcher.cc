/*
 * =============================================================================
 *
 *       Filename:  sect_battle_server_message_dispatcher.cc
 *        Created:  04/27/15 17:59:30
 *         Author:  Peng Wang
 *          Email:  pw2191195@gmail.com
 *    Description:  
 *
 * =============================================================================
 */

#include "sect_battle_server_message_dispatcher.h"
#include <alpha/logger.h>

namespace SectBattle {
    ssize_t MessageDispatcher::Dispatch(const google::protobuf::Message* m
            , char* out) {
        auto it = callbacks_.find(m->GetDescriptor());
        if (it != callbacks_.end()) {
            return it->second->OnMessage(m, out);
        } else {
            LOG_INFO << "Cannot dispatch message, " << m->DebugString();
            return 0;
        }
    }
}
