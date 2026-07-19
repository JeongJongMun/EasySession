# API Reference

Compact reference of every public surface. Tooltips in the editor carry the same information.

## Async Blueprint nodes

All nodes: `OnSuccess` / `OnFailure` exec pins with `Result` (`EEasySessionResult`) and `ErrorMessage` (String). All operations are queued — never concurrent.

| Node | Inputs | Notes |
|---|---|---|
| **Create Easy Session** | `HostParams` | Creates + advertises; ensures listen server; travels if Map Name set |
| **Find Easy Sessions** | `SearchParams` | `OnSuccess` also carries `Results` array; results cached |
| **Join Easy Session** | `SearchResult`, `bTravelOnSuccess=true` | Validates host address before success; auto ClientTravel |
| **Update Easy Session** | `NewHostParams` | Host only; Map Name / Host Mode ignored |
| **Leave Easy Session** | — | Destroys (host) / leaves (client) |
| **Quick Play Easy Session** | `QuickPlayParams`, `PolicyClass` (optional) | Search → join best → host fallback |

## Structs

### FEasySessionHostParams
`SessionDisplayName` (String) · `MapName` (String) · `HostMode` (ListenServer/DedicatedServer) · `MaxPlayers` (int) · `bIsLANMatch` · `bStartListening` · `bShouldAdvertise` · `bAllowJoinInProgress` · `bAllowInvites` · `bUsePresence` · `CustomSettings` (Map String→String)

### FEasySessionSearchParams
`MaxResults` (int) · `bLANQuery` · `TimeoutSeconds` (float) · `MinOpenSlots` (int) · `MaxPingMs` (int) · `RequiredCustomSettings` (Map String→String)

### FEasySessionSearchResult *(read-only)*
`SessionDisplayName` · `HostName` · `PingInMs` · `MaxPlayers` · `OpenSlots` · `bIsDedicatedServer` · `CustomSettings`

### FEasyQuickPlayParams
`Search` (SearchParams) · `Host` (HostParams) · `bAllowHostFallback` · `MaxSearchPasses` (int) · `DelayBetweenPassesSeconds` (float)

## Enums

**EEasySessionResult** — `Success`, `NoOnlineSubsystem`, `InvalidParams`, `SessionAlreadyExists`, `NoSessionExists`, `CreateFailure`, `SearchFailure`, `NoSessionsFound`, `MatchmakingAlreadyInProgress`, `JoinFailure`, `JoinSessionFull`, `JoinSessionDoesNotExist`, `ResolveFailure`, `DestroyFailure`, `UpdateFailure`, `StateChangeFailure`, `Canceled`, `UnknownFailure`

**EEasyMatchmakingState** — `Idle`, `Searching`, `Joining`, `Hosting`, `Complete`

**EEasySessionHostMode** — `ListenServer`, `DedicatedServer`

## UEasySessionSubsystem

Get it with `Get Easy Session Subsystem` (BP) or `GetGameInstance()->GetSubsystem<UEasySessionSubsystem>()` (C++).

**Operations (C++, native delegates):** `CreateEasySession` · `FindEasySessions` · `JoinEasySession` · `DestroyEasySession` · `UpdateEasySession` · `StartQuickPlay`

**Callable:** `CancelMatchmaking()` · `ServerTravelToMap(MapName)` *(host only)*

**Pure:** `IsInSession` · `IsHost` · `IsBusy` · `IsMatchmaking` · `GetMatchmakingState` · `GetLastSearchResults` · `GetOnlineSubsystemName` · `IsOnlineSubsystemAvailable` · `GetActiveMatchmakingPolicy`

**Events (assignable):** `OnSessionCreated` · `OnSessionsFound` · `OnSessionJoined` · `OnSessionUpdated` · `OnSessionDestroyed` · `OnMatchmakingComplete` · `OnSessionFailure`

## UEasySessionStatics (Blueprint function library)

WorldContext-based shortcuts: `GetEasySessionSubsystem` · `IsInEasySession` · `IsEasySessionHost` · `IsEasySessionBusy` · `GetLastEasySearchResults` · `GetOnlineSubsystemName` · `ResultToString`

## UEasyMatchmakingPolicy

Subclassable (BP/C++). Override **`ScoreSession(Session) → float`** (BlueprintNativeEvent, higher = better) for custom matchmaking criteria. Editable defaults: `PingBucketsMs` (default `[50, 100, 150]`), `TopCandidateRandomization` (default 3). Event: `OnStateChanged`.

## UEasySessionSettings (Project Settings → Plugins → EasySession)

`bAutoHostOnDedicatedServer` (default true) · `DedicatedServerHostParams`

## Console commands *(development builds only)*

`EasySession.Host [Map]` · `EasySession.Find` · `EasySession.Join [Index]` · `EasySession.QuickPlay [Map]` · `EasySession.Travel <Map>` · `EasySession.Leave` · `EasySession.Cancel` · `EasySession.Status`
