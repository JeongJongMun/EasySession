// Copyright (c) 2026 Langerak. Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameFramework/OnlineReplStructs.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "EasySessionTypes.h"
#include "Containers/Ticker.h"
#include "Templates/SubclassOf.h"
#include "EasySessionSubsystem.generated.h"

namespace ENetworkFailure
{
	enum Type : int;
}

namespace ETravelFailure
{
	enum Type : int;
}

class AController;
class AGameModeBase;
class APlayerController;
class FEasySessionBeaconPort;
class FEasySessionHost;
class FEasySessionParty;
class FEasySessionRequest;
class FEasySessionRequestQueue;
class FEasySessionSocial;
class FEasySessionTravel;
class UEasyMatchmakingPolicy;
struct FEasySessionRequestContext;

/** Multicast event fired with the result of a session request, used by the async nodes and by On Matchmaking Complete. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEasySessionEvent, EEasySessionResult, Result, const FString&, ErrorMessage);

/** Multicast event fired with the result of a session or party search, used by the Find Easy Sessions and Find Easy Parties nodes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FEasySessionFindEvent, EEasySessionResult, Result, const FString&, ErrorMessage, const TArray<FEasySessionSearchResult>&, Results);

/** Multicast event fired when something fails outside any node's result, such as a lost connection or a failed travel. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEasySessionFailureEvent, const FString&, Reason);

/** Multicast event fired when the player accepts an invite from the platform overlay. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEasySessionInviteAcceptedEvent, const FEasySessionSearchResult&, Session);

/** Multicast event fired when the session's advertised settings change. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FEasySessionSettingsChangedEvent);

/** Multicast event fired when the session's lifecycle state changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEasySessionStateEvent, EEasySessionState, OldState, EEasySessionState, NewState);

/** Multicast event fired when Is Busy changes, so a UI can enable and disable its buttons without polling. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEasySessionBusyChangedEvent, bool, bBusy);

/** Multicast event fired when a matchmaking run is accepted and its policy is registered. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FEasyMatchmakingStartedEvent);

/** Multicast event fired when the state of the running matchmaking changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEasyMatchmakingStateEvent, EEasyMatchmakingState, OldState, EEasyMatchmakingState, NewState);

/** Multicast event fired on every state change of a matchmaking run, and once a second while it runs. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEasyMatchmakingUpdatedEvent, EEasyMatchmakingState, State, int32, ElapsedSeconds);

/** Multicast event fired when a player joins or leaves the session, or changes whether they are ready. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FEasySessionPlayersChangedEvent);

/** Multicast event fired when a member joins or leaves the party, or changes whether they are ready, and when the local player leaves it. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FEasyPartyMembersChangedEvent);

/** Multicast event fired when the local player is no longer in the party, with the reason. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEasyPartyLeftEvent, EEasyPartyLeaveReason, Reason, const FText&, ReasonText);

/** Multicast event fired with the result of a friends list read, used by the Read Easy Friends node. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FEasyFriendsEvent, EEasySessionResult, Result, const FString&, ErrorMessage, const TArray<FEasySessionFriend>&, Friends);

/** Multicast event fired with the result of a friend session search, used by the Find Easy Friend Sessions node. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FEasyFriendSessionsEvent, EEasySessionResult, Result, const FString&, ErrorMessage, const TArray<FEasyFriendSession>&, FriendSessions);

/**
 * The EasySession subsystem is responsible for every session request of the plugin.
 * It is created automatically for each game instance, so no custom GameInstance class is needed.
 *
 * All requests are queued and run one at a time, so a call made while another request runs waits in the queue instead of failing in the online subsystem.
 * Each request reports its result through its completion delegate, or the output pins of its async node.
 * The events below report states and runs, not single requests: the session's state, settings and players, busy, matchmaking, the party and failures.
 */
