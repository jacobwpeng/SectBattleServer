/*
 * =============================================================================
 *
 *       Filename:  sect_battle_admin_server.cc
 *        Created:  05/27/15 18:55:28
 *         Author:  Peng Wang
 *          Email:  pw2191195@gmail.com
 *    Description:  把管理功能的HTTPServer逻辑从服务器主逻辑代码中分开
 *
 * =============================================================================
 */

#include "sect_battle_server.h"
#include <alpha/logger.h>
#include <alpha/http_message.h>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include "sect_battle_inspector.h"

namespace SectBattle {
    void Server::AdminServerCallback(alpha::TcpConnectionPtr conn,
                    const alpha::HTTPMessage& message) {
        auto path = message.Path();
        try {
            if (path == "/status") {
                conn->Write(ServerStatus());
                return;
            } else if (path == "/field") {
                auto x = std::stoul(message.Params().at("x"));
                auto y = std::stoul(message.Params().at("y"));
                if (x <= Pos::kMaxPos && y <= Pos::kMaxPos) {
                    auto pos = Pos::Create(x, y);
                    conn->Write(FieldStatus(pos));
                } else {
                    conn->Write(AdminServerUsage());
                }
            } else if (path == "/player") {
                UinType uin = std::stoul(message.Params().at("uin"));
                conn->Write(PlayerStatus(uin));
            } else {
                conn->Write(AdminServerUsage());
            }
        } catch(std::invalid_argument& e) {
            conn->Write(AdminServerUsage());
        } catch(std::out_of_range& e) {
            conn->Write(AdminServerUsage());
        }
    }

    std::string Server::ServerStatus() {
        boost::property_tree::ptree pt;
        pt.put("RequestPerSecond", inspector_->RequestProcessedPerSeconds());
        std::vector<int> seconds = {60, 300, 1500};
        boost::property_tree::ptree requests;
        for (auto second : seconds) {
            char buf[20];
            ::snprintf(buf, sizeof(buf), "%ds", second);
            boost::property_tree::ptree array_element;
            array_element.put(buf, inspector_->SampleRequests(second));
            requests.push_back(std::make_pair("", array_element));
        }
        pt.add_child("Requests", requests);
        requests.clear();
        for (auto second : seconds) {
            char buf[20];
            ::snprintf(buf, sizeof(buf), "%ds", second);
            boost::property_tree::ptree array_element;
            array_element.put(buf, inspector_->SampleSucceedRequests(second));
            requests.push_back(std::make_pair("", array_element));
        }
        pt.add_child("SucceedRequests", requests);
        pt.put("AverageProcessTime", inspector_->AverageProcessTime());
        pt.put("ProcessStartTime", inspector_->ProcessStartTime());
        pt.put("ProcessUpTime", alpha::Now() - inspector_->ProcessStartTime());
        pt.put("InfoLog", alpha::LogDestination::GetLogNum(alpha::kLogLevelInfo));
        pt.put("WarnLog", alpha::LogDestination::GetLogNum(alpha::kLogLevelWarning));
        pt.put("ErrorLog", alpha::LogDestination::GetLogNum(alpha::kLogLevelError));
        std::ostringstream oss;
        boost::property_tree::write_json(oss, pt);
        LOG_INFO << oss.str().size();
        return oss.str();
    }

    std::string Server::FieldStatus(Pos pos) {
        auto & field = CheckGetField(pos);
        // owner, garrison num
        boost::property_tree::ptree pt;
        pt.put("owner", field.Owner());
        pt.put("garrison_num", field.GarrisonNum());
        std::ostringstream oss;
        boost::property_tree::write_json(oss, pt);
        return oss.str();
    }

    std::string Server::PlayerStatus(UinType uin) {
        auto it = combatants_.find(uin);
        if (it == combatants_.end()) {
            return "";
        }

        auto & combatant = it->second;
        boost::property_tree::ptree pt;
        pt.put("Sect", combatant.CurrentSect()->Type());
        pt.put("Pos-X", combatant.CurrentPos().X());
        pt.put("Pos-Y", combatant.CurrentPos().Y());
        auto & identity = *combatant.Iterator();
        pt.put("Level", std::get<kCombatantLevel>(identity));
        pt.put("LastDefeatedTime", std::get<kCombatantDefeatedTimeStamp>(identity));
        pt.put("Uin", std::get<kCombatantUin>(identity));
        std::ostringstream oss;
        boost::property_tree::write_json(oss, pt);
        return oss.str();
    }

    std::string Server::AdminServerUsage() const {
        std::ostringstream oss;
        boost::property_tree::ptree pt;
        boost::property_tree::ptree usage;
        std::vector<std::string> usages = {
            "curl host:port/path",
            "get player info, path = player?uin=2191195",
            "get server status, path = status",
            "get field info, path = field?x=1&y=1",
            "usage, path = [default]"
        };
        for (const auto& msg : usages) {
            boost::property_tree::ptree array_element;
            array_element.put_value(msg);
            usage.push_back(std::make_pair("", array_element));
        }
        pt.add_child("usage", usage);

        boost::property_tree::write_json(oss, pt);
        return oss.str();
    }
}
