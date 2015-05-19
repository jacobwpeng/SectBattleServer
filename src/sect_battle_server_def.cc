/*
 * =============================================================================
 *
 *       Filename:  sect_battle_server_def.cc
 *        Created:  04/27/15 16:17:32
 *         Author:  Peng Wang
 *          Email:  pw2191195@gmail.com
 *    Description:  
 *
 * =============================================================================
 */

#include "sect_battle_server_def.h"
#include <alpha/random.h>
#include <algorithm>

namespace {
    template<typename T>
    int Compare(T lhs, T rhs) {
        if (lhs < rhs) return -1;
        else if (lhs > rhs) return 1;
        else return 0;
    }
}

namespace SectBattle {
    const char* kBackupMetaDataKey = "backup_metadata";
    const char* kCombatantMapDataKey = "combatant_map";
    const char* kOpponentMapDataKey = "opponent_map";
    const char* kOwnerMapDataKey = "owner_map";

    bool IsValidSectType(int type) {
        return type > static_cast<int>(SectType::kNone)
            && type < static_cast<int>(SectType::kMax);
    }

    bool IsValidDirection(int d) {
        return d >= static_cast<int>(Direction::kUp) 
            && d <= static_cast<int>(Direction::kRight);
    }

    bool operator < (const CombatantIdentity& lhs, const CombatantIdentity& rhs) {
        //1.Level小的在前
        //2.TimeStamp大的在前
        //3.Uin小的在前
        int res = Compare(std::get<kCombatantLevel>(lhs), std::get<kCombatantLevel>(rhs));
        if (res != 0) {
            return res < 0 ? true : false;
        }

        res = Compare(std::get<kCombatantDefeatedTimeStamp>(lhs),
                std::get<kCombatantDefeatedTimeStamp>(rhs));
        if (res != 0) {
            return res > 0 ? true : false;
        }

        res = Compare(std::get<kCombatantUin>(lhs), std::get<kCombatantUin>(rhs));
        if (res != 0) {
            return res < 0 ? true : false;
        }
        return false;
    }

    Pos Pos::Create(int16_t x, int16_t y) {
        Pos pos;
        pos.x_ = x;
        pos.y_ = y;
        assert(pos.Valid());
        return pos;
    }

    Pos Pos::CreateInvalid() {
        Pos pos;
        pos.x_ = -1;
        pos.y_ = -1;
        return pos;
    }

    std::pair<Pos, bool> Pos::Apply(Direction d) const {
        if (d == Direction::kUp && y_ == 0) {
            return std::make_pair(*this, false);
        }
        if (d == Direction::kDonw && y_ == kMaxPos) {
            return std::make_pair(*this, false);
        }
        if (d == Direction::kLeft && x_ == 0) {
            return std::make_pair(*this, false);
        }
        if (d == Direction::kRight && x_ == kMaxPos) {
            return std::make_pair(*this, false);
        }

        int x = x_;
        int y = y_;

        switch (d) {
            case Direction::kUp:
                y -= 1;
                break;
            case Direction::kDonw:
                y += 1;
                break;
            case Direction::kLeft:
                x -= 1;
                break;
            case Direction::kRight:
                x += 1;
                break;
        }

        assert (Valid());
        return std::make_pair(Pos::Create(x, y), true);
    }

    bool Pos::Valid() const {
        return x_ >= 0 && x_ <= kMaxPos
            && y_ >= 0 && y_ <= kMaxPos;
    }

    bool operator< (const Pos& lhs, const Pos& rhs) {
        assert (lhs.Valid());
        assert (rhs.Valid());
        const uint16_t kYCoordinateWeight = 10;
        return lhs.X() + lhs.Y() * kYCoordinateWeight 
            < rhs.X() + rhs.Y() * kYCoordinateWeight;
    }

    bool operator== (const Pos& lhs, const Pos& rhs) {
        return lhs.X() == rhs.X() && lhs.Y() == rhs.Y();
    }

    bool operator!= (const Pos& lhs, const Pos& rhs) {
        return !(lhs == rhs);
    }

    Field::Field(SectType owner, FieldType type)
        :owner_(owner), type_(type) {
    }

    GarrisonIterator Field::AddGarrison(UinType uin, LevelType level,
            alpha::TimeStamp last_defeated_time) {
        auto res = garrison_.insert(CombatantIdentity(level, last_defeated_time, uin));
        assert (res.second);
        return res.first;
    }

    void Field::ChangeOwner(SectType new_owner) {
        owner_ = new_owner;
    }

    void Field::ReduceGarrison(UinType uin, GarrisonIterator it) {
        assert (it != garrison_.end());
        assert (it->second == uin);
        assert (garrison_.find(*it) != it);
        (void)uin;
        garrison_.erase(it);
    }

    void Field::UpdateGarrisonLevel(UinType uin, LevelType newlevel,
            GarrisonIterator it) {
        assert (it != garrison_.end());
        assert (garrison_.find(*it) == it);
        auto last_defeated_time = std::get<kCombatantDefeatedTimeStamp>(*it);
        garrison_.erase(it);
        auto res = garrison_.insert(CombatantIdentity(newlevel, last_defeated_time, uin));
        assert (res.second);
        (void)res;
    }

