// Copyright (c) 2026 Langerak.

#include "EasySessionGameInstance.h"

#include "EasySessionOnlineSession.h"

TSubclassOf<UOnlineSession> UEasySessionGameInstance::GetOnlineSessionClass()
{
	return UEasySessionOnlineSession::StaticClass();
}
