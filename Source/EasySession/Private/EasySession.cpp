// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySession.h"

DEFINE_LOG_CATEGORY(LogEasySession);

void FEasySessionModule::StartupModule()
{
	UE_LOG(LogEasySession, Log, TEXT("EasySession module started."));
}

void FEasySessionModule::ShutdownModule()
{
	UE_LOG(LogEasySession, Log, TEXT("EasySession module shut down."));
}

IMPLEMENT_MODULE(FEasySessionModule, EasySession)