    OpponentList Field::GetOpponents(LevelType level, alpha::TimeStamp defeated_before) {
        OpponentList opponents;
        const auto kMaxOpponents = 5u;
        auto last = garrison_.end();
        if (last == garrison_.begin()) {
            //没有驻军
            return opponents;
        }

        //先找同一等级段的
        if (FindOpponentsInLevel(level, kMaxOpponents, defeated_before, &opponents)) {
            return opponents;
        }

        --last;
        const auto min_searching_level = std::get<kCombatantLevel>(*garrison_.begin());
        const auto max_searching_level = std::get<kCombatantLevel>(*last);

        //同一等级段不足，则在上下等级段查找
        int current_searching_level_offset = 1;
        while (opponents.size() < kMaxOpponents) {
            auto current_search_level = static_cast<int>(level)
                - current_searching_level_offset;

            if (current_search_level >= min_searching_level) {
                auto needs = kMaxOpponents - opponents.size();
                if (FindOpponentsInLevel(current_search_level, needs, defeated_before,
                            &opponents)) {
                    break;
                }
            }

            current_search_level = static_cast<int>(level) + current_searching_level_offset;
            if (current_search_level <= max_searching_level) {
                auto needs = kMaxOpponents - opponents.size();
                if (FindOpponentsInLevel(current_search_level, needs, defeated_before,
                            &opponents)) {
                    break;
                }
            }

            ++current_searching_level_offset;
        }
        return opponents;
    }

    SectType Field::Owner() const {
        return owner_;
    }

    FieldType Field::Type() const {
        return type_;
    }

    uint32_t Field::GarrisonNum() const {
        return garrison_.size();
    }

    bool Field::FindOpponentsInLevel(LevelType level, unsigned needs,
            alpha::TimeStamp defeated_before, OpponentList* opponents) {
        assert (opponents);
        CombatantIdentity lower(level, defeated_before,
                std::numeric_limits<UinType>::min());
        CombatantIdentity upper(level, std::numeric_limits<alpha::TimeStamp>::min(),
                std::numeric_limits<UinType>::max());
        auto it = garrison_.lower_bound(lower);
        auto last = garrison_.upper_bound(upper);

        //这个等级段没人
        if (it == last) {
            return false;
        }
        auto first = it;
        auto iterators = alpha::Random::Sample(first, last, needs);
        std::for_each(iterators.begin(), iterators.end(), 
                [opponents](decltype(it) it) {
            opponents->push_back(std::get<kCombatantUin>(*it));
        });

        return iterators.size() == needs;
    }

    Sect::Sect(SectType type, Pos born_pos)
        :type_(type), born_pos_(born_pos) {
        assert (type != SectType::kNone);
    }

    void Sect::AddMember(UinType uin) {
        members_.insert(uin);
    }

    void Sect::RemoveMember(UinType uin) {
        assert (members_.find(uin) != members_.end());
        members_.erase(uin);
    }

    uint32_t Sect::MemberCount() const {
        return members_.size();
    }

    SectType Sect::Type() const {
        return type_;
    }

    Pos Sect::BornPos() const {
        return born_pos_;
    }

    Combatant::Combatant(const Sect* sect, Pos pos, GarrisonIterator iter)
        :sect_(sect), pos_(pos), iter_(iter) {
        assert (sect);
        assert (sect->Type() != SectType::kNone);
    }

    void Combatant::MoveTo(Pos pos) {
        pos_ = pos;
        opponents_.clear();
    }

    void Combatant::SetIterator(GarrisonIterator iter) {
        iter_ = iter;
    }

    void Combatant::ChangeSect(const Sect* new_sect) {
        assert (new_sect);
        assert (new_sect->Type() != SectType::kNone);
        sect_ = new_sect;
    }

    void Combatant::ChangeOpponents(Direction d, const OpponentList& opponents) {
        opponents_[d] = opponents;
    }

    void Combatant::ClearOpponents(Direction d) {
        opponents_.erase(d);
    }

    const Sect* Combatant::CurrentSect() const {
        return sect_;
    }

    Pos Combatant::CurrentPos() const {
        return pos_;
    }

    GarrisonIterator Combatant::Iterator() const {
        return iter_;
    }

    OpponentList Combatant::GetOpponents(Direction d) const {
        auto it = opponents_.find(d);
        if (it == opponents_.end()) {
            return OpponentList();
        } else {
            return it->second;
        }
    }

    CombatantLite CombatantLite::Create(Pos p, LevelType l) {
        CombatantLite lite;
        lite.pos = p;
        lite.level = l;
        lite.last_defeated_time = 0;
        return lite;
    }

    OpponentLite OpponentLite::Default() {
        OpponentLite lite;
        ::memset(&lite, 0x0, sizeof(OpponentLite));
        return lite;
    }

    void OpponentLite::ChangeOpponents(Direction direction, const OpponentList& opponents) {
        auto d = static_cast<int>(direction);
        assert (d >= 0 && d <= kMaxDirection);
        assert (opponents.size() <= kMaxOpponentOneDirection);
        size_t i = 0;
        while (i < opponents.size()) {
            this->opponents[d][i] = opponents[i];
            ++i;
        }

        while (i < kMaxOpponentOneDirection) {
            this->opponents[d][i] = 0;
            ++i;
        }
    }

    OpponentList OpponentLite::GetOpponents(Direction d) {
        auto direction = static_cast<int>(d);
        assert (direction >= 0 && direction <= kMaxDirection);
        OpponentList res;
        std::copy_if(std::begin(opponents[direction]), std::end(opponents[direction]),
                std::back_inserter(res), [](UinType opponent_uin) {
            return opponent_uin != 0;
        });
        return res;
    }
}
