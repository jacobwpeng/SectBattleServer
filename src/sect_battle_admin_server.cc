/*
 * =============================================================================
 *
 *       Filename:  sect_battle_admin_server.cc
 *        Created:  05/27/15 18:55:28
 *         Author:  Peng Wang
 *          Email:  pw2191195@gmail.com
 *    Description:  把管理功能的HTTPServer逻辑从服务器主逻辑代码中分开
 *                  一点都不RESTful, 下次吧
 *
 * =============================================================================
 */

#include "sect_battle_server.h"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <alpha/logger.h>
#include <alpha/http_message.h>
#include <alpha/http_response_builder.h>
#include "sect_battle_inspector.h"
#include "sect_battle_backup_metadata.h"
#include "sect_battle_backup_coroutine.h"

namespace SectBattle {
    void Server::AdminServerCallback(alpha::TcpConnectionPtr conn,
                    const alpha::HTTPMessage& message) {
        auto path = message.Path();
        try {
            if (path == "/status") {
                WriteHTTPResponse(conn, 200, "OK", ServerStatus());
                return;
            } else if (path == "/field") {
                auto x = std::stoul(message.Params().at("x"));
                auto y = std::stoul(message.Params().at("y"));
                if (x <= Pos::kMaxPos && y <= Pos::kMaxPos) {
                    auto pos = Pos::Create(x, y);
                    WriteHTTPResponse(conn, 200, "OK", FieldStatus(pos));
                    return;
                }
            } else if (path == "/player") {
                UinType uin = std::stoul(message.Params().at("uin"));
                auto reply = PlayerStatus(uin);
                alpha::HTTPResponseBuilder builder(conn);
                builder.AddHeader("Server", "alpha::SimpleHTTPServer")
                    .AddHeader("Connection", "close")
                    .AddHeader("Date", alpha::HTTPMessage::FormatDate(alpha::Now()));
                if (reply.empty()) {
                    builder.status(404, "Not Found")
                    .AddHeader("Content-Type", "text/plain")
                    .body(alpha::Slice("Not Found\n"));
                } else {
                    builder.status(200, "OK")
                    .AddHeader("Content-Type", "application/json")
                    .body(std::move(reply));
                }
                builder.SendWithEOM();
                return;
            } else if (path == "/forcebackup") {
                ForceBackup();
                WriteHTTPResponse(conn, 200, "OK", "");
                return;
            } else if (path == "/sect") {
                unsigned sect = std::stoul(message.Params().at("type"));
                if (IsValidSectType(sect)) {
                    WriteHTTPResponse(conn, 200, "OK",
                            SectStatus(static_cast<SectType>(sect)));
                    return;
                }
            } else if (path == "/removeplayer") {
                UinType uin = std::stoul(message.Params().at("uin"));
                auto it = combatants_.find(uin);
                if (it == combatants_.end()) {
                    alpha::HTTPResponseBuilder(conn)
                        .status(404, "Not Found")
                        .AddHeader("Server", "alpha::SimpleHTTPServer")
                        .AddHeader("Date", alpha::HTTPMessage::FormatDate(alpha::Now()))
                        .AddHeader("Connection", "close")
                        .AddHeader("Content-Type", "text/plain")
                        .body(alpha::Slice("Not Found\n"))
                        .SendWithEOM();
                } else {
                    RemoveCombatant(uin);
                    alpha::HTTPResponseBuilder(conn)
                        .status(200, "OK")
                        .AddHeader("Server", "alpha::SimpleHTTPServer")
                        .AddHeader("Date", alpha::HTTPMessage::FormatDate(alpha::Now()))
                        .AddHeader("Connection", "close")
                        .AddHeader("Content-Type", "text/plain")
                        .body(alpha::Slice("Done\n"))
                        .SendWithEOM();
                }
                return;
            } else {
            }
        } catch(std::invalid_argument& e) {
        } catch(std::out_of_range& e) {
        }
        alpha::HTTPResponseBuilder(conn)
            .status(400, "Bad Request")
            .AddHeader("Server", "alpha::SimpleHTTPServer")
            .AddHeader("Date", alpha::HTTPMessage::FormatDate(alpha::Now()))
            .AddHeader("Connection", "close")
            .AddHeader("Content-Type", "application/json")
            .body(AdminServerUsage())
            .SendWithEOM();
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
        pt.put("ProcessStartTime",
                alpha::HTTPMessage::FormatDate(inspector_->ProcessStartTime()));
        pt.put("ProcessUpTime(ms)", alpha::Now() - inspector_->ProcessStartTime());
        pt.put("InfoLog", alpha::LogDestination::GetLogNum(alpha::kLogLevelInfo));
        pt.put("WarnLog", alpha::LogDestination::GetLogNum(alpha::kLogLevelWarning));
        pt.put("ErrorLog", alpha::LogDestination::GetLogNum(alpha::kLogLevelError));
        pt.put("BackupStatus", backup_coroutine_ && !backup_coroutine_->IsDead() ? 1 : 0);
        pt.put("BackupEndTime", alpha::HTTPMessage::FormatDate(backup_metadata_->EndTime()));
        pt.put("BackupPrefixIndex", current_backup_prefix_index_);
        pt.put("LastBattleFieldResetTime",
                alpha::HTTPMessage::FormatDate(
                    backup_metadata_->LatestBattleFieldResetTime()
                    * alpha::kMilliSecondsPerSecond));
        pt.put("CombatantsNum", combatants_.size());
        std::ostringstream oss;
        boost::property_tree::write_json(oss, pt);
        LOG_INFO << oss.str().size();
        return oss.str();
    }
#if 0
    void Server::AdminServerHandlePlayer(alpha::TcpConnectionPtr& conn,
            const alpha::HTTPMessage& message) {
        alpha::Slice path(message.Path());
        path.RemovePrefix("/player/");
        UinType uin = std::stoul(path.ToString());
        if (message.Method() == "GET") {
            AdminServerGetPlayer(conn, uin);
        } else if (message.Method() == "DELETE") {
            AdminServerDeletePlayer(conn, uin);
        } else {
            AdminServerMethodNotAllowed(conn, "GET, DELETE");
        }
        //AdminServerBadRequest(conn, "Bad Request", "Invalid path.");
    }

