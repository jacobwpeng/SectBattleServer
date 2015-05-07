/*
 * =============================================================================
 *
 *       Filename:  sect_battle_server.cc
 *        Created:  04/27/15 18:13:06
 *         Author:  Peng Wang
 *          Email:  pw2191195@gmail.com
 *    Description:  
 *
 * =============================================================================
 */

#include "sect_battle_server.h"

#include <limits.h>
#include <functional>
#include <gflags/gflags.h>
#include <alpha/compiler.h>
#include <alpha/logger.h>
#include <alpha/event_loop.h>
#include <alpha/udp_server.h>
#include <alpha/net_address.h>
#include <alpha/mmap_file.h>
#include "tt_client.h"

#include "sect_battle_protocol.pb.h"
#include "sect_battle_server_message_dispatcher.h"
#include "sect_battle_backup_metadata.h"
#include "sect_battle_recover_coroutine.h"

DEFINE_string(data_path, "/tmp", "Path of Local mmap files");
DEFINE_string(bind_ip, "0.0.0.0", "Server ip");
DEFINE_string(backup_tt_ip, "127.0.0.1", "Backup tokyotyrant server ip");
DEFINE_int32(backup_tt_port, 8080, "Backup tokyotyrant server port");
DEFINE_int32(bind_port, 9123, "Server port");
DEFINE_bool(force_backup, false, "Backup all data to remote db");
DEFINE_bool(recovery_mode, false, "Restore data from remote db"
        ", CAUTION!!! Run in this mode only when all local mmap files are damaged");

namespace detail {
    std::unique_ptr<google::protobuf::Message> CreateMessage(const std::string& name) {
        using namespace google::protobuf;
        const Descriptor* descriptor =
            DescriptorPool::generated_pool()->FindMessageTypeByName(name);
        if (descriptor) {
            const Message* prototype =
                MessageFactory::generated_factory()->GetPrototype(descriptor);
            if (prototype) {
                return std::unique_ptr<Message>(prototype->New());
            }
        }

        return nullptr;
    }
}

namespace SectBattle {
    static const char* kBackupPrefix[2] = {"tick", "tock"};
    Server::Server(alpha::EventLoop* loop, alpha::Slice file)
        :loop_(loop), conf_file_(file.ToString()), mt_(rd_()) {
    }
    Server::~Server() = default;

    bool Server::Run() {
        using namespace std::placeholders;
        tt_client_.reset(new tokyotyrant::Client(loop_));
        owner_map_file_path_ = FLAGS_data_path + "/" + "owner_map.mmap";
        combatant_map_file_path_ = FLAGS_data_path + "/" + "combatant_map.mmap";
        backup_metadata_file_path_ = FLAGS_data_path + "/backup_metadata.mmap";
        if (FLAGS_recovery_mode) {
            return RunRecovery();
        }

        alpha::NetAddress addr(FLAGS_bind_ip, FLAGS_bind_port);
        dispatcher_.reset (new MessageDispatcher());
        dispatcher_->Register<QueryBattleFieldRequest>(
                std::bind(&Server::HandleQueryBattleField, this, _1, _2));
        dispatcher_->Register<JoinBattleRequest>(
                std::bind(&Server::HandleJoinBattle, this, _1, _2));
        dispatcher_->Register<MoveRequest>(
                std::bind(&Server::HandleMove, this, _1, _2));
        dispatcher_->Register<ChangeSectRequest>(
                std::bind(&Server::HandleChangeSect, this, _1, _2));
        dispatcher_->Register<CheckFightRequest>(
                std::bind(&Server::HandleCheckFight, this, _1, _2));
        dispatcher_->Register<ResetPositionRequest>(
                std::bind(&Server::HandleResetPosition, this, _1, _2));
        server_.reset (new alpha::UdpServer(loop_));
        loop_->RunEvery(1000, std::bind(&Server::BackupRoutine, this, false));
        bool ok =  BuildMMapedData();
        if (!ok) {
            return false;
        }

        return server_->Start(addr, std::bind(&Server::HandleMessage, this, _1, _2));
    }
    
