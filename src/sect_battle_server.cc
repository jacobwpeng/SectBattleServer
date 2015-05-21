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
#include <google/protobuf/descriptor.h>
#include <gflags/gflags.h>
#include <alpha/compiler.h>
#include <alpha/logger.h>
#include <alpha/event_loop.h>
#include <alpha/udp_server.h>
#include <alpha/net_address.h>
#include <alpha/mmap_file.h>
#include <alpha/random.h>
#include "tt_client.h"

#include "sect_battle_protocol.pb.h"
#include "sect_battle_server_message_dispatcher.h"
#include "sect_battle_backup_metadata.h"
#include "sect_battle_backup_coroutine.h"
#include "sect_battle_recover_coroutine.h"
#include "sect_battle_server_conf.h"

DEFINE_string(conf_path, "sect_battle_svrd.conf", "战场信息配置文件路径");
DEFINE_string(data_path, "/tmp", "mmap文件存放路径");
DEFINE_string(bind_ip, "0.0.0.0", "服务器监听的本地ip地址");
DEFINE_int32(bind_port, 9123, "服务器监听的本地端口");
DEFINE_string(backup_tt_ip, "127.0.0.1", "备份TT的ip地址");
DEFINE_int32(backup_tt_port, 8080, "备份TT的端口");
DEFINE_int32(battle_field_cache_ttl, 0, "返回给CGI的全局战场信息缓存时间(毫秒)");
DEFINE_bool(recovery_mode, false, "以恢复模式启动，从备份TT恢复mmap文件\n"
        "注意，使用本选项会覆盖本地所有mmap文件！");

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
    Server::Server(alpha::EventLoop* loop)
        :loop_(loop) {
    }
    Server::~Server() = default;

    bool Server::Run() {
        conf_ = ServerConf::ReadFromFile(FLAGS_conf_path);
        if (conf_ == nullptr) {
            return false;
        }

        using namespace std::placeholders;
        tt_client_.reset(new tokyotyrant::Client(loop_));
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
        dispatcher_->Register<ChangeOpponentRequest>(
                std::bind(&Server::HandleChangeOpponent, this, _1, _2));
        dispatcher_->Register<CheckFightRequest>(
                std::bind(&Server::HandleCheckFight, this, _1, _2));
        dispatcher_->Register<ReportFightRequest>(
                std::bind(&Server::HandleReportFight, this, _1, _2));
        server_.reset (new alpha::UdpServer(loop_));
        //loop_->RunEvery(1000, std::bind(&Server::BackupRoutine, this, false));
        loop_->RunEvery(1000, std::bind(&Server::CheckResetBattleField, this));
        bool ok =  BuildMMapedData();
        if (!ok) {
            return false;
        }
        LOG_INFO << "BuildMMapedData done";
        BuildRunData();
        LOG_INFO << "BuildRunData done";

        auto it = std::find(std::begin(kBackupPrefix),
                std::end(kBackupPrefix), backup_metadata_->LatestBackupPrefix());
        if (it == std::end(kBackupPrefix)) {
            current_backup_prefix_index_ = 0;
        } else {
            current_backup_prefix_index_ = std::distance(it, std::end(kBackupPrefix)) - 1;
            assert (current_backup_prefix_index_ == 0 || current_backup_prefix_index_ == 1);
        }
        LOG_INFO << "owner_map_->max_size() = " << owner_map_->max_size();
        LOG_INFO << "combatant_map_->max_size() = " << combatant_map_->max_size();
        LOG_INFO << "opponent_map_->max_size() = " << opponent_map_->max_size();
        //只对加入战场做了限制，所以记录对手的map的上限要大于等于战场人数上限
        assert (opponent_map_->max_size() >= combatant_map_->max_size());
        return server_->Start(addr, std::bind(&Server::HandleMessage, this, _1, _2));
    }
    
    bool Server::RunRecovery() {
        if (recover_coroutine_ == nullptr) {
            alpha::NetAddress backup_tt_address(FLAGS_backup_tt_ip, FLAGS_backup_tt_port);
            recover_coroutine_.reset (new RecoverCoroutine(
                tt_client_.get(), 
                backup_tt_address,
                GetMMapedFilePath(kBackupMetaDataKey),
                GetMMapedFilePath(kOwnerMapDataKey),
                GetMMapedFilePath(kCombatantMapDataKey),
                GetMMapedFilePath(kOpponentMapDataKey)
                )
            );
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
        const int kOwnerMapFileSize = 20480; //10KiB
        const int kBackupMetaDataFileSize = 20480; //10KiB
        const int kCombatantMapFileSize = 120 * (1 << 20); //120MiB
        const int kOpponentMapFileSize = 200 * (1 << 20); //220MiB
        //这个SkipList还是太耗费内存了，迫切需要改进

        owner_map_ = BuildMMapedMapFromFile<OwnerMap>(kOwnerMapDataKey,
                kOwnerMapFileSize);
        if (owner_map_ == nullptr) {
            return false;
        }

        combatant_map_ = BuildMMapedMapFromFile<CombatantMap>(kCombatantMapDataKey,
                kCombatantMapFileSize);
        if (combatant_map_ == nullptr) {
            return false;
        }

        opponent_map_ = BuildMMapedMapFromFile<OpponentMap>(kOpponentMapDataKey,
                kOpponentMapFileSize);
        if (opponent_map_ == nullptr) {
            return false;
        }

        backup_metadata_ = BuildBackupMetaDataFromFile(kBackupMetaDataFileSize);
        if (backup_metadata_ == nullptr) {
            return false;
        }
        return true;
    }

    BackupMetadata* Server::BuildBackupMetaDataFromFile(size_t size) {
        std::string path = GetMMapedFilePath(kBackupMetaDataKey);
        const int flags = alpha::MMapFile::Flags::kCreateIfNotExists;
        auto file = alpha::MMapFile::Open(path.data(), size, flags);
        if (file == nullptr) {
            LOG_ERROR << "Open MMapFile failed, path = " << path.data()
                << ", size = " << size;
            return nullptr;
        }

        BackupMetadata* res;
        if (file->newly_created()) {
            res = BackupMetadata::Create(
                    static_cast<char*>(file->start()), file->size());
        } else {
            res = BackupMetadata::Restore(
                    static_cast<char*>(file->start()), file->size());
        }

        if (res == nullptr) {
            LOG_ERROR << "Build backup_meatdata failed"
                << "path = " << path.data()
                << ", file->size() = " << file->size()
                << ", file->newly_created() = " << file->newly_created();
            return nullptr;
        }
        auto p = mmaped_files_.insert(std::make_pair(kBackupMetaDataKey, std::move(file)));
        assert (p.second);
        (void)p;
        return res;
    }

    std::string Server::GetMMapedFilePath(const char* key) const {
        return FLAGS_data_path + "/" + std::string(key) + ".mmap";
    }

    void Server::BuildRunData() {
        //从通过mmap落地的三个SkipList中构造出跑在内存里的各种数据
        //本来只有一份数据是最好维护的，但是由于结构比较复杂，map里面嵌套各种东西
        //所以就维护了两份数据，不过运行时只会写SkipList，不会读
        //所以也不存在不一致的情况
        //先确定是初始状态
        assert (battle_field_.empty());
        assert (sects_.empty());
        assert (combatants_.empty());

        ReadBattleFieldFromConf();
        ReadSectFromConf();

        //恢复格子信息
        for (auto it = owner_map_->begin(); it != owner_map_->end(); ++it) {
            Field& field = CheckGetField(it->first);
            field.ChangeOwner(it->second);
        }

        //恢复玩家信息
        for (auto it = combatant_map_->begin(); it != combatant_map_->end(); ++it) {
            UinType uin = it->first;
            const CombatantLite& lite = it->second;
            Field& field = CheckGetField(lite.pos);
            auto combatant_iter = field.AddGarrison(uin, lite.level,
                    lite.last_defeated_time);
            Sect& sect = CheckGetSect(field.Owner());
            auto res = combatants_.emplace(std::piecewise_construct,
                    std::forward_as_tuple(uin),
                    std::forward_as_tuple(&sect, lite.pos, combatant_iter));
            assert (res.second);
            (void)res;
        }

        //恢复玩家的对手信息
        for (auto it = opponent_map_->begin(); it != opponent_map_->end(); ++it) {
            UinType uin = it->first;
            Combatant& combatant = CheckGetCombatant(uin);
            for (int i = static_cast<int>(Direction::kUp);
                    i <= static_cast<int>(Direction::kDown);
                    ++i) {
                assert (IsValidDirection(i));
                Direction d = static_cast<Direction>(i);
                auto opponents = it->second.GetOpponents(d);
                if (!opponents.empty()) {
                    combatant.ChangeOpponents(d, opponents);
                }
            }
        }
    }

    void Server::ReadBattleFieldFromConf() {
        //读取战场的初始化状态
        //先生成出生点
        for (int sect = static_cast<int>(SectType::kNone) + 1;
                sect != static_cast<int>(SectType::kMax);
                ++sect ) {
            auto sect_type = static_cast<SectType>(sect);
            auto pos = conf_->GetBornPos(sect_type);
            assert (pos.Valid());
            auto res = battle_field_.emplace(std::piecewise_construct,
                            std::forward_as_tuple(pos),
                            std::forward_as_tuple(sect_type, FieldType::kBornField));
            assert (res.second);
            (void)res;
        }
        //然后剩下的都是普通点（资源点对服务器来说并没有用）
        for (int x = 0; x <= Pos::kMaxPos; ++x) {
            for (int y = 0; y <= Pos::kMaxPos; ++y) {
                auto pos = Pos::Create(x, y);
                assert (pos.Valid());
                auto it = battle_field_.find(pos);
                if (it != battle_field_.end()) {
                    continue;
                }
                auto res = battle_field_.emplace(std::piecewise_construct,
                        std::forward_as_tuple(pos),
                        std::forward_as_tuple(SectType::kNone, FieldType::kDefault));
                assert (res.second);
                (void)res;
            }
        }
    }

    void Server::ReadSectFromConf() {
        //根据战场中的出生点生成门派
        for (int sect = static_cast<int>(SectType::kNone) + 1;
                sect != static_cast<int>(SectType::kMax);
                ++sect ) {
            auto sect_type = static_cast<SectType>(sect);
            auto pos = conf_->GetBornPos(sect_type);
            assert (pos.Valid());
            auto res = sects_.emplace(std::piecewise_construct,
                    std::forward_as_tuple(sect_type),
                    std::forward_as_tuple(sect_type, pos));
            assert (res.second);
            (void)res;
        }
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
        LOG_INFO_IF(ret < 0) << "Process failed, message_name = " << wrapper.name()
            << ", ret = " << ret;
        return ret;
    }

    ssize_t Server::HandleQueryBattleField(const QueryBattleFieldRequest* req, char* out) {
        if (unlikely(!req->has_uin() || !req->has_level())) {
            return -1;
        }

        UinType uin = req->uin();
        QueryBattleFieldResponse resp;
        resp.set_uin(uin);

        auto combatant_iter = combatants_.find(uin);
        Pos pos = Pos::CreateInvalid();
        if (combatant_iter == combatants_.end()) {
            resp.set_code(static_cast<int>(Code::kNotInBattle));
        } else {
            pos = combatant_iter->second.CurrentPos();
            resp.set_code(static_cast<int>(Code::kOk));
            //处理等级变了的情况
            auto & combatant = combatant_iter->second;
            auto old_level = std::get<kCombatantLevel>(*combatant.Iterator());
            if (req->level() != old_level) {
                Field& field = CheckGetField(combatant.CurrentPos());
                combatant.SetIterator(field.UpdateGarrisonLevel(
                            uin, req->level(), combatant.Iterator()));
            }
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
        } else if (unlikely(combatant_map_->size() == combatant_map_->max_size())) {
            //落地用的mmaped文件已经满了, 没法再增加人了
            resp.set_code(static_cast<int>(Code::kBattleFieldFull));
            return WriteResponse(resp, out);
        } else {
            const auto sect_type = RandomSect();
            LOG_INFO << "Combatant" << uin << " join battle"
                << ", sect = " << sect_type
                << ", level = " << level;

            Sect& sect = CheckGetSect(sect_type);
            Field& field = CheckGetField(sect.BornPos());

            //加入到对应门派中
            sect.AddMember(uin);
            //加入到对应门派出生点
            GarrisonIterator it = field.AddGarrison(uin, level);
            assert (std::get<kCombatantUin>(*it) == uin);
            //加入到参战人员中
            auto res = combatants_.emplace(std::piecewise_construct,
                    std::forward_as_tuple(uin),
                    std::forward_as_tuple(&sect, sect.BornPos(), it));
            assert (res.second);
            combatant_iter = res.first;
            resp.set_sect(static_cast<uint32_t>(sect_type));
            resp.set_code(static_cast<int>(Code::kOk));
            RecordCombatant(uin, sect.BornPos(), level);
        }
        assert (combatant_iter != combatants_.end());
        SetBattleField(combatant_iter->second.CurrentPos(), resp.mutable_battle_field());
        return WriteResponse(resp, out);
    }

    ssize_t Server::HandleMove(const MoveRequest* req, char* out) {
        if (unlikely(!req->has_uin() 
                    || !req->has_level()
                    || !req->has_direction()
                    || !req->has_can_move())) {
            return -1;
        }

        if (unlikely(!IsValidDirection(req->direction()))) {
            return -2;
        }

        MoveResponse resp;
        const UinType uin = req->uin();
        bool can_move = req->can_move(); //是否有足够的行动力进行移动
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
            DLOG_INFO << "current_pos.x = " << current_pos.X()
                << ", current_pos.y = " << current_pos.Y()
                << ", direction = " << direction;
            resp.set_code(static_cast<int>(Code::kInvalidDirection));
        } else {
            auto new_pos = res.first;
            Field& new_field = CheckGetField(new_pos);

            auto owner = new_field.Owner();
            OpponentList opponents = combatant.GetOpponents(direction);
            if (owner != SectType::kNone && owner != combatant.CurrentSect()->Type()
                    && opponents.empty()) {
                //移动到其它门派占领的格子, 而这个方向又没有刷新过对手，生成一下对手
                opponents = new_field.GetOpponents(req->level(), LastTimeNotInProtection());
            }
            bool pos_changed = false;
            //几种可能可以移动的情况
            if (owner == SectType::kNone
                    || owner == combatant.CurrentSect()->Type()
                    || opponents.empty()) {
                bool perform_move_action = false;
                if (owner == SectType::kNone || owner == combatant.CurrentSect()->Type()) {
                    //移动到没人占领，或者是属于本门派的格子
                    if (!can_move) {
                        resp.set_code(static_cast<int>(Code::kCannotMove));
                    } else {
                        perform_move_action = true;
                    }
                } else {
                    //移动到其它门派的格子，但是刷新不到对手
                    assert (opponents.empty());
                    if (new_field.GarrisonNum() != 0) {
                        //有人在却刷不出来对手，说明他们都在保护期
                        resp.set_code(static_cast<int>(Code::kNoOpponentFound));
                    } else if (conf_->GetBornPos(owner) == new_pos) {
                        //虽然没人，但是这是人家出生点
                        resp.set_code(static_cast<int>(Code::kCannotMoveToBornPos));
                    } else if (!can_move) {
                        //最后才判断是不是行动力不足
                        resp.set_code(static_cast<int>(Code::kCannotMove));
                    } else {
                        perform_move_action = true;
                    }
                }
                if (perform_move_action) {
                    LOG_INFO << "Combatant " << uin << " pos changed, old pos = "
                        << current_pos << ", new pos = " << new_pos;
                    if (owner != combatant.CurrentSect()->Type()) {
                        //这个格子换主人了
                        LOG_INFO << "Field owner changed, old = " << owner
                            << ", new = " << combatant.CurrentSect()->Type();
                        new_field.ChangeOwner(combatant.CurrentSect()->Type());
                    }
                    //更新玩家的位置
                    MoveCombatant(uin, req->level(), &combatant, new_pos);
                    resp.set_code(static_cast<int>(Code::kOk));
                    pos_changed = true;
                    RecordSect(new_pos, combatant.CurrentSect()->Type());
                }
            } else {
                //移动到其它门派占领的格子上, 而且能够刷新到对手
                assert (!opponents.empty());
                LOG_INFO << "Combatant " << uin << " opponents changed";
                combatant.ChangeOpponents(direction, opponents);
                resp.set_code(static_cast<int>(Code::kOccupied));
                std::copy(opponents.begin(), opponents.end(), 
                    google::protobuf::RepeatedFieldBackInserter(resp.mutable_opponents()));
                RecordOpponent(uin, direction, opponents);
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

        LOG_INFO << "Combatant " << uin << " sect changed"
            << ", old sect = " << combatant.CurrentSect()->Type()
            << ", new sect = " << sect_type;

        Sect& current_sect = CheckGetSect(combatant.CurrentSect()->Type());
        Sect& new_sect = CheckGetSect(sect_type);
        auto new_sect_born_pos = new_sect.BornPos();

        //从旧的门派里面干掉他
        current_sect.RemoveMember(uin);
        //然后加到新的门派
        new_sect.AddMember(uin);
        //更新玩家所属门派
        combatant.ChangeSect(&new_sect);
        //更新玩家的位置
        MoveCombatant(uin, level, &combatant, new_sect_born_pos);
        resp.set_code(static_cast<int>(Code::kOk));
        SetBattleField(new_sect_born_pos, resp.mutable_battle_field());
        return WriteResponse(resp, out);
    }

    ssize_t Server::HandleChangeOpponent(const ChangeOpponentRequest* req, char* out) {
        if (unlikely(!req->has_uin() || !req->has_level() || !req->has_direction())) {
            return -1;
        }
        if (unlikely(!IsValidDirection(req->direction()))) {
            return -2;
        }

        UinType uin = req->uin();
        LevelType level = req->level();
        Direction direction = static_cast<Direction>(req->direction());

        ChangeOpponentResponse resp;
        resp.set_uin(uin);
        auto it = combatants_.find(uin);
        if (it == combatants_.end()) {
            resp.set_code(static_cast<int>(Code::kNotInBattle));
            return WriteResponse(resp, out);
        }

        auto & combatant = it->second;
        auto old_opponents = combatant.GetOpponents(direction);
        if (old_opponents.empty()) {
            resp.set_code(static_cast<int>(Code::kNoOpponent));
            return WriteResponse(resp, out);
        }
        Field& field = CheckGetField(combatant.CurrentPos());
        auto new_opponents = field.GetOpponents(level, LastTimeNotInProtection());
        if (new_opponents.empty()) {
            resp.set_code(static_cast<int>(Code::kNoOpponentFound));
        } else {
            LOG_INFO << "Combatant " << uin << " opponents changed";
            combatant.ChangeOpponents(direction, new_opponents);
            std::copy(new_opponents.begin(), new_opponents.end(), 
                google::protobuf::RepeatedFieldBackInserter(resp.mutable_opponents()));
            RecordOpponent(uin, direction, new_opponents);
            resp.set_code(static_cast<int>(Code::kOk));
        }
        SetBattleField(combatant.CurrentPos(), resp.mutable_battle_field());
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
            //SetBattleField(combatant.CurrentPos(), resp.mutable_battle_field());
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

    ssize_t Server::HandleReportFight(const ReportFightRequest* req, char* out) {
        if (unlikely(!req->has_uin() 
                    || !req->has_opponent() 
                    || !req->has_loser()
                    || !req->has_direction()
                    || !req->has_reset_self() 
                    || !req->has_reset_opponent()
                    || !req->has_level()
                    || !req->has_opponent_level())) {
            return -1;
        }

        if (unlikely(!IsValidDirection(req->direction()))) {
            return -1;
        }

        auto uin = req->uin();
        auto opponent_uin = req->opponent();
        auto loser = req->loser();
        auto direction = static_cast<Direction>(req->direction());
        auto should_reset_self = req->reset_self();
        auto should_reset_opponent = req->reset_opponent();

        ReportFightResponse resp;
        resp.set_uin(uin);
        auto combatant_iter = combatants_.find(uin);
        auto opponent_iter = combatants_.find(opponent_uin);
        if (combatant_iter == combatants_.end()) {
            resp.set_code(static_cast<int>(Code::kNotInBattle));
        } else if (opponent_iter == combatants_.end()) {
            resp.set_code(static_cast<int>(Code::kInvalidOpponent));
        } else {
            LOG_INFO << "Combatant " << uin << " report fight"
                << ", opponent_uin = " << opponent_uin
                << ", loser = " << loser
                << ", direction = " << direction
                << ", should_reset_self = " << static_cast<int>(should_reset_self)
                << ", should_reset_opponent = " << static_cast<int>(should_reset_opponent);
            auto& self = combatant_iter->second;
            auto& opponent = opponent_iter->second;
            self.ClearOpponents(direction);
            auto expected_opponent_pos = self.CurrentPos();
            auto res = expected_opponent_pos.Apply(direction);
            assert (res.second);
            assert (opponent.CurrentPos() == expected_opponent_pos);
            (void)res;

            if (should_reset_self) {
                MoveCombatant(uin, req->level(), &self, self.CurrentSect()->BornPos());
            }
            if (should_reset_opponent) {
                MoveCombatant(opponent_uin, req->opponent_level(), &opponent,
                        opponent.CurrentSect()->BornPos());
            }
            RecordCombatantDefeatedTime(loser);
            SetBattleField(self.CurrentPos(), resp.mutable_battle_field());
        }
        return WriteResponse(resp, out);
    }

    void Server::MoveCombatant(UinType uin, LevelType level,
            Combatant* combatant, Pos new_pos) {
        assert (combatant);
        auto it = combatants_.find(uin);
        assert (it != combatants_.end());
        assert (&it->second == combatant);
        (void)it;
        if (combatant->CurrentPos() == new_pos) {
            return;
        }
        Field& current_field = CheckGetField(combatant->CurrentPos());
        Field& new_field = CheckGetField(new_pos);

        //从旧的格子里干掉
        current_field.ReduceGarrison(uin, combatant->Iterator());
        //放到新的格子中
        auto new_iter = new_field.AddGarrison(uin, level);
        //然后更新玩家的当前位置
        combatant->MoveTo(new_pos);
        //更新玩家在格子中的索引
        combatant->SetIterator(new_iter);
        //落地改动
        RecordCombatant(uin, new_pos, level);
    }

    void Server::RecordCombatant(UinType uin, Pos current_pos, LevelType level) {
        (*combatant_map_)[uin] = CombatantLite::Create(current_pos, level);
    }

    void Server::RecordCombatantDefeatedTime(UinType uin) {
        auto it = combatant_map_->find(uin);
        assert (it != combatant_map_->end());
        it->second.last_defeated_time = alpha::Now();
    }

    void Server::RecordSect(Pos pos, SectType sect_type) {
        (*owner_map_)[pos] = sect_type;
    }

    void Server::RecordOpponent(UinType uin, Direction d, const OpponentList& opponents) {
        auto it = opponent_map_->find(uin);
        if (it == opponent_map_->end()) {
            auto p = opponent_map_->insert(std::make_pair(uin, OpponentLite::Default()));
            it = p.first;
        }
        assert (it != opponent_map_->end());
        it->second.ChangeOpponents(d, opponents);
    }

    void Server::SetBattleField(Pos current_pos, BattleField* battle_field) {
        assert (battle_field);
        if (unlikely(cached_battle_field_ == nullptr)) {
            cached_battle_field_.reset(new BattleField);
        }
        static alpha::TimeStamp cache_create_time = 0;

        if (cache_create_time + FLAGS_battle_field_cache_ttl < alpha::Now()) {
            cached_battle_field_->Clear();
            cached_battle_field_->mutable_self_position()->set_x(current_pos.X());
            cached_battle_field_->mutable_self_position()->set_y(current_pos.Y());

            for (const auto & p : battle_field_) {
                const Field& field = p.second;
                auto cell = cached_battle_field_->add_field();
                cell->set_owner(static_cast<unsigned>(field.Owner()));
                cell->set_garrison_num(field.GarrisonNum());
            }
            assert (cached_battle_field_->field_size() == 100);
            cache_create_time = alpha::Now();
        }
        battle_field->CopyFrom(*cached_battle_field_);
    }

    ssize_t Server::WriteResponse(const google::protobuf::Message& resp, char* out) {
        bool ok = resp.SerializeToArray(out, resp.ByteSize());
        assert (ok);
        (void)ok;
        DLOG_INFO << "resp.ByteSize() = " << resp.ByteSize();
        return resp.ByteSize();
    }

    Combatant& Server::CheckGetCombatant(UinType uin) {
        auto it = combatants_.find(uin);
        assert (it != combatants_.end());
        return it->second;
    }

    Field& Server::CheckGetField(Pos pos) {
        auto it = battle_field_.find(pos);
        assert (it != battle_field_.end());
        return it->second;
    }

    Sect& Server::CheckGetSect(SectType sect_type) {
        auto it = sects_.find(sect_type);
        assert (it != sects_.end());
        return it->second;
    }

    void Server::CheckResetBattleField() {
        auto now = alpha::NowInSeconds();
        if (!conf_->InSameSeason(now, backup_metadata_->LatestBattleFieldResetTime())) {
            LOG_INFO << "now = " << now << ", LatestBattleFieldResetTime = "
                << backup_metadata_->LatestBattleFieldResetTime();;
            ResetBattleField();
            backup_metadata_->SetLatestBattleFieldResetTime(now);
        }
    }

    void Server::ResetBattleField() {
        //由于mmaped下来的skiplist依赖于alpha::MemoryList
        //而MemoryList是双链表实现，所以clear的时候会重建这两个双链表
        //这样要遍历整个文件，所以性能严重依赖于是否把mmaped文件放在tmpfs中
        LOG_INFO << "ResetBattleField start";
        battle_field_.clear();
        sects_.clear();
        combatants_.clear();
        owner_map_->clear();
        opponent_map_->clear();
        combatant_map_->clear();
        ReadBattleFieldFromConf();
        ReadSectFromConf();
        LOG_INFO << "ResetBattleField done";
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
                LOG_INFO << "Before create BackupCoroutine";
                backup_coroutine_.reset(new BackupCoroutine(
                            tt_client_.get(),
                            backup_tt_address,
                            kBackupPrefix[current_backup_prefix_index_],
                            mmaped_files_,
                            backup_metadata_));
                LOG_INFO << "After create BackupCoroutine";
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

    alpha::TimeStamp Server::LastTimeNotInProtection() const {
        auto now = alpha::Now();
        return now - conf_->DefeatedProtectionDuration();
    }

    SectType Server::RandomSect() {
        auto max = static_cast<int>(SectType::kMax);
        return static_cast<SectType>(alpha::Random::Rand32(1, max));
    }
};
