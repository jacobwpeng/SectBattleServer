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

#include <unistd.h>
#include <gflags/gflags.h>
#include <alpha/logger.h>
#include <alpha/event_loop.h>
#include <alpha/process_lock_file.h>
#include "sect_battle_server.h"

DEFINE_bool (daemon, false, "Run as daemon");
DEFINE_string (lock_file, "sect_battle_svrd.lock", "lock file path of server");

int main(int argc, char* argv[]) {
    const char* progname = argv[0];
    gflags::SetUsageMessage("Handle all SectBattle system request");
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    alpha::Logger::Init(progname);
    alpha::EventLoop loop;
    if (FLAGS_daemon && daemon(0, 0) == -1) {
        PLOG_ERROR << "daemon";
        return EXIT_FAILURE;
    }
    alpha::ProcessLockFile lock_file(FLAGS_lock_file);
    auto quit_loop = [&loop]{
        return loop.Quit();
    };
    auto ignore = []{};
    loop.TrapSignal(SIGINT, quit_loop);
    loop.TrapSignal(SIGQUIT, quit_loop);
    loop.TrapSignal(SIGTERM, quit_loop);
    loop.TrapSignal(SIGPIPE, ignore);
    SectBattle::Server s(&loop);
    if (s.Run()) {
        loop.Run();
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