    bool Server::RunRecovery() {
        if (recover_coroutine_ == nullptr) {
            alpha::NetAddress backup_tt_address(FLAGS_backup_tt_ip, FLAGS_backup_tt_port);
            recover_coroutine_.reset (new RecoverCoroutine(
                tt_client_.get(), 
                backup_tt_address,
                backup_metadata_file_path_, 
                owner_map_file_path_,
                combatant_map_file_path_));
        }
        const int kCheckRecoverDoneInterval = 1000; //1s
        loop_->RunEvery(kCheckRecoverDoneInterval,
                std::bind(&Server::RecoverRoutine, this));
        loop_->RunAfter(0, std::bind(&alpha::Coroutine::Resume, recover_coroutine_.get()));
        return true;
    }

    void Server::RecoverRoutine() {
        assert (recover_coroutine_);
        if (recover_coroutine_->IsDead()) {
            loop_->Quit();
        } else {
            LOG_INFO << "Still recovering...";
        }
    }

    bool Server::BuildMMapedData() {
        const int kBackupMetaDataFileSize = 20480; //10KiB
        const int kOwnerMapFileSize = 20480; //10KiB
        const int kGarrisonMapFileSize = 1 << 27; //128MiB
        const int flags = alpha::MMapFile::Flags::kCreateIfNotExists;
        auto file = alpha::MMapFile::Open(owner_map_file_path_, kOwnerMapFileSize,
                flags);
        if (file == nullptr) {
            LOG_ERROR << "Create MMapFile failed, path = " << owner_map_file_path_;
            return false;
        }

        if (file->newly_created()) {
            owner_map_ = alpha::SkipList<Pos, SectType>::Create(
                    static_cast<char*>(file->start()), file->size());
        } else {
            owner_map_ = alpha::SkipList<Pos, SectType>::Restore(
                    static_cast<char*>(file->start()), file->size());
        }
        if (owner_map_ == nullptr) {
            LOG_ERROR << "Build owner map failed, file->size() = " << file->size()
                << ", file->newly_created() = " << file->newly_created();
            return false;
        }

        owner_map_file_ = std::move(file);
        LOG_INFO << "owner map max_size = " << owner_map_->max_size();

        file = alpha::MMapFile::Open(combatant_map_file_path_, kGarrisonMapFileSize,
                flags);
        if (file == nullptr) {
            LOG_ERROR << "Create MMapFile failed, path = " << combatant_map_file_path_;
            return false;
        }
                
        if (file->newly_created()) {
            combatant_map_ = alpha::SkipList<uint32_t, CombatantLite>::Create(
                    static_cast<char*>(file->start()), file->size());
        } else {
            combatant_map_ = alpha::SkipList<uint32_t, CombatantLite>::Restore(
                    static_cast<char*>(file->start()), file->size());
        }

        if (combatant_map_ == nullptr) {
            LOG_ERROR << "Build combatant map failed, file->size() = " << file->size()
                << ", file->newly_created() = " << file->newly_created();
            return false;
        }
        combatant_map_file_ = std::move(file);
        LOG_INFO << "combatant map max_size = " << combatant_map_->max_size();

        file = alpha::MMapFile::Open(backup_metadata_file_path_,
                kBackupMetaDataFileSize, flags);
        if (file == nullptr) {
            LOG_ERROR << "Create MMapFile failed, path = " << backup_metadata_file_path_;
            return false;
        }

        if (file->newly_created()) {
            backup_metadata_ = BackupMetadata::Create(
                    static_cast<char*>(file->start()), file->size());
        } else {
            backup_metadata_ = BackupMetadata::Restore(
                    static_cast<char*>(file->start()), file->size());
        }

        if (backup_metadata_ == nullptr) {
            LOG_ERROR << "Build backup_meatdata failed, file->size() = " << file->size()
                << ", file->newly_created() = " << file->newly_created();
            return false;
        }

        backup_metadata_file_ = std::move(file);

        return true;
    }

