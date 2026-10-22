// Copyright (c) 2026 Langerak.

#pragma once

#include "CoreMinimal.h"
#include "EasySessionWidget.h"
#include "EasySessionTestWidget.generated.h"

/** A UEasySessionWidget the tests can create, because the base class is abstract. */
UCLASS()
class UEasySessionTestWidget : public UEasySessionWidget
{
	GENERATED_BODY()
};
