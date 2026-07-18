// Copyright Langerak. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

EASYSESSION_API DECLARE_LOG_CATEGORY_EXTERN(LogEasySession, Log, All);

/**
 * Runtime module for the EasySession plugin.
 */
class FEasySessionModule : public IModuleInterface
{
public:

	//~ Begin IModuleInterface Interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface Interface
};
