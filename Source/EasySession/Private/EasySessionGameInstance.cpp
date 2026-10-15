// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#include "EasySessionGameInstance.h"

#include "EasySessionOnlineSession.h"

TSubclassOf<UOnlineSession> UEasySessionGameInstance::GetOnlineSessionClass()
{
	return UEasySessionOnlineSession::StaticClass();
}