UCLASS()
class EASYSESSION_API UEasySessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

	//~ FEasySessionTestAccess is the only friend, the attorney of the attorney-client idiom.
	//~ It reads private state for the tests, so no test API ships on the subsystem.
	//~ The internal collaborators are not friends.
	//~ They call this subsystem's public API only, so this header lists everything they can call.
	friend class FEasySessionTestAccess;

public:

	/**
	 * Declared here and defined in the .cpp on purpose.
	 * The collaborators below are held by TUniquePtr to types this header only forward declares.
	 * A compiler generated constructor or destructor would have to instantiate their deleters where those types are still incomplete.
	 * That is exactly where UHT puts the constructors it generates, including the hot reload one.
	 * The engine's own pimpl holders (UPrimitiveComponent, ULocalPlayer, UNetConnection) declare the same three for the same reason.
	 */
	UEasySessionSubsystem();
	UEasySessionSubsystem(FVTableHelper& Helper);
	virtual ~UEasySessionSubsystem() override;

	//~ Begin USubsystem Interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem Interface

public:

	/**
	 * Fired when the advertised session settings change: on the host when it updates them, and on a client when the host's values arrive.
	 * Get Easy Session Settings already returns the new values when this fires, so a UI reads them and refreshes.
	 */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasySessionSettingsChangedEvent OnSessionSettingsChanged;

	/**
	 * Fired when the session's lifecycle state changes, on the host and on every client.
	 * Creating a session and destroying one are state changes too, from and to No Session.
	 * A client reports the host's replicated state, so this fires there when the host starts or ends the match.
	 */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasySessionStateEvent OnSessionStateChanged;

	/**
	 * Fired whenever Is Busy changes, with the new value.
	 * Bind once and enable or disable session buttons from the flag instead of reading Is Busy every tick.
	 */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasySessionBusyChangedEvent OnBusyChanged;

	/**
	 * Fired when a matchmaking run is accepted.
	 * It is the first event of that run, and from then on Get Active Easy Matchmaking Policy returns the run's policy.
	 */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasyMatchmakingStartedEvent OnMatchmakingStarted;

	/**
	 * Fired when the state of the running matchmaking changes.
	 * A progress UI binds here once, for every run.
	 */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasyMatchmakingStateEvent OnMatchmakingStateChanged;

	/** Fired on every state change of the running matchmaking and once a second while it runs, with the elapsed whole seconds. */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasyMatchmakingUpdatedEvent OnMatchmakingUpdated;

	/** Fired when a matchmaking run completes, a canceled one included. A cancel arrives as the Canceled result, never as a separate event. */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasySessionEvent OnMatchmakingComplete;

	/**
	 * Fired when something fails outside any node's result.
	 * That is a dropped connection, or an accepted invite that this player does not join or whose join fails.
	 * It also fires when following the party leader or the host into a new session fails.
	 * It also covers a travel or listen server started by EasySession that fails, for example on a wrong Initial Map Name.
	 */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasySessionFailureEvent OnSessionFailure;

	/**
	 * Fired when the player accepts an invite from the platform overlay.
	 * With Auto Join Accepted Invites on, a running matchmaking is canceled and the join of the invited session is queued after this event fires.
	 * A player who is already in a session joins only when Accept Invites While In Session is on.
	 * Their current session is destroyed first, which disconnects everyone if they were hosting it.
	 * With Auto Join Accepted Invites off, call Join Easy Session yourself, for example after asking the player.
	 */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasySessionInviteAcceptedEvent OnSessionInviteAccepted;

	/**
	 * Fired when a player joins or leaves the session, or changes whether they are ready, on the host and on every client.
	 * Get Easy Session Player Infos already returns the new list when this fires, so a UI reads it and refreshes.
	 */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasySessionPlayersChangedEvent OnSessionPlayersChanged;

	/**
	 * Fired when a member joins or leaves the party, or changes whether they are ready, on the leader and on every member.
	 * Get Easy Party Members already returns the new list when this fires, so a UI reads it and refreshes.
	 */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasyPartyMembersChangedEvent OnPartyMembersChanged;

	/**
	 * Fired when the local player is no longer in the party.
	 * They left, the leader kicked them or left, the connection was lost, or the party entered a game session.
	 * Reason Text is the leader's reason for a kick, and a message for the player otherwise.
	 */
	UPROPERTY(BlueprintAssignable, Category = "EasySession|Events")
	FEasyPartyLeftEvent OnPartyLeft;