    ssize_t Server::HandleMessage(alpha::Slice packet, char* out) {
        SectBattle::ProtocolMessage wrapper;
        if (!wrapper.ParseFromArray(packet.data(), packet.size())) {
            LOG_WARNING << "Invalid packet, packet.size() = " << packet.size();
            return -1;
        }
        auto m = detail::CreateMessage(wrapper.name());
        if (m == nullptr) {
            LOG_WARNING << "Cannot create message, name = " << wrapper.name();
            return -2;
        }

        if (!m->ParseFromString(wrapper.payload())) {
            LOG_WARNING << "Cannot parse message, name = " << wrapper.name()
                << "wrapper.payload().size() = " << wrapper.payload().size();
            return -2;
        }

        auto ret = dispatcher_->Dispatch(m.get(), out);
        LOG_INFO_IF(ret < 0) << "message_name = " << wrapper.name()
            << "ret = " << ret;
        return ret;
    }

    ssize_t Server::HandleQueryBattleField(const QueryBattleFieldRequest* req, char* out) {
        if (unlikely(!req->has_uin())) {
            return -1;
        }

        UinType uin = req->uin();
        QueryBattleFieldResponse resp;
        resp.set_uin(uin);

        auto combatant_iter = combatants_.find(uin);
        Pos pos = Pos::Create(0, 0);
        if (combatant_iter == combatants_.end()) {
            resp.set_code(static_cast<int>(Code::kNotInBattle));
        } else {
            pos = combatant_iter->second.CurrentPos();
            resp.set_code(static_cast<int>(Code::kOk));
        }

        SetBattleField(pos, resp.mutable_battle_field());
        return WriteResponse(resp, out);
    }

    ssize_t Server::HandleJoinBattle(const JoinBattleRequest* req, char* out) {
        if (unlikely(!req->has_uin() || !req->has_level())) {
            return -1;
        }

        const UinType uin = req->uin();
        const LevelType level = req->level();

        JoinBattleResponse resp;
        resp.set_uin(uin);

        auto combatant_iter = combatants_.find(uin);
        //由于用户数据和服务器数据可能不一致
        //当用户数据保存失败时可能会重复发送加入请求
        //这种情况用户只可能在对应帮派的出生点，直接返回现在的状态
        //不在出生点视为非法请求
        if (unlikely(combatant_iter != combatants_.end())) {
            auto & combatant = combatant_iter->second;
            auto sect = combatant.CurrentSect();
            if (combatant.CurrentPos() == sect->BornPos()) {
                resp.set_sect(static_cast<uint32_t>(sect->Type()));
                resp.set_code(static_cast<int>(Code::kOk));
            } else {
                resp.set_code(static_cast<int>(Code::kJoinedBattle));
            }
        } else {
            const auto sect_type = RandomSect();
            auto sect_iter = sects_.find(sect_type);
            assert (sect_iter != sects_.end());
            const auto born_pos = sect_iter->second.BornPos();
            auto field_iter = battle_field_.find(born_pos);
            assert (field_iter != battle_field_.end());

            //加入到对应门派中
            sect_iter->second.AddMember(uin);
            //加入到对应门派出生点
            auto it = field_iter->second.AddGarrison(uin, level);
            //加入到参战人员中
            auto res = combatants_.emplace(uin, Combatant(&sect_iter->second, born_pos, it));
            assert (res.second);
            combatant_iter = res.first;
            resp.set_sect(static_cast<uint32_t>(sect_type));
            resp.set_code(static_cast<int>(Code::kOk));
        }
        assert (combatant_iter != combatants_.end());
        SetBattleField(combatant_iter->second.CurrentPos(), resp.mutable_battle_field());
        return WriteResponse(resp, out);
    }

