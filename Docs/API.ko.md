# API 레퍼런스

*[English](API.en.md)*

모든 공개 API를 한 줄씩 정리했습니다. 에디터 툴팁에도 같은 내용이 들어 있으며,
이 문서는 노드를 하나씩 찾아보는 대신 전체를 한 번에 보기 위한 것입니다.

동작 설명은 각 가이드에 있습니다. 이 문서는 이름과 형태만 알려주고 링크로 넘깁니다.

**플러그인이 처음이신가요?** [Quick Start](QuickStart.ko.md)가 노드 세 개로 호스트와 참가자를
연결해줍니다. 나머지는 여기로 돌아와서 보세요.

**노드가 어디에 있는가.** 대부분은 블루프린트 그래프에서 우클릭해 이름을 검색하면 바로
나옵니다. 미리 준비할 것은 없습니다. 나머지는 서브시스템에서 호출합니다.
`Get Easy Session Subsystem` 노드를 놓고 그 출력 핀에서 선을 끌어내면 됩니다.
아래 표는 이 둘을 나눠 두었습니다.

**C++에서는** `GetGameInstance()->GetSubsystem<UEasySessionSubsystem>()`으로 서브시스템을
얻습니다. 같은 조회 기능을 아래 C++ 열의 더 짧은 이름으로 갖고 있습니다. 바로 쓰는 노드는
`UEasySessionStatics`의 함수로, 노드 이름에서 공백을 뺀 이름이며, 월드를 찾기 위해 액터나
위젯 아무거나 첫 인자로 받습니다.

