# API Reference

Compact reference of every public surface. Tooltips in the editor carry the same information.

## Async Blueprint nodes

All nodes: `OnSuccess` / `OnFailure` exec pins with `Result` (`EEasySessionResult`) and `ErrorMessage` (String). All operations are queued - never concurrent.

| Node | Inputs | Notes |
|---|---|---|
| **Create Easy Session** | `HostParams` | Creates + advertises; ensures listen server; travels if Map Name set |
| **Find Easy Sessions** | `SearchParams` | `OnSuccess` also carries `Results` array; results cached |
| **Join Easy Session** | `SearchResult`, `bTravelOnSuccess=true` | Validates host address before success; auto ClientTravel |
| **Start Easy Session** | - | Session authority only; Pending -> InProgress |
| **End Easy Session** | - | Session authority only; InProgress -> Ended |
| **Update Easy Session** | `NewHostParams` | Session authority only; Map Name / Host Mode ignored |
| **Destroy Easy Session** | - | Destroys (host) / leaves (client) |
| **Quick Match Easy Session** | `QuickMatchParams`, `PolicyClass` (optional) | Search -> join best -> host fallback |

> **Session authority only** means the game that created the session: the host player's
> game on a listen server, or the server itself on a dedicated server. Anyone else gets
> a `RequiresSessionAuthority` failure. `Server Travel To Map` needs it too and returns
> false without it. Gate these buttons with `Is Easy Session Host`.

## Structs

### FEasySessionHostParams
`SessionDisplayName` (String), `MapName` (String), `HostMode` (ListenServer/DedicatedServer), `MaxPlayers` (int), `bIsLANMatch`, `bStartListening`, `bShouldAdvertise`, `bAllowJoinInProgress`, `bAllowInvites`, `bUsePresence`, `CustomSettings` (Map String->String)

### FEasySessionSearchParams
`MaxResults` (int), `bLANQuery`, `TimeoutSeconds` (float), `MinOpenSlots` (int), `MaxPingMs` (int), `RequiredCustomSettings` (Map String->String)

### FEasySessionSearchResult *(read-only)*
`SessionDisplayName`, `HostName`, `PingInMs`, `MaxPlayers`, `OpenSlots`, `bIsDedicatedServer`, `CustomSettings`

### FEasyQuickMatchParams
`Search` (SearchParams), `Host` (HostParams), `bAllowHostFallback`, `MaxSearchPasses` (int), `DelayBetweenPassesSeconds` (float)

## Enums

**EEasySessionResult** - `Success`, `NoOnlineSubsystem`, `InvalidParams`, `SessionAlreadyExists`, `NoSessionExists`, `CreateFailure`, `SearchFailure`, `NoSessionsFound`, `MatchmakingAlreadyInProgress`, `JoinFailure`, `JoinSessionFull`, `JoinSessionDoesNotExist`, `ResolveFailure`, `DestroyFailure`, `UpdateFailure`, `StateChangeFailure`, `Canceled`, `UnknownFailure`, `Timeout`, `RequiresSessionAuthority`

**EEasyMatchmakingState** - `Idle`, `Searching`, `Joining`, `Hosting`, `Complete`

**EEasySessionHostMode** - `ListenServer`, `DedicatedServer`

## UEasySessionSubsystem

Get it with `Get Easy Session Subsystem` (BP) or `GetGameInstance()->GetSubsystem<UEasySessionSubsystem>()` (C++).

> **Which session do these answer about?** The game session - the one players find,
> join and play in. There is exactly one per process (see Limitations in the README),
> so nothing here takes a session argument. Should a later version add a second kind
> of session, such as a party that lives alongside the match, it will come with its
> own nodes rather than change the meaning of these: `Is In Session` will keep
> answering about the game session for as long as it exists.
>
> Some queries here are not about a session at all: `Is Busy` and `Get Queue Status
> Description` describe the operation queue, and `Is Matchmaking`, `Get Matchmaking
> State`, `Get Online Subsystem Name` and `Is Online Subsystem Available` describe
> the process. Those keep their meaning whatever sessions exist.

**Operations (C++, native delegates):** `CreateEasySession`, `FindEasySessions`, `JoinEasySession`, `DestroyEasySession`, `UpdateEasySession`, `StartQuickMatch`

**Callable:** `CancelMatchmaking()`, `ServerTravelToMap(MapName)` *(host only)*

**Pure:** `IsInSession`, `IsHost`, `IsBusy`, `IsMatchmaking`, `GetMatchmakingState`, `GetLastSearchResults`, `GetOnlineSubsystemName`, `IsOnlineSubsystemAvailable`, `GetActiveMatchmakingPolicy`

**Events (assignable):** `OnSessionCreated`, `OnSessionsFound`, `OnSessionJoined`, `OnSessionUpdated`, `OnSessionDestroyed`, `OnMatchmakingComplete`, `OnSessionFailure`

## UEasySessionStatics (Blueprint function library)

WorldContext-based shortcuts: `GetEasySessionSubsystem`, `IsInEasySession`, `IsEasySessionHost`, `IsEasySessionBusy`, `GetLastEasySearchResults`, `GetOnlineSubsystemName`, `ResultToString`

## UEasyMatchmakingPolicy

Subclassable (BP/C++). Override **`ScoreSession(Session) -> float`** (BlueprintNativeEvent, higher = better) for custom matchmaking criteria. Editable defaults: `PingBucketsMs` (default `[50, 100, 150]`), `TopCandidateRandomization` (default 3). Event: `OnStateChanged`.

## UEasySessionSettings (Project Settings -> Plugins -> EasySession)

`bAutoHostOnDedicatedServer` (default true), `DedicatedServerHostParams`

## Console commands *(development builds only)*

`EasySession.Host [Map]`, `EasySession.Find`, `EasySession.Join [Index]`, `EasySession.QuickMatch [Map]`, `EasySession.Travel <Map>`, `EasySession.Destroy`, `EasySession.Start`, `EasySession.End`, `EasySession.Cancel`, `EasySession.Status`, `EasySession.Friends`, `EasySession.Invites`, `EasySession.InviteUI`, `EasySession.Diagnose`
