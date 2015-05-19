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
#include <alpha/logger.h>
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
    class ChangeOpponentRequest;
    class ChangeOpponentResponse;
    class CheckFightRequest;
    class CheckFightResponse;
    class ReportFightRequest;
    class ReportFightResponse;

    //抽象类
    class MessageDispatcher;
    class BackupCoroutine;
    class RecoverCoroutine;
    class BackupMetadata;
    class ServerConf;
    class Server {
        public:
            Server(alpha::EventLoop* loop);
            ~Server();

            bool Run();

        private:
            //初始化运行时需要的各种数据结构
            bool RunRecovery();
            bool BuildMMapedData();
            template<typename T>
            std::unique_ptr<T> BuildMMapedMapFromFile(alpha::Slice key, size_t size);
            BackupMetadata* BuildBackupMetaDataFromFile(size_t size);
            std::string GetMMapedFilePath(const char* key) const;
            void BuildRunData();
            void ReadBattleFieldFromConf();
            void ReadSectFromConf();

            //主逻辑
            ssize_t HandleMessage(alpha::Slice data, char* out);
            ssize_t HandleQueryBattleField(const QueryBattleFieldRequest* req, char* out);
            ssize_t HandleJoinBattle(const JoinBattleRequest* req, char* out);
            ssize_t HandleMove(const MoveRequest* req, char* out);
            ssize_t HandleChangeSect(const ChangeSectRequest* req, char* out);
            ssize_t HandleChangeOpponent(const ChangeOpponentRequest* req, char* out);
            ssize_t HandleCheckFight(const CheckFightRequest* req, char* out);
            ssize_t HandleReportFight(const ReportFightRequest* req, char* out);
            void MoveCombatant(UinType uin, LevelType level, Combatant* combatant, Pos pos);
            SectType RandomSect();
            alpha::TimeStamp LastTimeNotInProtection() const;
            void SetBattleField(Pos current_pos, BattleField*);
            ssize_t WriteResponse(const google::protobuf::Message& resp, char* out);
            Combatant& CheckGetCombatant(UinType uin);
            Field& CheckGetField(Pos pos);
            Sect& CheckGetSect(SectType sect_type);
            void CheckResetBattleField();
            void ResetBattleField();

            //落地各种操作（备份恢复用）
            void RecordCombatant(UinType uin, Pos current_pos, LevelType level);
            void RecordCombatantDefeatedTime(UinType uin);
            void RecordSect(Pos pos, SectType sect_type);
            void RecordOpponent(UinType uin, Direction d, const OpponentList& opponents);

            //备份和恢复
            void BackupRoutine(bool force);
            void RecoverRoutine();

            static const int kBackupInterval = 30 * 60 * 1000; //30mins in milliseconds
            //static const int kBackupInterval = 10 * 1000;
            alpha::EventLoop* loop_;
            std::unique_ptr<ServerConf> conf_;
            std::uniform_int_distribution<int> dist_;
            std::unique_ptr<alpha::UdpServer> server_;
            std::unique_ptr<MessageDispatcher> dispatcher_;
            MMapedFileMap mmaped_files_;
            std::unique_ptr<OwnerMap> owner_map_;
            std::unique_ptr<CombatantMap> combatant_map_;
            std::unique_ptr<OpponentMap> opponent_map_;
            std::unique_ptr<tokyotyrant::Client> tt_client_;
            std::unique_ptr<BackupCoroutine> backup_coroutine_;
            std::unique_ptr<RecoverCoroutine> recover_coroutine_;
            std::unique_ptr<BattleField> cached_battle_field_;
            BackupMetadata* backup_metadata_ = nullptr;
            int current_backup_prefix_index_ = 0;
            alpha::TimeStamp backup_start_time_ = 0;
            std::map<Pos, Field> battle_field_;
            std::map<SectType, Sect> sects_;
            std::map<UinType, Combatant> combatants_;
    };

    template<typename T>
    std::unique_ptr<T> Server::BuildMMapedMapFromFile(alpha::Slice key, size_t size) {
        const std::string path = GetMMapedFilePath(key.data());
        const int flags = alpha::MMapFile::Flags::kCreateIfNotExists;
        auto file = alpha::MMapFile::Open(path.data(), size, flags);
        if (file == nullptr) {
            LOG_ERROR << "Open MMapFile failed, path = " << path.data()
                << ", size = " << size;
            return nullptr;
        }
        std::unique_ptr<T> res;
        if (file->newly_created()) {
            res = T::Create(static_cast<char*>(file->start()), file->size());
        } else {
            res = T::Restore(static_cast<char*>(file->start()), file->size());
        }
        if (res == nullptr) {
            LOG_ERROR << "Build mmaped map failed"
                << ", path = " << path
                << ", file->size() = " << file->size()
                << ", file->newly_created() = " << file->newly_created();
            return nullptr;
        }
        auto p = mmaped_files_.insert(std::make_pair(key.ToString(), std::move(file)));
        assert (p.second);
        (void)p;
        return std::move(res);
    }
};

#endif   /* ----- #ifndef __SECT_BATTLE_SERVER_H__  ----- */