public:

	/**
	 * Create a new session and travel to Initial Map Name.
	 * For listen servers the map is opened with the ?listen option automatically.
	 * A party leader brings the party, whose members follow once the map is open, and a party member is refused with InParty.
	 *
	 * @param HostParams Parameters describing the session to create.
	 * @param OnComplete Called when the request completes.
	 */
	void CreateSession(const FEasySessionHostParams& HostParams, FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * Search for sessions matching the given filters.
	 *
	 * @param SearchParams Parameters describing what to search for.
	 * @param OnComplete Called with the filtered results when the search completes.
	 */
	void FindSessions(const FEasySessionSearchParams& SearchParams, FEasySessionFindCompleteDelegate OnComplete = FEasySessionFindCompleteDelegate());

	/**
	 * Join the given session and travel to the host.
	 * A player in another session destroys it first, once the new host approved the join, and a host tells its clients why.
	 * A join that fails after that destroy travels the player to the menu.
	 * Joining the session this player is already in fails with SessionAlreadyExists, and so does a join by the host of a match in progress.
	 * A player in a session stays in it when the reservation beacon cannot reach the new host.
	 * A party leader, or the host of a match that has not started, brings the group, which follows before this player joins.
	 * A party member is refused with InParty.
	 *
	 * @param SearchResult A search result returned by FindSessions.
	 * @param Password Password for password protected sessions. Ignored otherwise.
	 * @param AdditionalTravelOptions Extra options appended to the client travel URL (e.g. "Name=Player?Team=1").
	 * @param OnComplete Called when the request completes.
	 */
	void JoinSession(const FEasySessionSearchResult& SearchResult, const FString& Password = FString(), const FString& AdditionalTravelOptions = FString(), FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * Start the match.
	 * The session moves to InProgress.
	 * When Allow Join In Progress is off, new players are refused from here until the match ends.
	 * On Steam they are refused from the first join on, because Steam closes the lobby when the first player joins.
	 * Needs session authority: only the game that created the session can start the match.
	 *
	 * @param OnComplete Called when the request completes.
	 */
	void StartSession(FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * End the match.
	 * The session moves to Ended, so a new match can be started.
	 * Needs session authority: only the game that created the session can end the match.
	 *
	 * @param OnComplete Called when the request completes.
	 */
	void EndSession(FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * Destroy the current session.
	 * Without session authority, this destroys only this game's copy of the session.
	 *
	 * @param OnComplete Called when the request completes.
	 */
	void DestroySession(FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * Leave the session: destroy this game's named session, then travel to the menu map.
	 * A leaving host takes the session with it, so it destroys the session for everyone and every client receives "The host has left the game." first.
	 * The menu travel runs whatever the destroy reported, because the player asked to leave.
	 *
	 * @param OnComplete Called with the destroy's result, after the menu travel was requested.
	 */
	void LeaveSession(FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * Destroy the session for every player.
	 * Clients record Reason as a Host Destroyed Session disconnect, and travel to the menu when Auto Return To Menu On Disconnect is on.
	 * There, ConsumePendingDisconnectInfo returns it so the menu can show it to the player.
	 * Needs session authority: only the game that created the session can do this.
	 *
	 * @param OnComplete Called with the destroy's result, after the host's own menu travel was requested.
	 */
	void DestroySessionForEveryone(FText Reason, FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * Update the advertised properties of the current session.
	 * Needs session authority: only the game that created the session can update it.
	 *
	 * Every field is applied as given, including Password.
	 * Pass settings from GetSessionSettings and change only what you mean to change.
	 * Otherwise the fields you left at their defaults overwrite the session with those defaults.
	 *
	 * @param NewSettings The settings to advertise in place of the current ones.
	 * @param OnComplete Called when the request completes.
	 */
	void UpdateSession(const FEasySessionSettings& NewSettings, FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * Remove a player from the session, and keep them out until the session is destroyed.
	 * ConsumePendingDisconnectInfo then returns Kicked with this reason, and the player travels to the menu when Auto Return To Menu On Disconnect is on.
	 * Needs session authority: only the game that created the session can do this.
	 *
	 * @return Success, RequiresSessionAuthority, or InvalidParams for a player who is not a connected remote player.
	 */
	EEasySessionResult KickPlayer(const FEasySessionPlayerInfo& Player, const FText& Reason);

	/**
	 * Change whether the local player is ready, which every player in the session sees in GetSessionPlayerInfos.
	 * The plugin only shares the value, and the game decides what being ready allows, such as starting the match.
	 * Every travel of the session sets it back to false.
	 *
	 * @return Success, or NoSessionExists outside a session.
	 */
	EEasySessionResult SetSessionReady(bool bReady);

	/**
	 * ServerTravel the current session to a new map, bringing every connected player along.
	 * Extra travel options go after a '?'.
	 * The ?listen option is appended for you, unless this game is a dedicated server or the map name already has it.
	 * Needs session authority: only the game that created the session can travel it.
	 * Returns false for other games.
	 */
	bool ServerTravel(const FString& MapName);

	/**
	 * Start Matchmaking: search for sessions, join the best one, and optionally host a new session when nothing is found.
	 * The run holds the session queue until it ends, so session requests made meanwhile run after it.
	 * A party leader, or the host of a match that has not started, searches for open slots for the whole group and brings it.
	 * A party member is refused with InParty.
	 *
	 * @param MatchmakingParams Parameters describing the search and the fallback host session.
	 * @param PolicyClass Optional custom matchmaking policy class. Uses the default policy when null.
	 * @param OnComplete Called when matchmaking completes.
	 */
	void StartMatchmaking(const FEasyMatchmakingParams& MatchmakingParams, TSubclassOf<UEasyMatchmakingPolicy> PolicyClass = nullptr, FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * Cancel the running matchmaking.
	 * A search ends inside this call.
	 * A join or host that completes after the cancel is undone.
	 * Does nothing when no matchmaking is running.
	 */
	void CancelMatchmaking();

public:

	/**
	 * Create a party, with the local player as its leader.
	 * A party lives outside game sessions and needs no map, so nothing travels.
	 * Needs a player logged in to the online subsystem, because members are told apart by their ids.
	 *
	 * @param PartySettings How many players the party holds, whether FindParties lists it, and whether it advertises a join code.
	 * @param OnComplete Called when the request completes.
	 */
	void CreateParty(const FEasyPartySettings& PartySettings, FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * Search for parties.
	 * Without a join code this lists the parties that are not hidden, and with one it finds the party that advertises it, hidden or not.
	 *
	 * @param SearchParams Parameters describing what to search for. Region, Required Custom Settings and Include In Progress Sessions are ignored,
	 *        because a party advertises none of them.
	 * @param OnComplete Called with the parties found when the search completes.
	 */
	void FindParties(const FEasySessionSearchParams& SearchParams, FEasySessionFindCompleteDelegate OnComplete = FEasySessionFindCompleteDelegate());

	/**
	 * Join a party a search returned.
	 * The leader decides the join, and a refusal fails with JoinRefused and the leader's reason.
	 * A player in a party or in a game session fails with SessionAlreadyExists.
	 *
	 * @param SearchResult A party returned by FindParties.
	 * @param OnComplete Called when the request completes. On Success, GetPartyMembers already lists the local player.
	 */
	void JoinParty(const FEasySessionSearchResult& SearchResult, FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * Leave the party.
	 * A leader who leaves ends the party for every member.
	 * OnPartyLeft fires with Left once the party is left.
	 *
	 * @param OnComplete Called when the request completes.
	 */
	void LeaveParty(FEasySessionCompleteDelegate OnComplete = FEasySessionCompleteDelegate());

	/**
	 * Remove a member from the party, and keep them out of this party.
	 * The member receives OnPartyLeft with Kicked and this reason.
	 * Only the leader can do this.
	 *
	 * @return Success, RequiresPartyLeader, or InvalidParams for a player who is not a connected member.
	 */
	EEasySessionResult KickPartyMember(const FEasyPartyMemberInfo& Member, const FText& Reason);

	/**
	 * Change whether the local player is ready, which every member sees in GetPartyMembers.
	 * The plugin only shares the value, and the game decides what being ready allows.
	 *
	 * @return Success, or NoSessionExists outside a party.
	 */
	EEasySessionResult SetPartyReady(bool bReady);

	/** @return Whether the local player is in a party. */
	bool IsInParty() const;

	/** @return Whether the local player leads the party. False outside a party. */
	bool IsPartyLeader() const;

	/** @return Every member of the party, the leader included. Empty outside a party. */
	TArray<FEasyPartyMemberInfo> GetPartyMembers() const;

	/**
	 * @return Whether the party of the last match is being restored: created again on the leader, or searched for and joined on a member.
	 *         A member searches until the party is back or Party Restore Wait Seconds ends, and IsInParty stays false meanwhile.
	 */
	bool IsRestoringParty() const;

	/**
	 * @return The settings the party advertises: how many players it holds, whether it is hidden, and whether it uses a join code.
	 *         Works for the leader and every member. Default settings outside a party.
	 */
	FEasyPartySettings GetPartySettings() const;

	/**
	 * @return The join code the party advertises, or empty when it uses none or there is no party.
	 *         Works for the leader and every member, so any member can share the code.
	 */
	FString GetPartyJoinCode() const;

public:

	/** @return Whether matchmaking is running. */
	bool IsMatchmakingRunning() const;

	/** @return The state of the running matchmaking. Idle when none is running. */
	EEasyMatchmakingState GetMatchmakingState() const;

	/**
	 * @return The policy of the running matchmaking, or null when none is running.
	 *         Progress is broadcast on OnMatchmakingStateChanged and OnMatchmakingUpdated of this subsystem, not on the policy.
	 */
	UEasyMatchmakingPolicy* GetActiveMatchmakingPolicy() const;

	/** @return Whether the local player is in a session. */
	bool IsInSession() const;

	/**
	 * @return The lifecycle state of the current session (Pending, InProgress, Ended, ...).
	 *         The host reports its own state. A client reports the host's replicated state once it has arrived, and its own until then.
	 */
	EEasySessionState GetSessionState() const;

	/**
	 * Get these, change the one field, and pass them to Update Easy Session.
	 * Building new settings instead resets every field you did not fill in.
	 *
	 * @return The settings the current session is advertising, so a change can be made without restating everything else.
	 *         Works for every player in the session; the password and its friends exception are only filled on the host, the one game that holds them.
	 */
	FEasySessionSettings GetSessionSettings() const;

	/**
	 * @return The join code the current session advertises, or empty when it advertises none.
	 *         Works for every player in the session, so any session member can share the code.
	 */
	FString GetSessionJoinCode() const;

	/**
	 * @return Whether the local player is hosting the current session.
	 *         False on a dedicated server, which has no local player, so ask IsSessionAuthority there.
	 */
	bool IsHost() const;

	/**
	 * @return Whether this game created the session it is in, so it may Start, End, Update, travel or destroy it.
	 *         Read from FNamedOnlineSession's bHosting, which this plugin sets when the create completes because Steam never does.
	 *         Is Host is a different question, and false on a dedicated server.
	 */
	bool IsSessionAuthority() const;

	/** @return The display name of the current session. Empty when no session exists. */
	FString GetSessionDisplayName() const;

	/** @return Per-player info for everyone in the session: name, whether it is the local player on this machine, and whether it is the session host. */
	TArray<FEasySessionPlayerInfo> GetSessionPlayerInfos() const;

	/** @return The number of players currently in the session. */
	int32 GetSessionPlayerCount() const;

	/** @return The maximum number of players allowed in the current session. 0 when no session exists. */
	int32 GetSessionMaxPlayers() const;

	/**
	 * Session buttons read this to disable themselves.
	 * IsMatchmakingRunning asks about matchmaking alone.
	 *
	 * @return Whether a request is running or queued, matchmaking included,
	 *         or a travel this plugin started has not loaded its map yet.
	 */
	bool IsBusy() const;

	/**
	 * What keeps IsBusy true: the running or waiting request, or a travel this plugin started.
	 * It covers requests the game did not start too, such as the destroy after a lost connection.
	 * Get Activity Message turns it into a status line such as "Joining the session...".
	 *
	 * @return None exactly when IsBusy is false.
	 */
	EEasySessionActivity GetActivity() const;

	/** @return What the session queue is doing right now, for status UI and bug reports, e.g. "Create (running 2.4s), queued: Start" or "Idle". */
	FString GetQueueStatus() const;

public:

	/**
	 * Invite a friend to the current session.
	 *
	 * @return Success, or why not: NotSupportedByService on an online subsystem without invites such as NULL (LAN),
	 *         NoSessionExists with no session to invite to, InvalidParams for a friend ReadFriends did not return, or NoOnlineSubsystem.
	 */
	EEasySessionResult SendSessionInviteToFriend(const FEasySessionFriend& Friend);

	/**
	 * Open the platform invite overlay (e.g. Steam) for the current session.
	 *
	 * @return Success, or why the overlay could not be opened.
	 */
	EEasySessionResult ShowInviteUI();

	/**
	 * Invite a friend to the party.
	 * Any member can do this.
	 *
	 * @return Success, or why not: NoSessionExists outside a party,
	 *         NotSupportedByService on an online subsystem without invites such as NULL (LAN), InvalidParams for a friend ReadFriends did not return,
	 *         or NoOnlineSubsystem.
	 */
	EEasySessionResult SendPartyInviteToFriend(const FEasySessionFriend& Friend);

	/**
	 * Open the platform invite overlay (e.g. Steam) for the party.
	 * Any member can do this.
	 *
	 * @return Success, or why the overlay could not be opened.
	 */
	EEasySessionResult ShowPartyInviteUI();

	/**
	 * Open the platform profile overlay (e.g. Steam) for the given friend.
	 *
	 * @return Success, or why the overlay could not be opened.
	 */
	EEasySessionResult ShowProfileUI(const FEasySessionFriend& Friend);

	/**
	 * Open the platform profile overlay (e.g. Steam) for a player in the session.
	 *
	 * @return Success, or why the overlay could not be opened.
	 */
	EEasySessionResult ShowProfileUIForPlayer(const FEasySessionPlayerInfo& Player);

	/**
	 * Open the platform profile overlay (e.g. Steam) for a member of the party.
	 *
	 * @return Success, or why the overlay could not be opened.
	 */
	EEasySessionResult ShowProfileUIForPartyMember(const FEasyPartyMemberInfo& Member);

	/**
	 * Read the local player's friends list, in display order.
	 * Not supported on the NULL (LAN) subsystem.
	 * The read waits in the session queue like every other request.
	 *
	 * @param OnComplete Called with the friends when the read completes.
	 */
	void ReadFriends(FEasyFriendsCompleteDelegate OnComplete = FEasyFriendsCompleteDelegate());

	/**
	 * Read the friends list and find the session each friend playing this game is in.
	 * Not supported on the NULL (LAN) subsystem.
	 * The search holds the session queue until it ends, so session requests made meanwhile run after it.
	 * One friend search runs at a time.
	 * A second call while one runs fails with FriendSearchAlreadyInProgress.
	 *
	 * @param OnComplete Called with one friend session per friend. Those with bHasSession carry a session joinable with JoinSession.
	 */
	void FindFriendSessions(FEasyFriendSessionsCompleteDelegate OnComplete = FEasyFriendSessionsCompleteDelegate());

	/**
	 * Cancel the running friend session search.
	 * It completes with Canceled inside this call.
	 * Does nothing when none is running.
	 */
	void CancelFriendSearch();

public:

	/** @return Whether a disconnect reason is waiting to be shown (e.g. as a popup on the menu). */
	bool HasPendingDisconnectInfo() const { return PendingDisconnectInfo.IsSet(); }

	/**
	 * Take the waiting disconnect info, or an empty one when none is waiting.
	 * It is kept across map travel until this call.
	 */
	FEasyDisconnectInfo ConsumePendingDisconnectInfo();

	/**
	 * Record a disconnect, destroy the lost session, and travel to the project's Game Default Map when Auto Return To Menu On Disconnect is on.
	 * Called automatically on network and travel failures.
	 * Call it yourself only if you detect disconnects yourself.
	 * The first reason recorded is kept until it is consumed, so a later failure cannot replace the first cause.
	 */
	void HandleDisconnect(EEasyDisconnectReason Reason, const FText& ReasonText);

public:

	/**
	 * C++ delegate: change the server travel URL (hosting and server travel) before it is used.
	 * Bind at startup.
	 * The delegate fires before the completion callback of the request that travels, so binding inside that callback misses its own travel.
	 * For one request's options, use Additional Travel Options on the params instead.
	 */
	FEasyModifyTravelURLDelegate OnModifyServerTravelURL;

	/**
	 * C++ delegate: change the client travel URL (joining a host) before it is used.
	 * Bind at startup, with the same timing and the same per-request alternative as OnModifyServerTravelURL.
	 */
	FEasyModifyTravelURLDelegate OnModifyClientTravelURL;

public:

	/**
	 * Internal, called by every request when it completes and by HandleReplicatedSessionState.
	 * Broadcasts OnSessionStateChanged when the session state differs from the last one reported.
	 * A state that lasts less than a frame still reaches the game this way, which a ticker that samples once a frame would miss.
	 */
	void RefreshSessionState();

	/** Internal, called by the Destroy request: clear the host state a client received through replication. */
	void ClearReplicatedSessionState();

	/**
	 * Internal, called by the state actor: receive the host's replicated session state.
	 * Clients store it, and GetSessionState returns it in place of their own session copy's state.
	 */
	void HandleReplicatedSessionState(EEasySessionState HostState);

	/**
	 * Internal, called by the state actor: receive the host's replicated session settings.
	 * Clients write them into their local session copy so the regular getters return the host's values, then broadcast OnSessionSettingsChanged.
	 */
	void HandleReplicatedSessionSettings(const FEasySessionReplicatedSettings& Settings);

	/**
	 * Internal, called when the party leader or the host tells this player to follow: join the session of a host who holds a reservation for this player.
	 * Runs as a matchmaking run that searches for that host only, so the matchmaking events report its progress and CancelMatchmaking stops it.
	 *
	 * @param HostId The player who hosts the session to join.
	 * @param bLANQuery Whether that session is a LAN session.
	 */
	void FollowHost(const FUniqueNetIdRepl& HostId, bool bLANQuery);

	/**
	 * Internal, called when the party ends for the local player without LeaveParty.
	 * That is when the leader ended the membership, the connection to the leader was lost, or this player entered a game session.
	 * Queues a leave of the party session unless one is queued already, and that leave broadcasts OnPartyLeft with the reason.
	 */
	void HandlePartyEnded(EEasyPartyLeaveReason Reason, const FText& ReasonText);

	/**
	 * Internal, called by the party to queue the requests that restore the party of the last match.
	 * The public calls such as CreateParty and JoinParty cancel the restore first, so they cannot be used for this.
	 */
	void EnqueuePartyRequest(TSharedRef<FEasySessionRequest> Request);

	/**
	 * Internal, called by a player component when its player appears, leaves or changes whether they are ready.
	 * Broadcasts OnSessionPlayersChanged on the next tick, once per frame however many changes arrived.
	 */
	void HandleSessionPlayersChanged();

private:

	/** @return Whether the local player is in a party they do not lead, so the leader decides where they go. */
	bool IsPartyMember() const;

	/** Resolve the session interface for the current world context. */
	IOnlineSessionPtr GetSessionInterface() const;

	/**
	 * Add a request to the queue and start processing if idle.
	 *
	 * @param SessionName The session the request acts on: the game session, or the party session for a party request.
	 */
	void EnqueueRequest(TSharedRef<FEasySessionRequest> Request, FName SessionName = NAME_GameSession);

	/** Session state as derived from the local online subsystem session copy. */
	EEasySessionState GetLocalSessionState() const;

	/** Broadcast OnBusyChanged when IsBusy differs from the last value reported. */
	void RefreshBusyState();

	/** Engine delegate handlers. */
	void HandleNetworkFailure(UWorld* World, class UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);

private:

	/**
	 * Internal collaborators.
	 * Each owns the state and the engine delegates of one area: the queue, travel, invites, the beacon port, hosting or the party.
	 * Created in Initialize and destroyed in Deinitialize, which is what unbinds them.
	 */
	TUniquePtr<FEasySessionRequestQueue> RequestQueue;
	TUniquePtr<FEasySessionTravel> Travel;
	TUniquePtr<FEasySessionSocial> Social;
	TUniquePtr<FEasySessionBeaconPort> BeaconPort;
	TUniquePtr<FEasySessionHost> Host;
	TUniquePtr<FEasySessionParty> Party;

	/** The subsystem, queue, travel, host and party that every request reads while it runs. Created after the collaborators it refers to. */
	TUniquePtr<FEasySessionRequestContext> RequestContext;

	/** The host's session state as last received through replication, on a client. Unset until it arrives for the current session. */
	TOptional<EEasySessionState> ReplicatedSessionState;

	/** Latest session settings applied through replication (clients only). Guards against re-applying the same payload. */
	FEasySessionReplicatedSettings AppliedReplicatedSessionSettings;

	/** The first disconnect since the game last consumed one. Kept across map travel. */
	TOptional<FEasyDisconnectInfo> PendingDisconnectInfo;

	/** Ticker that waits for the session interface before binding the invite delegates. */
	FTSTicker::FDelegateHandle InviteBindTickerHandle;

	/** Ticker that broadcasts OnSessionPlayersChanged on the next tick, once per frame however many changes arrived. */
	FTSTicker::FDelegateHandle SessionPlayersChangedHandle;

	/** Ticker that watches Is Busy for the transitions no single call site sees, such as a travel ending. */
	FTSTicker::FDelegateHandle BusyTickerHandle;

	/** Delegate handle for engine-level network failures. Bound for the subsystem lifetime. */
	FDelegateHandle NetworkFailureHandle;

	/** Delegate handle for engine-level travel failures. Bound for the subsystem lifetime. */
	FDelegateHandle TravelFailureHandle;

	/** The session state OnSessionStateChanged last reported. */
	EEasySessionState LastReportedSessionState = EEasySessionState::NoSession;

	/** The IsBusy value OnBusyChanged last reported. */
	bool bLastReportedBusy = false;
};
