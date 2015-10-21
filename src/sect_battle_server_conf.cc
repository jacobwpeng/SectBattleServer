/*
 * =============================================================================
 *
 *       Filename:  sect_battle_server_conf.cc
 *        Created:  05/14/15 15:34:22
 *         Author:  Peng Wang
 *          Email:  pw2191195@gmail.com
 *    Description:  
 *
 * =============================================================================
 */

#include "sect_battle_server_conf.h"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/date_time.hpp>
#include <alpha/logger.h>

namespace SectBattle {
    std::unique_ptr<ServerConf> ServerConf::ReadFromFile(alpha::Slice file) {
        using namespace boost::property_tree;

        ptree pt;
        try {
            std::unique_ptr<ServerConf> conf(new ServerConf);
            boost::property_tree::read_xml(file.data(), pt,
                    xml_parser::no_comments);
            for(const auto & child : pt.get_child("cgi_conf.Field")) {
                if (child.first != "field") continue;
                auto & field = child.second;
                const std::string field_type = field.get<std::string>("<xmlattr>.type"); 
                bool is_born_pos = field_type == "born";
                bool is_off_limits_pos = field_type == "forbidden";
                if (!is_born_pos && !is_off_limits_pos) {
                    continue; //只读取出生点和禁入点
                }
                const int x = field.get<int>("<xmlattr>.x");
                const int y = field.get<int>("<xmlattr>.y");
                auto pos = Pos::Create(x, y);
                assert (pos.Valid());
                if (is_born_pos) {
                    const int sect = field.get<int>("<xmlattr>.sect");
                    assert (IsValidSectType(sect));
                    auto sect_type = static_cast<SectType>(sect);
                    auto res = conf->born_positions_.emplace(sect_type, pos);
                    assert (res.second);
                    (void)res;
                } else {
                    auto res = conf->off_limits_area_.emplace(pos);
                    assert (res.second);
                    (void)res;
                }
            }
            //检查一下完整性
            for (int sect = static_cast<int>(SectType::kNone) + 1;
                    sect != static_cast<int>(SectType::kMax);
                    ++sect ) {
                auto pos = conf->GetBornPos(static_cast<SectType>(sect));
                assert (pos.Valid());
                (void)pos;
            }

            return std::move(conf);
        } catch (ptree_error & e) {
            LOG_ERROR << "ReadFromFile failed, file = " << file.data()
                << ", " << e.what();
            return nullptr;
        }
    }

    bool ServerConf::IsOffLimitsArea(Pos pos) const {
        return off_limits_area_.find(pos) != off_limits_area_.end();
    }

    Pos ServerConf::GetBornPos(SectType sect_type) const {
        auto it = born_positions_.find(sect_type);
        assert (it != born_positions_.end());
        return it->second;
    }

    bool ServerConf::InSameSeason(time_t lhs, time_t rhs) const {
        static const int kOneHourSeconds = 3600;
        static const int kOneDaySeconds = 24 * kOneHourSeconds;
        static const int kOneWeekSeconds = 7 * kOneDaySeconds;
        // CST时区下time_t == 0 为周四早8点
        // 所以倒退26个小时作为切换时间点
        static const int kSameSeasonOffset = 26 * kOneHourSeconds;
        return (lhs + kSameSeasonOffset) / kOneWeekSeconds
            == (rhs + kSameSeasonOffset) / kOneWeekSeconds;
    }

    int ServerConf::DefeatedProtectionDuration() const {
        return 30 * 1000; //30s in milliseconds
    }
}