1. [비동기 블루프린트 노드](#1-비동기-블루프린트-노드) - 만들기, 찾기, 참가, 퀵매치
2. [조회 블루프린트 노드](#2-조회-블루프린트-노드) - 세션 상태, 참가자, 검색 결과
3. [동작 블루프린트 노드](#3-동작-블루프린트-노드) - Travel, 취소, 초대
4. [이벤트](#4-이벤트) | 5. [구조체](#5-구조체) | 6. [열거형](#6-열거형)
7. [UEasyQuickMatchPolicy](#7-ueasyquickmatchpolicy) | 8. [UEasySessionSettings](#8-ueasysessionsettings-project-settings---plugins---easysession) | 9. [C++ 참고](#9-c-참고) | 10. [콘솔 명령](#10-콘솔-명령-개발-빌드-전용)

## 1. 비동기 블루프린트 노드

이 노드들은 작업을 [온라인 서브시스템](Concepts.ko.md)에 넘기고, 답은 나중에 돌아옵니다.
스팀처럼 인터넷 너머에 있는 서비스라면 수 초가 걸릴 수도 있습니다. 답이 늦게 오기 때문에 값을
바로 돌려주는 노드가 하나도 없습니다. 각 노드는 `OnSuccess` 또는 `OnFailure` 실행 핀으로 끝나며, 두
핀 모두 `Result`(`EEasySessionResult`)와 `ErrorMessage`(String)를 넘겨줍니다. 다만 서비스에
닿기도 전에 걸러지는 요청(온라인 서브시스템 없음, 성립할 수 없는 파라미터)은 그 자리에서
`OnFailure`로 끝납니다.

EasySession은 자기 작업을 하나씩 실행하므로, 버튼을 연타해도 오류 대신 순서대로 처리됩니다.
큐가 덮는 것은 EasySession을 거치는 호출까지이며, 엔진 자체 세션 노드는 따로 서비스에
도달합니다 ([FAQ](FAQ.ko.md)).

| 노드 | 입력 | 비고 |
|---|---|---|
| **Create Easy Session** | `HostParams` | `CreateSession` 호출. 넘긴 파라미터가 광고되는 `FOnlineSessionSettings`가 됩니다. 리슨 서버라면 이어서 Map Name으로 `?listen`을 붙여 Travel하므로 이 게임이 서버가 되고, Map Name이 비어 있으면 현재 맵에서 리슨을 시작합니다. 데디케이티드 서버는 실행된 맵을 그대로 유지합니다 |
| **Find Easy Sessions** | `SearchParams` | `FindSessions` 호출. 돌아온 결과를 캐시합니다. `OnSuccess`가 `Results` 배열을 넘기며, 숨김 세션은 제외됩니다 |
| **Join Easy Session** | `SearchResult`, `Password`, `AdditionalTravelOptions` | 호스트에게 승인을 먼저 물은 뒤 `JoinSession`을 호출하고, 호스트 주소를 해석해 이동합니다. 비밀번호가 틀리거나 매치가 닫혀 있으면 맵 로드 없이 `WrongPassword` / `JoinRefused`로 실패합니다. 호스트에게 물을 수 없었던 경우에만 거절이 늦게, `Rejected` 디스커넥트로 도착합니다 ([가이드](Guide-Sessions.ko.md)) |
| **Start Easy Session** | - | `StartSession` 호출. Pending -> InProgress. Allow Join In Progress가 꺼져 있다면 이 시점부터 새 플레이어를 받지 않습니다. 단 Steam은 첫 참가 시점부터 이미 받지 않습니다 ([FAQ](FAQ.ko.md)). 세션 권한 필요 |
| **End Easy Session** | - | `EndSession` 호출. InProgress -> Ended가 되어, 같은 세션에서 Start로 다음 매치를 돌릴 수 있습니다. 세션 권한 필요 |
| **Update Easy Session** | `NewHostParams` | `UpdateSession` 호출. 광고 중인 `FOnlineSessionSettings`를 다시 씁니다. 정원, 광고 여부, 난입 허용, 초대 허용, 표시 이름, 숨김, 비밀번호, 커스텀 데이터가 대상입니다. Map Name과 Host Mode는 무시됩니다. 세션 권한 필요 |
| **Destroy Easy Session** | - | `DestroySession` 호출. 호스트는 세션을 없애고 클라이언트는 나가기만 합니다. 호스트든 클라이언트든 직후에 다시 호스팅하거나 참가할 수 있습니다 |
| **Quick Match Easy Session** | `QuickMatchParams`, `PolicyClass`(선택) | 검색하고, 가장 좋은 결과에 참가하고, 없으면 직접 만듭니다. 위 세 노드를 대신 돌려주는 노드입니다 ([가이드](Guide-QuickMatch.ko.md)) |
| **Read Easy Friends** | - | `ReadFriendsList` 호출. `OnSuccess`가 `FEasySessionFriend` 배열을 넘깁니다. NULL/LAN에는 친구 개념이 없어 실패합니다 |

> **세션 권한 필요**는 그 세션을 만든 게임을 뜻합니다. 리슨 서버라면 호스트 플레이어의 게임,
> 데디케이티드 서버라면 서버 자신입니다. 그 외에는 `RequiresSessionAuthority` 실패를 받습니다.
> `Server Travel Easy Session`과 `Destroy Easy Session For Everyone`도 같은 권한이 필요합니다.
>
> `Is Easy Session Authority`가 이 권한이 있는지 답해줍니다. 플레이어가 실행하는 게임에서는
> `Is Easy Session Host`도 같은 답을 주므로, 메뉴 버튼을 비활성화할 때는 두 노드 중 무엇을 써도
> 됩니다. 데디케이티드 서버에는 호스트가 될 로컬 플레이어가 없어, 서버가 세션을 만들었는데도
> `Is Easy Session Host`가 false입니다. 그래서 데디케이티드 서버에서도 실행되는 로직에는
> `Is Easy Session Authority`를 써야 합니다.

## 2. 조회 블루프린트 노드

즉시 답을 주고 실행 핀이 없습니다. 아무것도 바꾸지 않으므로 매 프레임 호출해도, 위젯에 바로
바인딩해도 안전합니다.

### 2.1 바로 쓰는 노드 (`UEasySessionStatics`)

C++ 열은 static 함수의 이름이 아닙니다. 같은 답을 주는 서브시스템 메서드로, 서브시스템을 이미
들고 있다면 그쪽이 더 짧습니다. static 함수 이름은 노드 이름에서 공백을 뺀 것입니다.

| 노드 | C++ | 무엇을 답하는가 |
|---|---|---|
| Is In Easy Session | `IsInSession` | 세션에 들어가 있는가 |
| Is Easy Session Host | `IsHost` | 로컬 플레이어가 호스트인가. 로컬 플레이어가 없는 데디케이티드 서버에서는 false입니다 |
| Is Easy Session Authority | `IsSessionAuthority` | 지금 들어가 있는 세션을 이 게임이 만들었는가. 만들었다면 Start/End/Update/Travel/전체 종료를 할 수 있습니다. Is Easy Session Host와 달리 데디케이티드 서버에서도 true입니다 |
| Get Easy Session State | `GetSessionState` | 세션이 수명주기의 어디에 있는가. 클라이언트는 호스트가 복제한 값을 읽으므로 모든 플레이어가 같은 값을 봅니다 |
| Get Easy Session State Label | - | 같은 상태를 바로 표시할 수 있는 문자열로. 예: "In Match (InProgress)" |
| Is Easy Session Busy | `IsBusy` | 작업이나 그 뒤에 이어지는 레벨 로드가 진행 중인가. 버튼의 Is Enabled에 연결하세요 |
| Get Easy Session Display Name | `GetSessionDisplayName` | 세션이 광고되는 이름 |
| Get Easy Session Password | `GetSessionPassword` | 이 게임이 호스팅 중인 세션의 비밀번호. 호스트에게 보여주기 위한 것으로, **클라이언트에서는 빈 값**입니다. 비밀번호는 호스트를 떠나지 않습니다 |
| Get Easy Session Player Names | `GetSessionPlayerNames` | 세션에 있는 모두의 이름. 호스트와 클라이언트 양쪽에서 동작합니다 |
| Get Easy Session Player Infos | `GetSessionPlayerInfos` | 같은 목록에 호스트/로컬 플레이어 여부까지. 참가자 목록 UI용 |
| Get Easy Session Player Count | `GetSessionPlayerCount` | 지금 세션에 있는 플레이어 수 |
| Get Easy Session Max Players | `GetSessionMaxPlayers` | 정원. 세션이 없으면 0 |
| Get Last Easy Search Results | `GetLastSearchResults` | 마지막 검색 결과이며 어디서든 읽을 수 있습니다. 새 검색이 도는 동안에는 비어 있습니다 |
| Is Easy Quick Match Running | `IsQuickMatchRunning` | Quick Match가 돌고 있는가 |
| Get Easy Quick Match State | `GetQuickMatchState` | 어느 단계인가. Searching, Joining, Hosting, Complete |
| Has Pending Easy Disconnect Info | `HasPendingDisconnectInfo` | 읽지 않은 디스커넥트 사유가 있는가. 메뉴의 Event Construct에서 확인하세요 |
| Get Online Subsystem Name | `GetOnlineSubsystemName` | 어느 서비스가 동작 중인가. LAN이면 `NULL`, 그 외 `STEAM` 등 |
| Is Online Subsystem Available | `IsOnlineSubsystemAvailable` | 온라인 서브시스템이 올라와 있고 세션 인터페이스가 유효한가 |
| Get Easy Session Queue Status | `GetQueueStatusDescription` | 요청 큐가 무엇을 하고 있는지 문자열로. 상태 UI와 버그 리포트용 |
| Get Easy Session Host Params | `GetEasySessionHostParams` | 세션을 만들 때 쓴 파라미터. 한 필드만 바꿔 Update에 넘길 때 씁니다. 호스트 전용 |

`To String (EasySessionResult)`(C++ `ResultToString`)는 결과 열거형을 텍스트로 바꿉니다.

### 2.2 서브시스템에서 호출하는 노드 (`UEasySessionSubsystem`)

`Get Easy Session Subsystem`에서 호출합니다. 블루프린트와 C++ 이름이 같아 C++ 열이 없습니다.

| 노드 | 무엇을 답하는가 |
|---|---|
| Get Active Quick Match Policy | 정책 객체. `OnStateChanged` 바인딩에 씁니다 |

> **이 함수들은 어떤 세션에 대해 답하는가?** 플레이어가 찾고, 참가하고, 플레이하는 게임 세션입니다.
> 프로세스당 정확히 하나만 존재하므로(README의 제약 사항 참고) 세션을 인자로 받는 함수가 없습니다.
> 나중에 매치와 나란히 존재하는 파티 같은 두 번째 종류의 세션이 추가되더라도, 이 함수들의 의미를
> 바꾸는 대신 자체 노드를 함께 들여올 것입니다. `Is In Session`은 게임 세션이 존재하는 한 계속
> 게임 세션에 대해 답합니다.
>
> 여기 있는 것 중 일부는 애초에 세션에 대한 질문이 아닙니다. `Is Easy Session Busy`와
> `Get Queue Status Description`은 작업 큐를, `Is Easy Quick Match Running`,
> `Get Easy Quick Match State`, `Get Online Subsystem Name`, `Is Online Subsystem
> Available`은 프로세스를 설명합니다. 이들은 세션이 무엇이든 의미가 그대로입니다.

## 3. 동작 블루프린트 노드

비동기가 아니라 즉시 반환합니다. 상태를 바꾸고 실행 핀이 있습니다.

돌려주는 값은 "요청을 받았다"는 뜻이지 "끝났다"는 뜻이 아닙니다. `Cancel Easy Quick Match`는
진행 중이던 온라인 작업이 끝난 뒤에야 실제로 취소되고, `Server Travel Easy Session`은 맵이 로드되기
전에 돌아옵니다.

### 3.1 바로 쓰는 노드 (`UEasySessionStatics`)

2.1과 같은 규약입니다. C++ 열은 static 함수 이름이 아니라 서브시스템 메서드입니다.

| 노드 | C++ | 하는 일 |
|---|---|---|
| Consume Last Easy Disconnect Info | `ConsumeLastDisconnectInfo` | 디스커넥트 사유를 읽고 비웁니다. 맵 Travel을 넘어 보존되므로 메뉴에서 읽을 수 있습니다 |
| Cancel Easy Quick Match | `CancelQuickMatch` | 진행 중인 Quick Match를 `Canceled`로 끝냅니다 |
| Send Easy Session Invite To Friend | `SendSessionInviteToFriend` | 플랫폼 초대 |
| Show Easy Invite UI | `ShowInviteUI` | 플랫폼 초대 오버레이 |
| Show Easy Profile UI | `ShowProfileUI` | 친구의 프로필 오버레이 |
| Show Easy Profile UI For Player | `ShowProfileUIForPlayer` | 세션에 있는 사람의 프로필 오버레이 |
| Server Travel Easy Session | `ServerTravelEasySession` | 세션 전체를 새 맵으로 옮깁니다. 세션 권한 필요 |
| Destroy Easy Session For Everyone | `DestroyEasySessionForEveryone` | 세션을 끝내고 모든 클라이언트를 사유와 함께 메뉴로 돌려보냅니다. 세션 권한 필요 |

초대와 프로필 노드는 플랫폼 서비스가 필요합니다. NULL/LAN에서는 false를 반환합니다.

마지막 두 노드는 세션 권한을 스스로 확인합니다. 클라이언트가 호출하면 아무것도 바뀌지 않고 로그에 경고만 남으므로, 호출이 무해하다는 데 기대지 말고 `Is Easy Session Authority`로 버튼을 막으세요.

## 4. 이벤트

서브시스템에 바인딩합니다. 누가 그 작업을 시작했든 발화하므로, 게임의 다른 코드가 세션을
움직여도 여기에 묶인 UI는 계속 맞는 값을 보여줍니다.

| 이벤트 | 넘기는 값 | 언제 발화하는가 |
|---|---|---|
| `OnSessionCreated` | `Result`, `ErrorMessage` | Create Easy Session이 끝났을 때 |
| `OnSessionsFound` | `Result`, `ErrorMessage`, `Results` | Find Easy Sessions가 끝났을 때. 검색 결과를 넘기는 유일한 이벤트입니다 |
| `OnSessionJoined` | `Result`, `ErrorMessage` | Join Easy Session이 끝났을 때 |
| `OnSessionStarted` | `Result`, `ErrorMessage` | Start Easy Session이 끝났을 때. 매치가 진행 중이 됩니다 |
| `OnSessionEnded` | `Result`, `ErrorMessage` | End Easy Session이 끝났을 때. 매치만 끝나고 세션은 남습니다 |
| `OnSessionUpdated` | `Result`, `ErrorMessage` | Update Easy Session이 끝났을 때 |
| `OnSessionDestroyed` | `Result`, `ErrorMessage` | Destroy Easy Session이 끝났을 때. 호스트든 나가는 클라이언트든 똑같이 발화합니다 |
| `OnQuickMatchComplete` | `Result`, `ErrorMessage` | Quick Match 한 번이 끝났을 때. 참가했든 호스트가 됐든 발화하므로, 어느 쪽인지는 `Is Easy Session Host`로 확인합니다 |
| `OnSessionFailure` | `Reason`(String) | 작업이 끝난 것이 아닙니다. 연결이 끊기거나 네트워크 오류가 났을 때이며, 죽은 세션은 알아서 정리됩니다 |
| `OnSessionInviteAccepted` | `Session`(`FEasySessionSearchResult`) | 플랫폼 오버레이에서 초대를 수락했을 때. Auto Join Accepted Invites가 켜져 있으면 참가가 이어서 진행됩니다. 단 이미 세션에 있다면 `bAcceptInvitesWhileInSession`이 켜져 있어야 합니다 |

`Result`와 `ErrorMessage`는 해당 노드의 출력 핀으로 받았을 값과 같습니다.

## 5. 구조체

### 5.1 FEasySessionHostParams
`SessionDisplayName`(String), `MapName`(String), `HostMode`(`EEasySessionHostMode`), `MaxPlayers`(int), `bIsLANMatch`, `bStartListening`, `bShouldAdvertise`, `bHidden`, `Password`(String), `bFriendsBypassPassword`, `AdditionalTravelOptions`(String), `bAllowJoinInProgress`, `bAllowInvites`, `bUsePresence`, `CustomSettings`(Map String->String)

각 필드의 동작은 [세션 가이드](Guide-Sessions.ko.md)에 있습니다. `bHidden`은 세션을 광고하되
Find 결과에서는 빼므로, 초대로만 들어올 수 있게 됩니다. `Password`와 `bFriendsBypassPassword`는
[같은 가이드의 비밀번호 절](Guide-Sessions.ko.md#비밀번호로-잠근-세션)에서 다룹니다.
`AdditionalTravelOptions`는 호스트의 Travel URL 뒤에
붙으며(예: `GameMode=Deathmatch?MyOption=1`), 서버에서 `Parse Option`으로 읽습니다.

### 5.2 FEasySessionSearchParams
`MaxResults`(int), `bLANQuery`, `TimeoutSeconds`(float), `MinOpenSlots`(int), `MaxPingMs`(int), `RequiredCustomSettings`(Map String->String)

### 5.3 FEasySessionSearchResult *(읽기 전용)*
`SessionDisplayName`, `HostName`, `PingInMs`, `MaxPlayers`, `OpenSlots`, `bIsDedicatedServer`, `bPasswordProtected`, `CustomSettings`

두 노드를 잇는 구조체입니다. `Find Easy Sessions`가 돌려주고 `Join Easy Session`이 받습니다.
구조체를 통째로 들고 계세요. 서버 브라우저의 각 행은 표시할 이름만이 아니라 이 구조체를
저장해야 합니다.

`bPasswordProtected`는 `Join Easy Session` 전에 비밀번호를 물어볼지 판단하는 근거입니다.

### 5.4 FEasyQuickMatchParams
`Search`(SearchParams), `Host`(HostParams - **Map Name 필수**), `bAllowHostFallback`, `MaxSearchPasses`(int), `DelayBetweenPassesSeconds`(float)

### 5.5 FEasySessionPlayerInfo *(읽기 전용)*
`PlayerName`, `bIsLocalPlayer`, `bIsHost`(데디케이티드 서버에서는 항상 false), `PlayerId`(온라인 서비스의 플레이어 id - 이름은 겹칠 수 있지만 이것은 겹치지 않습니다)

### 5.6 FEasySessionFriend *(읽기 전용)*
`DisplayName`, `bIsOnline`, `bIsPlayingThisGame`

`Read Easy Friends`가 돌려주며, 초대와 프로필 함수에 그대로 넘기면 됩니다.

### 5.7 FEasyDisconnectInfo *(읽기 전용)*
`Reason`(`EEasyDisconnectReason`), `ReasonText`(Text)

## 6. 열거형

### 6.1 EEasySessionResult

모든 노드의 `Result` 핀입니다. 분기할 만한 값에 표시했습니다.

| 값 | 뜻 |
|---|---|
| `Success` | 성공 |
| **`SessionAlreadyExists`** | 이미 세션에 들어가 있습니다. `Destroy Easy Session`을 먼저 부르세요 |
| **`NoSessionExists`** | 대상이 될 세션이 없습니다 |
| **`NoSessionsFound`** | 검색은 정상이었고 결과가 없었습니다. 오류가 아니므로 직접 호스팅을 권하면 됩니다 |
| **`JoinSessionFull`** | 검색과 참가 사이에 방이 찼습니다 |
| **`JoinSessionDoesNotExist`** | 참가 시점에 방이 사라졌습니다. 다시 검색하세요 |
| **`WrongPassword`** | 호스트가 거절했습니다: 비밀번호가 맞지 않습니다. 다시 입력받으세요 |
| **`JoinRefused`** | 호스트가 다른 이유로 거절했습니다. 예: 더 이상 플레이어를 받지 않는 매치. `ErrorMessage`는 호스트가 쓴 문장이라 그대로 보여줘도 됩니다 |
| **`ResolveFailure`** | 참가는 됐지만 호스트 주소가 동작하지 않습니다. 대개 호스트가 리슨 서버가 되지 못한 경우입니다 ([FAQ](FAQ.ko.md)) |
| **`RequiresSessionAuthority`** | 그 세션을 만든 게임만 할 수 있는 일입니다. `Is Easy Session Authority`가 true일 때만 버튼을 보여주세요 |
| **`Timeout`** | 온라인 서비스가 끝내 답하지 않았습니다. 결과를 알 수 없으므로 남은 것이 있으면 정리됩니다. `RequestTimeoutSeconds` 참고 |
| **`Canceled`** | `Cancel Easy Quick Match`가 Quick Match를 중단시켰습니다 |
| `NoOnlineSubsystem` | 온라인 서브시스템이 없습니다. `DefaultEngine.ini`를 확인하세요 |
| `InvalidParams` | 성립할 수 없는 파라미터입니다. 예: 폴백 Map Name 없는 Quick Match |
| `QuickMatchAlreadyInProgress` | Quick Match가 이미 돌고 있습니다 |
| `CreateFailure`, `SearchFailure`, `JoinFailure`, `DestroyFailure`, `UpdateFailure`, `StateChangeFailure` | 온라인 서비스가 그 호출을 거절했습니다. 서비스가 한 말은 `ErrorMessage`에 담깁니다 |
| `UnknownFailure` | 더 구체적인 사유가 없었습니다 |

### 6.2 EEasySessionState

`NoSession`, `Creating`, `Pending`, `Starting`, `InProgress`, `Ending`, `Ended`, `Destroying`

`Pending`은 아직 시작하지 않은 세션이고, `Ended`는 끝난 매치로 `Start Easy Session`으로 다시 플레이할 수 있습니다. `-ing`으로 끝나는 값들은 해당 작업이 진행 중인 순간입니다.

### 6.3 EEasyDisconnectReason

`Consume Last Easy Disconnect Info`로 읽습니다. `Reason`으로 분기하고 `ReasonText`를 보여주세요.

| 값 | 뜻 |
|---|---|
| `None` | 기록된 것이 없습니다 |
| `ConnectionLost` | 연결이 죽었습니다. 호스트가 나갔거나, 튕겼거나, 네트워크가 끊겼습니다 |
| `HostDestroyedSession` | 호스트가 `Destroy Easy Session For Everyone`으로 모두를 돌려보냈습니다 |
| `TravelFailure` | 세션의 맵을 로드하지 못했습니다 |
| `Rejected` | 호스트가 사유를 대며 접속을 거절했습니다. 비밀번호 불일치, 더 이상 받지 않는 매치 등이며 `ReasonText`가 호스트가 쓴 문장이라 그대로 보여줘도 됩니다 |

### 6.4 EEasyQuickMatchState

`Idle`, `Searching`, `Joining`, `Hosting`, `Complete` - Quick Match 한 번의 진행 단계이며 `OnStateChanged`로 알려줍니다.

### 6.5 EEasySessionHostMode

`ListenServer`(호스트 플레이어의 게임이 곧 서버) 또는 `DedicatedServer` ([가이드](Guide-DedicatedServer.md)).

## 7. UEasyQuickMatchPolicy

`Quick Match Easy Session` 뒤에서 실제로 일하는 객체입니다. 검색하고, 찾은 것 중 가장 좋은 방에 참가하고, 없으면 직접 호스트가 됩니다.

블루프린트나 C++로 서브클래스를 만들고, 매치메이킹 기준을 바꾸려면
**`ScoreSession(Session) -> float`**(값이 클수록 먼저 참가)만 오버라이드하면 됩니다.
편집 가능한 기본값은 `PingBucketsMs`(기본 `[50, 100, 150]`), `TopCandidateRandomization`(기본 3)입니다.
상태는 `GetState`로 조회하거나 `OnStateChanged`에 바인딩하세요.

## 8. UEasySessionSettings (Project Settings -> Plugins -> EasySession)

| 설정 | 기본값 | 효과 |
|---|---|---|
| `bAutoReturnToMenuOnDisconnect` | true | 접속이 끊기거나 Travel이 실패하면 세션을 정리하고 프로젝트의 **Game Default Map**으로 이동하며, 그 맵이 읽을 수 있도록 사유를 남깁니다. 끄면 플레이어를 그 자리에 둡니다 |
| `bAutoJoinAcceptedInvites` | true | 플랫폼 초대를 수락하면 그 세션에 바로 참가합니다. 끄면 `OnSessionInviteAccepted`만 받습니다 |
| `bAcceptInvitesWhileInSession` | false | 초대를 수락하면 지금 있는 세션을 파괴하고 초대받은 세션에 참가합니다. 오버레이의 클릭 한 번으로 진행 중인 매치가 끝나지 않도록 기본값은 꺼짐입니다. `OnSessionInviteAccepted`는 그대로 발생하므로 먼저 물어볼 수 있습니다 |
| `RequestTimeoutSeconds` | 30 | 요청이 온라인 서비스를 기다리다 `Timeout`으로 실패하기까지의 시간. **0이면 무한히 기다립니다.** 검색은 자기 Timeout Seconds를 이 값 위에 더합니다 |
| `bAutoHostOnDedicatedServer` | true | 데디케이티드 서버가 맵을 띄우면 스스로를 광고합니다 |
| `DedicatedServerHostParams` | - | 위 자동 호스팅이 쓰는 파라미터. Map Name은 무시되고 서버가 실행된 맵을 유지합니다 |

## 9. C++ 참고

작업 함수들은 델리게이트 콜백과 함께 네이티브에서 호출할 수 있습니다. `CreateEasySession`,
`FindEasySessions`, `JoinEasySession`, `DestroyEasySession`, `UpdateEasySession`,
`StartQuickMatch`. 블루프린트와 C++은 같은 코드 경로를 지납니다.

`OnModifyServerTravelURL`과 `OnModifyClientTravelURL`은 서브시스템의 C++ 전용 델리게이트입니다.
Travel 직전에 URL을 넘겨주므로 원하는 옵션을 덧붙일 수 있습니다. 고정된 문자열로 표현할 수 있는
것이라면 `AdditionalTravelOptions` 쪽이 낫습니다.

## 10. 콘솔 명령 *(개발 빌드 전용)*

`EasySession.Host [Map]`, `EasySession.Find`, `EasySession.Join [Index] [Password]`, `EasySession.QuickMatch [Map]`, `EasySession.Travel <Map>`, `EasySession.Destroy`, `EasySession.Start`, `EasySession.End`, `EasySession.Cancel`, `EasySession.Status`, `EasySession.Friends`, `EasySession.InviteUI`, `EasySession.Diagnose`
