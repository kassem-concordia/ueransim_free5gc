//
// This file is a part of UERANSIM project.
// Copyright (c) 2023 ALİ GÜNGÖR.
//
// https://github.com/aligungr/UERANSIM/
// See README, LICENSE, and CONTRIBUTING files for licensing details.
//

#pragma once

#include <gnb/nts.hpp>
#include <gnb/types.hpp>
#include <mutex>

namespace nr::gnb
{

class GnbCmdHandler
{
  private:
    TaskBase *m_base;
    std::mutex m_cmdMutex;

  public:
    explicit GnbCmdHandler(TaskBase *base) : m_base(base)
    {
    }

    void handleCmd(NmGnbCliCommand &msg);

  private:
    void pauseTasks();
    void unpauseTasks();
    bool isAllPaused();

  private:
    void handleCmdImpl(NmGnbCliCommand &msg);

  private:
    void sendResult(const InetAddress &address, const std::string &output);
    void sendError(const InetAddress &address, const std::string &output);
};

} // namespace nr::gnb
