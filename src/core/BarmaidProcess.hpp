#pragma once

#include <string>
#include <vector>

#include <sys/types.h>

class CBarmaidProcess {
  public:
    static pid_t launch(const std::string& app, const std::vector<std::string>& params);
    static bool  isRunning(pid_t pid);
    static void  terminate(pid_t pid);
};
