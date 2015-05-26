/*
 * =============================================================================
 *
 *       Filename:  sect_battle_server_def.h
 *        Created:  04/27/15 14:44:19
 *         Author:  Peng Wang
 *          Email:  pw2191195@gmail.com
 *    Description:  门派大战服务器用到的一些抽象类
 *
 * =============================================================================
 */

#ifndef  __SECT_BATTLE_SERVER_DEF_H__
#define  __SECT_BATTLE_SERVER_DEF_H__

#include <cstddef>
#include <set>
#include <map>
#include <vector>
#include <memory>
#include <tuple>
#include <alpha/mmap_file.h>
#include <alpha/skip_list.h>
#include <alpha/time_util.h>

namespace SectBattle {
    //错误码
    enum class Code : int16_t{
        kOk = 0,
        kOccupied = -1000,
        kNotInBattle = -1001,
        kInvalidDirection = -1002,
        kJoinedBattle = -1003,
        kInSameSect = -1004,
        kInvalidOpponent = -1005,
        kOpponentMoved = -1006,
        kNoOpponent = -1007,
        kAllGarrisonInProtection = -1008,
        kBattleFieldFull = -1009,
        kOpponentInProtection = -1010,
        kCannotMove = -1011,
        kCannotMoveToBornPos = -1012,
        kNoGarrisonInField = -1013,
    };
    //战场格子类型
    enum class FieldType {
        kDefault = 0, //普通位置
        kBornField = 1, //出生点
        //kExtraResourceField = 2, //额外资源点，对Server没用
    };
    //门派类型
    enum class SectType {
        kNone = 0, //默认类型
        kShaoLin = 1,
        kWuDang = 2,
        kKunLun = 3,
        kEMei = 4,
        kHuaShan = 5,
        kKongTong = 6,
        kMingJiao = 7,
        kGaiBang = 8,
        kMax = 9
    };

    enum class Direction {
        kUp = 1,
        kDown = 2,
        kLeft = 3,
        kRight = 4
    };

    //for std::get<*>(combatant_identity);
    enum {
        kCombatantLevel = 0,
        kCombatantDefeatedTimeStamp = 1,
        kCombatantUin = 2
    };
    
    using UinType = uint32_t;
    using LevelType = uint16_t;
    using OpponentList = std::vector<UinType>;
    using CombatantIdentity = std::tuple<LevelType, alpha::TimeStamp, UinType>;
    struct CompareCombatantIdentity {
        bool operator ()(const CombatantIdentity& lhs, const CombatantIdentity& rhs);
    };
    using GarrisonSet = std::set<CombatantIdentity, CompareCombatantIdentity>;
    using GarrisonIterator = GarrisonSet::iterator;

    bool IsValidSectType(int type);
    bool IsValidDirection(int d);
    //战场位置
    class Pos {
        public:
            static const int kMaxPos = 9;
            static Pos Create(int16_t x, int16_t y);
            static Pos CreateInvalid();
            int16_t X() const { return x_; }
            int16_t Y() const { return y_; }
            bool Valid() const;
            int64_t HashCode() const; //排序用
            std::pair<Pos, bool> Apply(Direction d) const;

        private:
            Pos() = default;
            friend class Sect;
            friend class Combatant;
            friend struct CombatantLite;
            int16_t x_;
            int16_t y_;
    };
    bool operator< (const Pos& lhs, const Pos& rhs);
    bool operator== (const Pos& lhs, const Pos& rhs);
    bool operator!= (const Pos& lhs, const Pos& rhs);
    std::ostream& operator<<(std::ostream& os, const Pos& pos);
    static_assert (std::is_pod<Pos>::value, "Pos must be POD type");

    //战场位置对应的格子
    class Field {
        public:
            Field(SectType owner, FieldType type);
            DISABLE_COPY_ASSIGNMENT(Field);
            GarrisonIterator AddGarrison(UinType uin, LevelType level,
                    alpha::TimeStamp last_defeated_time = 0);
            void ChangeOwner(SectType new_owner);
            void ReduceGarrison(UinType uin, GarrisonIterator it);
            GarrisonIterator UpdateGarrisonLevel(UinType uin, LevelType newlevel, GarrisonIterator it);
            OpponentList GetOpponents(LevelType level, alpha::TimeStamp defeated_before);
            SectType Owner() const;
            FieldType Type() const;
            uint32_t GarrisonNum() const;

        private:
            //从level这个等级最多取出needs个人
            //如果取出的人数恰好为needs, 返回true, 否则为false
            bool FindOpponentsInLevel(LevelType level, unsigned needs,
                    alpha::TimeStamp defeated_before, OpponentList*);
            SectType owner_;
            FieldType type_;
            GarrisonSet garrison_;
    };
    //门派
    class Sect {
        public:
            Sect(SectType type, Pos born_pos);
            DISABLE_COPY_ASSIGNMENT(Sect);
            void AddMember(UinType uin);
            void RemoveMember(UinType uin);
            uint32_t MemberCount() const;
            SectType Type() const;
            Pos BornPos() const;

        private:
            SectType type_;
            Pos born_pos_;
            std::set<UinType> members_;
    };

    //参战人员
    class Combatant {
        public:
            Combatant(const Sect* sect, Pos pos, GarrisonIterator iter);
            DISABLE_COPY_ASSIGNMENT(Combatant);
            void MoveTo(Pos pos);
            void SetIterator(GarrisonIterator iter);
            void ChangeSect(const Sect* new_sect);
            void ChangeOpponents(Direction d, const OpponentList& opponents);
            void ClearOpponents(Direction d);
            const Sect* CurrentSect() const;
            Pos CurrentPos() const;
            GarrisonIterator Iterator() const;
            OpponentList GetOpponents(Direction d) const;

        private:
            using OpponentMap = std::map<Direction, OpponentList>;
            const Sect* sect_;
            Pos pos_;
            GarrisonIterator iter_;
            OpponentMap opponents_;
    };

    struct CombatantLite {
        static CombatantLite Create(Pos p, LevelType l);
        Pos pos;
        LevelType level;
        alpha::TimeStamp last_defeated_time;
    };

    struct OpponentLite {
        static const int kMaxDirection = 4;
        static const int kMaxOpponentOneDirection = 5;
        static OpponentLite Default();
        void ChangeOpponents(Direction d, const OpponentList& opponents);
        OpponentList GetOpponents(Direction d);
        UinType opponents[kMaxDirection][kMaxOpponentOneDirection];

        private:
            OpponentLite() = default;
    };

    using OwnerMap = alpha::SkipList<Pos, SectType>;
    using CombatantMap = alpha::SkipList<UinType, CombatantLite>;
    using OpponentMap = alpha::SkipList<UinType, OpponentLite>;
    using MMapedFileMap = std::map<std::string, std::unique_ptr<alpha::MMapFile>>;
    extern const char* kBackupMetaDataKey;
    extern const char* kCombatantMapDataKey;
    extern const char* kOpponentMapDataKey;
    extern const char* kOwnerMapDataKey;
};

#endif   /* ----- #ifndef __SECT_BATTLE_SERVER_DEF_H__  ----- */
