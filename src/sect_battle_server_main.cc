/*
 * =============================================================================
 *
 *       Filename:  sect_battle_server_main.cc
 *        Created:  04/27/15 14:30:56
 *         Author:  Peng Wang
 *          Email:  pw2191195@gmail.com
 *    Description:  
 *
 * =============================================================================
 */

#include <gflags/gflags.h>
#include <alpha/logger.h>
#include <alpha/event_loop.h>
#include "sect_battle_server.h"

DEFINE_bool (daemon, false, "Run as daemon");

int main(int argc, char* argv[]) {
    const char* progname = argv[0];
    gflags::SetUsageMessage("Handle all SectBattle system request");
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    alpha::Logger::Init(progname);
    //if (FLAGS_daemon) {
    //}
    alpha::EventLoop loop;
    auto quit_loop = [&loop]{
        return loop.Quit();
    };
    auto ignore = []{};
    loop.TrapSignal(SIGINT, quit_loop);
    loop.TrapSignal(SIGQUIT, quit_loop);
    loop.TrapSignal(SIGTERM, quit_loop);
    loop.TrapSignal(SIGPIPE, ignore);
    SectBattle::Server s(&loop, "");
    if (s.Run()) {
        loop.Run();
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
