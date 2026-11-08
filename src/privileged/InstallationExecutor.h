// SPDX-License-Identifier: BSD-3-Clause
// Copyright 2026 Nitrux

#pragma once

#include "../InstallationPlan.h"

#include <QString>
#include <functional>

struct InstallationProgressUpdate {
    int percent = 0;
    QString phase;
    QString detail;
};

class InstallationExecutor final
{
public:
    using ProgressCallback = std::function<void(const InstallationProgressUpdate&)>;

    static bool execute(const InstallationPlan& plan, const ProgressCallback& progress,
                        QString* error);
};