    ssize_t Server::HandleMove(const MoveRequest* req, char* out) {
        if (unlikely(!req->has_uin() || !req->has_level())) {
            return -1;
        }

        if (unlikely(!IsValidDirection(req->direction()))) {
            return -2;
        }

        MoveResponse resp;
        const UinType uin = req->uin();
        resp.set_uin(uin);
        auto combatant_iter = combatants_.find(uin);
        if (combatant_iter == combatants_.end()) {
            //没有参加的时候也没有当前位置，就不返回战场信息了
            resp.set_code(static_cast<int>(Code::kNotInBattle));
            return WriteResponse(resp, out);
        }

        auto & combatant = combatant_iter->second;

        auto current_pos = combatant.CurrentPos();
        auto direction = static_cast<Direction>(req->direction());
        auto res = current_pos.Apply(direction);
        if (!res.second) {
            //再移动就要出界了
            resp.set_code(static_cast<int>(Code::kInvalidDirection));
        } else {
            auto new_pos = res.first;
            auto current_field_iter = battle_field_.find(current_pos);
            auto new_field_iter = battle_field_.find(new_pos);
            assert (current_field_iter != battle_field_.end());
            assert (new_field_iter != battle_field_.end());
            auto & current_field = current_field_iter->second;
            auto & new_field = current_field_iter->second;

            auto owner = new_field.Owner();
            OpponentList opponents = combatant.GetOpponents(direction);
            if (owner != SectType::kNone && owner !=  combatant.CurrentSect()->Type()
                    && !opponents.empty()) {
                //移动到其它门派占领的格子, 而这个方向又没有刷新过对手，生成一下对手
                opponents = new_field.GetOpponents(req->level());
            }
            bool pos_changed = false;
            if (owner == SectType::kNone || owner == combatant.CurrentSect()->Type()
                    || opponents.empty()) {
                //移动到没有人占领，或者是属于本门派的格子, 或者这个格子没有人
                //从原来的格子里干掉自己
                current_field.ReduceGarrison(uin, combatant.Iterator());
                //放到新的格子里
                auto it = new_field.AddGarrison(uin, req->level());
                //设置新的位置
                combatant.MoveTo(new_pos);
                //设置新的驻军索引
                combatant.SetIterator(it);
                //新格子换主人了
                new_field.ChangeOwner(combatant.CurrentSect()->Type());
                resp.set_code(static_cast<int>(Code::kOk));
                pos_changed = true;
            } else {
                //移动到其它门派占领的格子上, 而且格子里有人
                assert (!opponents.empty());
                resp.set_code(static_cast<int>(Code::kOccupied));
                std::copy(opponents.begin(), opponents.end(), 
                    google::protobuf::RepeatedFieldBackInserter(resp.mutable_opponents()));
            }
            SetBattleField(pos_changed ? new_pos : current_pos, 
                    resp.mutable_battle_field());
        }
        return WriteResponse(resp, out);
    }

