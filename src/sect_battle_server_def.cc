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
#include <algorithm>

namespace SectBattle {
    bool IsValidSectType(int type) {
        return type > static_cast<int>(SectType::kNone)
            && type < static_cast<int>(SectType::kMax);
    }

    bool IsValidDirection(int d) {
        return d >= static_cast<int>(Direction::kUp) 
            && d <= static_cast<int>(Direction::kRight);
    }

    Pos Pos::Create(uint16_t x, uint16_t y) {
        assert (x <= kMaxPos);
        assert (y <= kMaxPos);

        Pos pos;
        pos.x_ = x;
        pos.y_ = y;
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

        assert (x <= kMaxPos);
        assert (y <= kMaxPos);
        return std::make_pair(Pos::Create(x, y), true);
    }

    bool operator< (const Pos& lhs, const Pos& rhs) {
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

    GarrisonIterator Field::AddGarrison(UinType uin, LevelType level) {
        return garrison_.insert(std::make_pair(level, uin)).first;
    }

    void Field::ChangeOwner(SectType new_owner) {
        owner_ = new_owner;
    }

    void Field::ReduceGarrison(UinType uin, GarrisonIterator iter) {
        assert (iter != garrison_.end());
        assert (iter->second == uin);
        garrison_.erase(iter);
    }

    OpponentList Field::GetOpponents(LevelType level) {
        (void)level;
        return OpponentList();
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
}
