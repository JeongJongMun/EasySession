// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EasySessionTypes.h"
#include "EasySessionStatics.generated.h"

class UEasyMatchmakingPolicy;
class UEasySessionSubsystem;

/**
 * Blueprint function library for reading EasySession state and for the calls that answer inside the call.
 * A call that changes the session is an async node instead, such as Create Easy Session or Join Easy Session.
 *
 * Every function here is about the game session, the one players find, join and play in, except the Party ones.
 * There is one of each per process, so none of them take a session argument.
 */
UCLASS()
class EASYSESSION_API UEasySessionStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** The EasySession subsystem, for binding session events like On Session Failure. */
	UFUNCTION(BlueprintPure, Category = "EasySession|Session", meta = (WorldContext = "WorldContextObject"))
	static UEasySessionSubsystem* GetEasySessionSubsystem(const UObject* WorldContextObject);

	/** Whether the local player is in a session. */
	UFUNCTION(BlueprintPure, Category = "EasySession|Session", meta = (WorldContext = "WorldContextObject"))
	static bool IsInEasySession(const UObject* WorldContextObject);

	/**
	 * Whether the local player is hosting the current session.
	 * Always false on a dedicated server, which has no local player.
	 * Use Is Easy Session Authority there instead.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession|Session", meta = (WorldContext = "WorldContextObject"))
	static bool IsEasySessionHost(const UObject* WorldContextObject);

	/**
	 * Whether this game created the session it is in, so it may Start, End, Update, travel or destroy it.
	 * Is Easy Session Host is a different question, and false on a dedicated server.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession|Session", meta = (WorldContext = "WorldContextObject"))
	static bool IsEasySessionAuthority(const UObject* WorldContextObject);

	/**
	 * The lifecycle state of the current session (Pending, InProgress, Ended, ...). The host reports its own state.
	 * A client reports the host's replicated state once it has arrived, and its own until then.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession|Session", meta = (WorldContext = "WorldContextObject"))
	static EEasySessionState GetEasySessionState(const UObject* WorldContextObject);

	/**
	 * The password this game's session was created with, for the host to share.
	 * Empty on clients and for sessions without a password, because the password never leaves the host.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession|Session", meta = (WorldContext = "WorldContextObject"))
	static FString GetEasySessionPassword(const UObject* WorldContextObject);

	/** Whether a matchmaking run is running. */
	UFUNCTION(BlueprintPure, Category = "EasySession|Matchmaking", meta = (WorldContext = "WorldContextObject"))
	static bool IsEasyMatchmakingRunning(const UObject* WorldContextObject);

	/** The state of the running matchmaking: Searching, Joining, Hosting, Canceling or Complete. Idle when none is running. */
	UFUNCTION(BlueprintPure, Category = "EasySession|Matchmaking", meta = (WorldContext = "WorldContextObject"))
	static EEasyMatchmakingState GetEasyMatchmakingState(const UObject* WorldContextObject);

	/**
	 * The policy of the running matchmaking, or null when none is running.
	 * Progress is broadcast on the On Matchmaking events of the subsystem, not on the policy.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession|Matchmaking", meta = (WorldContext = "WorldContextObject"))
	static UEasyMatchmakingPolicy* GetActiveEasyMatchmakingPolicy(const UObject* WorldContextObject);

	/**
	 * Whether a request is running or queued, a matchmaking run is running, or a travel this plugin started has not loaded its map yet.
	 * Session buttons read this to disable themselves.
	 * Is Easy Matchmaking Running asks about matchmaking alone.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession|Session", meta = (WorldContext = "WorldContextObject"))
	static bool IsEasySessionBusy(const UObject* WorldContextObject);

	/**
	 * What keeps Is Easy Session Busy true: Creating, Joining, Traveling and so on.
	 * None exactly when it is false.
	 * It covers requests the game did not start too, such as the destroy after a lost connection.
	 * Get Activity Message turns it into a status line.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession|Session", meta = (WorldContext = "WorldContextObject"))
	static EEasySessionActivity GetEasySessionActivity(const UObject* WorldContextObject);

	/** The display name of the current session. Empty when no session exists. */
	UFUNCTION(BlueprintPure, Category = "EasySession|Session", meta = (WorldContext = "WorldContextObject"))
	static FString GetEasySessionDisplayName(const UObject* WorldContextObject);

	/** Per-player info for everyone in the session: name, whether it is the local player on this machine, and whether it is the session host. */
	UFUNCTION(BlueprintPure, Category = "EasySession|Session", meta = (WorldContext = "WorldContextObject"))
	static TArray<FEasySessionPlayerInfo> GetEasySessionPlayerInfos(const UObject* WorldContextObject);

	/**
	 * Change whether the local player is ready, which every player in the session sees in Get Easy Session Player Infos.
	 * The plugin only shares the value, and the game decides what being ready allows, such as starting the match.
	 * Unset again in every map the session travels to.
	 *
	 * @return Success, or No Session Exists outside a session.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Session", meta = (WorldContext = "WorldContextObject"))
	static EEasySessionResult SetEasySessionReady(const UObject* WorldContextObject, bool bReady);

	/** The number of players in the session. */
	UFUNCTION(BlueprintPure, Category = "EasySession|Session", meta = (WorldContext = "WorldContextObject"))
	static int32 GetEasySessionPlayerCount(const UObject* WorldContextObject);

	/** The maximum number of players allowed in the current session. 0 when no session exists. */
	UFUNCTION(BlueprintPure, Category = "EasySession|Session", meta = (WorldContext = "WorldContextObject"))
	static int32 GetEasySessionMaxPlayers(const UObject* WorldContextObject);

	/** Whether the local player is in a party. */
	UFUNCTION(BlueprintPure, Category = "EasySession|Party", meta = (WorldContext = "WorldContextObject"))
	static bool IsInEasyParty(const UObject* WorldContextObject);

	/** Whether the local player leads the party. False outside a party. */
	UFUNCTION(BlueprintPure, Category = "EasySession|Party", meta = (WorldContext = "WorldContextObject"))
	static bool IsEasyPartyLeader(const UObject* WorldContextObject);

	/** Every member of the party, the leader included: name, whether it is the local player, and whether it leads the party. */
	UFUNCTION(BlueprintPure, Category = "EasySession|Party", meta = (WorldContext = "WorldContextObject"))
	static TArray<FEasyPartyMemberInfo> GetEasyPartyMembers(const UObject* WorldContextObject);

	/**
	 * Whether the party of the last match is being got back: created again on the leader, or looked for on a member.
	 * True while a member waits for a leader who stays in the match longer, when Is In Easy Party is still false.
	 * A menu can show a waiting message then, instead of the buttons that would stop the restore.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession|Party", meta = (WorldContext = "WorldContextObject"))
	static bool IsEasyPartyRestoring(const UObject* WorldContextObject);

	/**
	 * The settings of the party: how many players it holds and who may join it, as Create Easy Party set them.
	 * Works for the leader and every member, for example to show the party as 2/4. Default settings outside a party.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession|Party", meta = (WorldContext = "WorldContextObject"))
	static FEasyPartySettings GetEasyPartySettings(const UObject* WorldContextObject);

	/** The join code the party advertises, for the leader to show and share. Empty when the party's privacy is not Join Code, or outside a party. */
	UFUNCTION(BlueprintPure, Category = "EasySession|Party", meta = (WorldContext = "WorldContextObject"))
	static FString GetEasyPartyJoinCode(const UObject* WorldContextObject);

	/**
	 * Remove a member from the party, and keep them out of this party.
	 * The member receives On Party Left with Kicked and this reason.
	 * Party leader only.
	 *
	 * @return Success, Requires Party Leader, or Invalid Params for a player who is not a connected member.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Party", meta = (WorldContext = "WorldContextObject"))
	static EEasySessionResult KickEasyPartyMember(const UObject* WorldContextObject, const FEasyPartyMemberInfo& Member, FText Reason);

	/**
	 * Change whether the local player is ready, which every member sees in Get Easy Party Members.
	 * The plugin only shares the value, and the game decides what being ready allows.
	 *
	 * @return Success, or No Session Exists outside a party.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Party", meta = (WorldContext = "WorldContextObject"))
	static EEasySessionResult SetEasyPartyReady(const UObject* WorldContextObject, bool bReady);

	/** Whether a disconnect reason is waiting to be shown, for example as a popup on the menu. */
	UFUNCTION(BlueprintPure, Category = "EasySession|Session", meta = (WorldContext = "WorldContextObject"))
	static bool HasPendingEasyDisconnectInfo(const UObject* WorldContextObject);

	/** Take the waiting disconnect info. It is kept across map travel until this call, and empty when none is waiting. */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Session", meta = (WorldContext = "WorldContextObject"))
	static FEasyDisconnectInfo ConsumePendingEasyDisconnectInfo(const UObject* WorldContextObject);

	/** The name of the online subsystem in use (e.g. NULL, STEAM, EOS). */
	UFUNCTION(BlueprintPure, Category = "EasySession|Advanced", DisplayName = "Get Online Subsystem Name (EasySession)", meta = (WorldContext = "WorldContextObject"))
	static FName GetOnlineSubsystemName(const UObject* WorldContextObject);

	/** Whether an online subsystem is available and its session interface is valid. */
	UFUNCTION(BlueprintPure, Category = "EasySession|Advanced", DisplayName = "Is Online Subsystem Available (EasySession)", meta = (WorldContext = "WorldContextObject"))
	static bool IsOnlineSubsystemAvailable(const UObject* WorldContextObject);

	/** What the session queue is doing right now, for status UI and bug reports, e.g. "Create (running 2.4s), queued: Start" or "Idle". */
	UFUNCTION(BlueprintPure, Category = "EasySession|Advanced", meta = (WorldContext = "WorldContextObject"))
	static FString GetEasySessionQueueStatus(const UObject* WorldContextObject);

	/**
	 * The settings the current session is advertising, so one field can be changed and passed to Update Easy Session.
	 * Building new settings instead resets every field you did not fill in.
	 * Works for every player in the session.
	 * The password and its friends exception are only filled on the host, the one game that holds them.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession|Session", meta = (WorldContext = "WorldContextObject"))
	static FEasySessionSettings GetEasySessionSettings(const UObject* WorldContextObject);

	/**
	 * The join code the current session advertises, or empty when it advertises none.
	 * Works for every player in the session, so any session member can share the code.
	 */
	UFUNCTION(BlueprintPure, Category = "EasySession|Session", meta = (WorldContext = "WorldContextObject"))
	static FString GetEasySessionJoinCode(const UObject* WorldContextObject);

	/**
	 * Cancel the running matchmaking.
	 * A search ends inside this call.
	 * A join or host that completes after the cancel is undone.
	 * Does nothing when no matchmaking is running.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Matchmaking", meta = (WorldContext = "WorldContextObject"))
	static void CancelEasyMatchmaking(const UObject* WorldContextObject);

	/** Cancel the running Find Easy Friend Sessions. It completes with Canceled. Does nothing when none is running. */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Friends", meta = (WorldContext = "WorldContextObject"))
	static void CancelEasyFriendSearch(const UObject* WorldContextObject);

	/**
	 * ServerTravel the current session to a new map, bringing every connected player along.
	 * Extra travel options go after a '?'. The ?listen option is appended for you, unless this game is a dedicated server or the map name already has it.
	 * Session authority only: on any other game this does nothing and returns false.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Advanced", meta = (WorldContext = "WorldContextObject"))
	static bool ServerTravelEasySession(const UObject* WorldContextObject, const FString& MapName);

	/**
	 * Remove a player from the session, and keep them out until the session is destroyed.
	 * The player travels to the menu, and Consume Pending Easy Disconnect Info returns Kicked with this reason there.
	 * Session authority only.
	 *
	 * @return Success, Requires Session Authority, or Invalid Params for a player who is not a connected remote player.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Advanced", meta = (WorldContext = "WorldContextObject"))
	static EEasySessionResult KickEasySessionPlayer(const UObject* WorldContextObject, const FEasySessionPlayerInfo& Player, FText Reason);

	/**
	 * Destroy the session for every player.
	 * Clients record Reason as a Host Destroyed Session disconnect and travel back to the menu.
	 * There, Consume Pending Easy Disconnect Info returns it so the menu can show it to the player.
	 * Session authority only: on any other game this does nothing.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Advanced", meta = (WorldContext = "WorldContextObject"))
	static void DestroyEasySessionForEveryone(const UObject* WorldContextObject, FText Reason);

	/**
	 * Invite a friend to the current session.
	 *
	 * @return Success, or why not: Not Supported By Service on an online subsystem without invites such as NULL (LAN),
	 *         No Session Exists with no session to invite to, or Invalid Params for a friend Read Easy Friends did not return.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Invites", meta = (WorldContext = "WorldContextObject"))
	static EEasySessionResult SendEasySessionInviteToFriend(const UObject* WorldContextObject, const FEasySessionFriend& Friend);

	/**
	 * Open the platform invite overlay (e.g. Steam) for the current session.
	 *
	 * @return Success, or Not Supported By Service on an online subsystem without an overlay such as NULL (LAN).
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Invites", meta = (WorldContext = "WorldContextObject"))
	static EEasySessionResult ShowEasyInviteUI(const UObject* WorldContextObject);

	/**
	 * Invite a friend to the party. Any member can do this.
	 *
	 * @return Success, or why not: No Session Exists outside a party,
	 *         Not Supported By Service on an online subsystem without invites such as NULL (LAN), or Invalid Params for a friend Read Easy Friends did not return.
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Invites", meta = (WorldContext = "WorldContextObject"))
	static EEasySessionResult SendEasyPartyInviteToFriend(const UObject* WorldContextObject, const FEasySessionFriend& Friend);

	/**
	 * Open the platform invite overlay (e.g. Steam) for the party. Any member can do this.
	 *
	 * @return Success, or why the overlay could not be opened: No Session Exists outside a party,
	 *         or Not Supported By Service on an online subsystem without an overlay such as NULL (LAN).
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Invites", meta = (WorldContext = "WorldContextObject"))
	static EEasySessionResult ShowEasyPartyInviteUI(const UObject* WorldContextObject);

	/**
	 * Open the platform profile overlay (e.g. Steam) for the given friend.
	 *
	 * @return Success, or Not Supported By Service on an online subsystem without an overlay such as NULL (LAN).
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Invites", meta = (WorldContext = "WorldContextObject"))
	static EEasySessionResult ShowEasyProfileUI(const UObject* WorldContextObject, const FEasySessionFriend& Friend);

	/**
	 * Open the platform profile overlay (e.g. Steam) for a player in the session.
	 *
	 * @return Success, or Not Supported By Service on an online subsystem without an overlay such as NULL (LAN).
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Invites", meta = (WorldContext = "WorldContextObject"))
	static EEasySessionResult ShowEasyProfileUIForPlayer(const UObject* WorldContextObject, const FEasySessionPlayerInfo& Player);

	/**
	 * Open the platform profile overlay (e.g. Steam) for a member of the party.
	 *
	 * @return Success, or Not Supported By Service on an online subsystem without an overlay such as NULL (LAN).
	 */
	UFUNCTION(BlueprintCallable, Category = "EasySession|Invites", meta = (WorldContext = "WorldContextObject"))
	static EEasySessionResult ShowEasyProfileUIForPartyMember(const UObject* WorldContextObject, const FEasyPartyMemberInfo& Member);
};