    ssize_t Server::HandleChangeSect(const ChangeSectRequest* req, char* out) {
        if (unlikely(!req->has_uin() || !req->has_level() || !req->has_sect())) {
            return -1;
        }

        if (unlikely(!IsValidSectType(req->sect()))) {
            return -1;
        }

        UinType uin = req->uin();
        LevelType level = req->level();
        SectType sect_type = static_cast<SectType>(req->sect());
        ChangeSectResponse resp;
        resp.set_uin(uin);

        auto combatant_iter = combatants_.find(uin);
        if (unlikely(combatant_iter == combatants_.end())) {
            //没有参与，不返回战场信息
            resp.set_code(static_cast<int>(Code::kNotInBattle));
            return WriteResponse(resp, out);
        }
        auto & combatant = combatant_iter->second;
        if (unlikely(combatant.CurrentSect()->Type() == sect_type)) {
            resp.set_code(static_cast<int>(Code::kInSameSect));
            SetBattleField(combatant.CurrentPos(), resp.mutable_battle_field());
            return WriteResponse(resp, out);
        }

        auto current_sect_iter = sects_.find(combatant.CurrentSect()->Type());
        assert (current_sect_iter != sects_.end());
        auto & current_sect = current_sect_iter->second;
        auto new_sect_iter = sects_.find(sect_type);
        assert (new_sect_iter != sects_.end());
        auto & new_sect = new_sect_iter->second;
        auto new_sect_born_pos = new_sect.BornPos();
        auto current_field_iter = battle_field_.find(combatant.CurrentPos());
        assert (current_field_iter != battle_field_.end());
        auto & current_field = current_field_iter->second;
        auto new_field_iter = battle_field_.find(new_sect_born_pos);
        assert (new_field_iter != battle_field_.end());
        auto & new_field = new_field_iter->second;

        //从旧的门派里面干掉他
        current_sect.RemoveMember(uin);
        //从原来的格子干掉他
        current_field.ReduceGarrison(uin, combatant.Iterator());
        //然后加到新的门派
        new_sect.AddMember(uin);
        //然后加到新格子中
        auto it = new_field.AddGarrison(uin, level);
        //然后更新玩家身上记录的驻军位置
        combatant.SetIterator(it);
        //更新玩家所属门派
        combatant.ChangeSect(&new_sect);
        //更新玩家当前位置
        combatant.MoveTo(new_sect_born_pos);

        resp.set_code(static_cast<int>(Code::kOk));
        SetBattleField(new_sect_born_pos, resp.mutable_battle_field());
        return WriteResponse(resp, out);
    }

    ssize_t Server::HandleCheckFight(const CheckFightRequest* req, char* out) {
        if (unlikely(!req->has_uin() || !req->has_opponent() || !req->has_direction())) {
            return -1;
        }

        if (unlikely(!IsValidDirection(req->direction()))) {
            return -1;
        }

        UinType uin = req->uin();
        UinType opponent_uin = req->opponent();
        Direction direction = static_cast<Direction>(req->direction());

        CheckFightResponse resp;
        resp.set_uin(uin);
        auto opponent_iter = combatants_.find(opponent_uin);
        if (opponent_iter == combatants_.end()) {
            resp.set_code(static_cast<int>(Code::kInvalidOpponent));
            return WriteResponse(resp, out);
        }

        auto & opponent = opponent_iter->second;

        auto combatant_iter = combatants_.find(uin);
        if (unlikely(combatant_iter == combatants_.end())) {
            resp.set_code(static_cast<int>(Code::kNotInBattle));
            return WriteResponse(resp, out);
        }

        auto & combatant = combatant_iter->second;
        auto opponents = combatant.GetOpponents(direction);

        auto res = combatant.CurrentPos().Apply(direction);
        if (res.second == false) {
            resp.set_code(static_cast<int>(Code::kInvalidDirection));
            SetBattleField(combatant.CurrentPos(), resp.mutable_battle_field());
            return WriteResponse(resp, out);
        }

        auto it = std::find(opponents.begin(), opponents.end(), opponent_uin);
        if (it == opponents.end()) {
            resp.set_code(static_cast<int>(Code::kInvalidOpponent));
            SetBattleField(combatant.CurrentPos(), resp.mutable_battle_field());
            return WriteResponse(resp, out);
        }

        //判断对手是否仍然在那个位置
        if (opponent.CurrentPos() != res.first) {
            resp.set_code(static_cast<int>(Code::kOpponentMoved));
        } else {
            resp.set_code(static_cast<int>(Code::kOk));
        }
        SetBattleField(combatant.CurrentPos(), resp.mutable_battle_field());
        return WriteResponse(resp, out);
    }

