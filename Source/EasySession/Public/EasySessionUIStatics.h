// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EasySessionTypes.h"
#include "EasySessionUIStatics.generated.h"

/**
 * Blueprint function library for the text a session UI shows.
 * Every function here is pure and touches no session state, so a menu built from them needs no string assembly of its own.
 * The example widgets use these, and a game UI can use them the same way.
 */
UCLASS()
class EASYSESSION_API UEasySessionUIStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** @return A short, player facing sentence for a result, e.g. "The session is full". Success reads "Done". */
	UFUNCTION(BlueprintPure, Category = "EasySession|UI")
	static FText GetResultMessage(EEasySessionResult Result);

	/** @return A status line for a running operation, e.g. "Creating the session...". None gives empty text, so a status line clears itself with it. */
	UFUNCTION(BlueprintPure, Category = "EasySession|UI")
	static FText GetActivityMessage(EEasySessionActivity Activity);

	/**
	 * @return A status line for a matchmaking run, e.g. "Searching... 12s". Idle reads "Ready".
	 * Feed it the values On Matchmaking Updated hands out, which fires once a second while a run is active.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession|UI")
	static FText FormatMatchmakingStatus(EEasyMatchmakingState State, int32 ElapsedSeconds);

	/** @return The occupancy line of a search result, e.g. "1/4   ping 32ms". Players are Max Players minus Open Slots. */
	UFUNCTION(BlueprintPure, Category = "EasySession|UI")
	static FText FormatSessionSlots(const FEasySessionSearchResult& Result);

	/** @return The region's display name, e.g. "North America East". */
	UFUNCTION(BlueprintPure, Category = "EasySession|UI")
	static FText GetRegionDisplayName(EEasySessionRegion Region);

	/** @return Every region's display name in enum order, ready for a combo box. Region From Index maps the selection back. */
	UFUNCTION(BlueprintPure, Category = "EasySession|UI")
	static TArray<FText> GetRegionOptions();

	/** @return The region at a combo box index from Get Region Options. Out of range indices give Any. */
	UFUNCTION(BlueprintPure, Category = "EasySession|UI")
	static EEasySessionRegion RegionFromIndex(int32 Index);
};
