// Copyright Langerak. All Rights Reserved.

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