    ssize_t Server::HandleResetPosition(const ResetPositionRequest* req, char*) {
        if (unlikely(!req->has_uin())) {
            return -1;
        }

        UinType uin = req->uin();
        auto combatant_iter = combatants_.find(uin);
        if (unlikely(combatant_iter == combatants_.end())) {
            LOG_WARNING << "Invalid ResetPositionRequest from client, uin = " << uin;
            return -1;
        }

        auto & combatant = combatant_iter->second;
        const LevelType level = combatant.Iterator()->first;
        auto sect = combatant.CurrentSect();
        auto born_pos = sect->BornPos();
        auto current_field_iter = battle_field_.find(combatant.CurrentPos());
        assert (current_field_iter != battle_field_.end());
        auto new_field_iter = battle_field_.find(born_pos);
        assert (new_field_iter != battle_field_.end());

        //从之前的格子干掉他
        current_field_iter->second.ReduceGarrison(uin, combatant.Iterator());
        //加入到新的格子
        auto it = new_field_iter->second.AddGarrison(uin, level);
        //重置回对应门派出生点
        combatant.MoveTo(born_pos);
        //设置驻军位置
        combatant.SetIterator(it);

        return 0;
    }

    void Server::SetBattleField(const Pos& current_pos, BattleField* battle_field) {
        assert (battle_field);
        battle_field->mutable_self_position()->set_x(current_pos.X());
        battle_field->mutable_self_position()->set_y(current_pos.Y());

        //TODO: 是否需要cache一份，防止每次都遍历
        for (const auto & p : battle_field_) {
            const Field& field = p.second;
            auto cell = battle_field->add_field();
            cell->mutable_pos()->set_x(p.first.X());
            cell->mutable_pos()->set_y(p.first.Y());
            cell->set_owner(static_cast<unsigned>(field.Owner()));
            cell->set_garrison_num(field.GarrisonNum());
        }
        assert (battle_field->field_size() == 100);

        for (const auto & p : sects_) {
            battle_field->add_sect_members_count(p.second.MemberCount());
        }

        assert (battle_field->sect_members_count_size() 
                == static_cast<int>(SectType::kMax) - 1);
    }

    ssize_t Server::WriteResponse(const google::protobuf::Message& resp, char* out) {
        bool ok = resp.SerializeToArray(out, alpha::UdpServer::kOutputBufferSize);
        assert (ok);
        return resp.ByteSize();
    }

    void Server::BackupRoutine(bool force) {
        if (backup_metadata_->EndTime() + kBackupInterval < alpha::Now() || force) {
            if (backup_coroutine_ && !backup_coroutine_->IsDead()) {
                LOG_INFO << "Backup coroutine still running, start time = "
                    << backup_start_time_;
                return;
            } else if (backup_coroutine_ == nullptr) {
                LOG_INFO << "Start backup, force = " << (force ? "true" : "false");
                alpha::NetAddress backup_tt_address(FLAGS_backup_tt_ip, 
                        FLAGS_backup_tt_port);
                assert (current_backup_prefix_index_ == 0
                        || current_backup_prefix_index_ == 1);
                backup_coroutine_.reset(new BackupCoroutine(
                            tt_client_.get(), backup_tt_address, 
                            kBackupPrefix[current_backup_prefix_index_],
                            owner_map_file_.get(), combatant_map_file_.get(),
                            backup_metadata_));
                backup_start_time_ = alpha::Now();
                backup_coroutine_->Resume();
            }
        }

        if (backup_coroutine_ && backup_coroutine_->IsDead()) {
            if (!backup_coroutine_->succeed()) {
                LOG_WARNING << "Backup failed";
            } else {
                current_backup_prefix_index_ = 1 - current_backup_prefix_index_;
            }
            backup_coroutine_.reset();
        }
    }

    SectType Server::RandomSect() {
        auto random = dist_(mt_);
        auto max = static_cast<int>(SectType::kMax) - 1;
        return static_cast<SectType>(random % max + 1);
    }
};