    void Server::AdminServerGetPlayer(alpha::TcpConnectionPtr& conn, UinType uin) {
        auto it = combatants_.find(uin);
        if (it == combatants_.end()) {
            //WriteHTTPResponse("404", "Not
        }
    }

    void Server::AdminServerHandleField(alpha::TcpConnectionPtr& conn,
            const alpha::HTTPMessage& message) {
        alpha::Slice path(message.Path());
        path.RemovePrefix("/field/");
        int16_t x, y;
        auto n = sscanf("%" SCNd16 "/%" SCNd16, &x, &y);
        if (n != 2) {
            AdminServerBadRequest(conn, "Invalid path.");
            return;
        }

        if (x < 0 || x > Pos::kMaxPos || y < 0 || y > Pos::kMaxPos) {
            AdminServerBadRequest(conn, "Invalid position.");
            return;
        }

        if (message.Method() == "GET") {
            AdminServerGetField(conn, Pos::Create(x, y));
        } else {
            AdminServerMethodNotAllowed(conn, "GET");
        }
    }

    void Server::AdminServerHandleSect(alpha::TcpConnectionPtr& conn,
            const alpha::HTTPMessage& message) {
        alpha::Slice path(message.Path());
        path.RemovePrefix("/sect/");
        std::vector<std::string> sect_names = {
            "shaolin",
            "wudang",
            "kunlun",
            "emei",
            "huashan",
            "kongtong",
            "mingjiao",
            "gaibang"
        };
        auto it = std::find(sect_names.begin(), sect_names.end(), path.ToString());
        if (it == sect_names.end()) {
            AdminServerBadRequest(conn, "Invalid sect");
            return;
        }
        SectType sect_type = static_cast<SectType>(
                std::distance(sect_names.begin(), it) + 1);
        if (message.Method() == "GET") {
            AdminServerGetSect(conn, sect_type);
        } else {
            AdminServerMethodNotAllowed(conn, "GET");
        }
    }

    void Server::AdminServerHandleBackup(alpha::TcpConnectionPtr& conn,
            const alpha::HTTPMessage& message) {
        if (method.Method() == "GET") {
            boost::property_tree::ptree pt;
            pt.put("BackupStatus",
                    backup_coroutine_ && !backup_coroutine_->IsDead() ? 1 : 0);
            pt.put("BackupEndTime",
                    alpha::HTTPMessage::FormatDate(backup_metadata_->EndTime()));
            pt.put("BackupPrefixIndex", current_backup_prefix_index_);
            std::ostringstream oss;
            boost::property_tree::write_json(oss, pt);
            WriteHTTPResponse(conn, 200, "OK", oss.str());
        }
        else if (method.Method() == "POST") {
            BackupRoutine(true);
            WriteHTTPResponse(conn, "202", "Accepted", "");
        } else {
            AdminServerMethodNotAllowed(conn, "GET, POST");
        }
    }
#endif

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

    std::string Server::SectStatus(SectType sect_type) {
        auto & sect = CheckGetSect(sect_type);
        boost::property_tree::ptree pt;
        pt.put("Sect", sect.Type());
        pt.put("MembersCount", sect.MemberCount());
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
            "GET /player?uin=$UIN",
            "GET /removeplayer?uin=$UIN",
            "GET /status",
            "GET /field?x=$X&y=$Y",
            "GET /sect?type=$TYPE",
            "GET /forcebackup",
        };
#if 0
        std::vector<std::string> usages = {
            "GET /player/uin",
            "DELETE /player/uin",
            "GET /status",
            "GET /field/x/y",
            "GET /sect/shaolin",
            "POST /forcebackup",
        };
#endif
        for (const auto& msg : usages) {
            boost::property_tree::ptree array_element;
            array_element.put_value(msg);
            usage.push_back(std::make_pair("", array_element));
        }
        pt.add_child("usage", usage);

        boost::property_tree::write_json(oss, pt);
        return oss.str();
    }

    void Server::ForceBackup() {
        BackupRoutine(true);
    }

    void Server::RemoveCombatant(UinType uin) {
        auto & combatant = CheckGetCombatant(uin);
        auto & field = CheckGetField(combatant.CurrentPos());
        field.ReduceGarrison(uin, combatant.Iterator());
        auto & sect = CheckGetSect(combatant.CurrentSect()->Type());
        sect.RemoveMember(uin);
        combatants_.erase(uin);
        combatant_map_->erase(uin);
        opponent_map_->erase(uin);
    }

    void Server::WriteHTTPResponse(alpha::TcpConnectionPtr& conn,
            int status,
            alpha::Slice status_string,
            alpha::Slice body) {

        alpha::HTTPResponseBuilder builder(conn);
        builder.status(status, status_string)
            .AddHeader("Server", "alpha::SimpleHTTPServer")
            .AddHeader("Connection", "close")
            .AddHeader("Content-Type", "application/json")
            .AddHeader("Date", alpha::HTTPMessage::FormatDate(alpha::Now()))
            .body(body)
            .SendWithEOM();
    }
}
