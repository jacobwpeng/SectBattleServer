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
                if (field_type != "born") continue; //只读取出生点
                const int sect = field.get<int>("<xmlattr>.sect");
                const int x = field.get<int>("<xmlattr>.x");
                const int y = field.get<int>("<xmlattr>.y");
                auto pos = Pos::Create(x, y);
                assert (pos.Valid());
                assert (IsValidSectType(sect));
                auto sect_type = static_cast<SectType>(sect);
                auto res = conf->born_positions_.emplace(sect_type, pos);
                assert (res.second);
                (void)res;
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

    Pos ServerConf::GetBornPos(SectType sect_type) const {
        auto it = born_positions_.find(sect_type);
        assert (it != born_positions_.end());
        return it->second;
    }

    bool ServerConf::InSameSeason(time_t lhs, time_t rhs) const {
        auto one = boost::posix_time::from_time_t(lhs);
        one += boost::posix_time::hours(8);
        auto another = boost::posix_time::from_time_t(rhs);
        another += boost::posix_time::hours(8);

        //周三六点切换，所以过去倒退3天6小时，以周日0点来判断是否为同一个星期
        one -= boost::posix_time::hours(3 * 24 + 6);
        another -= boost::posix_time::hours(3 * 24 + 6);

        DLOG_INFO << "one.date().week_number() = " << one.date().week_number()
            << ", another.date().week_number() = " << another.date().week_number();

        return one.date().week_number() == another.date().week_number();
    }
}
