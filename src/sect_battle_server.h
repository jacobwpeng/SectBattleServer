/*
 * =============================================================================
 *
 *       Filename:  sect_battle_server.h
 *        Created:  04/27/15 14:41:39
 *         Author:  Peng Wang
 *          Email:  pw2191195@gmail.com
 *    Description:  门派大战服务器
 *
 * =============================================================================
 */

#ifndef  __SECT_BATTLE_SERVER_H__
#define  __SECT_BATTLE_SERVER_H__

#include <random>
#include <memory>
#include <string>
#include <alpha/slice.h>
#include <alpha/skip_list.h>
#include <alpha/time_util.h>

#include "sect_battle_server_def.h"

namespace google {
    namespace protobuf {
        class Message;
    }
}

namespace alpha {
    class MMapFile;
    class EventLoop;
    class UdpServer;
}

namespace tokyotyrant {
    class Client;
}

namespace SectBattle {
    //PB协议类
    class PBPos;
    class PBCell;
    class BattleField;
    class QueryBattleFieldRequest;
    class QueryBattleFieldResponse;
    class JoinBattleRequest;
    class JoinBattleResponse;
    class MoveRequest;
    class MoveResponse;
    class ChangeSectRequest;
    class ChangeSectResponse;
    class CheckFightRequest;
    class CheckFightResponse;
    class ResetPositionRequest;

    //抽象类
    class MessageDispatcher;
    class BackupCoroutine;
    class RecoverCoroutine;
    class BackupMetadata;
    class Server {
        public:
            Server(alpha::EventLoop* loop, alpha::Slice file);
            ~Server();

            bool Run();

        private:
            bool RunRecovery();
            bool BuildMMapedData();
            bool BuildMemoryData();
            ssize_t HandleMessage(alpha::Slice data, char* out);
            ssize_t HandleQueryBattleField(const QueryBattleFieldRequest* req, char* out);
            ssize_t HandleJoinBattle(const JoinBattleRequest* req, char* out);
            ssize_t HandleMove(const MoveRequest* req, char* out);
            ssize_t HandleChangeSect(const ChangeSectRequest* req, char* out);
            ssize_t HandleCheckFight(const CheckFightRequest* req, char* out);
            ssize_t HandleResetPosition(const ResetPositionRequest* req, char* out);
            void SetBattleField(const Pos& current_pos, BattleField*);
            ssize_t WriteResponse(const google::protobuf::Message& resp, char* out);
            void BackupRoutine(bool force);
            void RecoverRoutine();

            SectType RandomSect();

            static const int kBackupInterval = 30 * 60 * 1000; //30mins in milliseconds
            //static const int kBackupInterval = 3 * 1000;
            alpha::EventLoop* loop_;
            std::string conf_file_;
            std::random_device rd_;
            std::mt19937 mt_;
            std::uniform_int_distribution<int> dist_;
            std::unique_ptr<alpha::UdpServer> server_;
            std::unique_ptr<MessageDispatcher> dispatcher_;
            std::unique_ptr<alpha::MMapFile> owner_map_file_;
            std::unique_ptr<alpha::MMapFile> combatant_map_file_;
            std::unique_ptr<alpha::MMapFile> backup_metadata_file_;
            std::unique_ptr<alpha::SkipList<Pos, SectType>> owner_map_;
            std::unique_ptr<alpha::SkipList<UinType, CombatantLite>> combatant_map_;
            std::unique_ptr<tokyotyrant::Client> tt_client_;
            std::unique_ptr<BackupCoroutine> backup_coroutine_;
            std::unique_ptr<RecoverCoroutine> recover_coroutine_;
            BackupMetadata* backup_metadata_ = nullptr;
            int current_backup_prefix_index_ = 0;
            alpha::TimeStamp backup_start_time_ = 0;
            std::string owner_map_file_path_;
            std::string combatant_map_file_path_;
            std::string backup_metadata_file_path_;
            std::map<Pos, Field> battle_field_;
            std::map<SectType, Sect> sects_;
            std::map<UinType, Combatant> combatants_;
    };
};

#endif   /* ----- #ifndef __SECT_BATTLE_SERVER_H__  ----- */
