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
7. [UEasyMatchmakingPolicy](#7-ueasymatchmakingpolicy) | 8. [UEasySessionConfig](#8-ueasysessionconfig-project-settings---plugins---easysession) | 9. [C++ 참고](#9-c-참고) | 10. [콘솔 명령](#10-콘솔-명령-개발-빌드-전용)

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
| **Update Easy Session** | `NewSettings` | `UpdateSession` 호출. `FEasySessionSettings`로 광고 중인 `FOnlineSessionSettings`를 다시 씁니다. 이 구조체는 살아있는 세션이 바꿀 수 있는 필드만 들고 있어서, 무시되는 값이 없습니다. 세션 권한 필요 |
| **Destroy Easy Session** | - | `DestroySession` 호출. 이 게임의 네임드 세션만 지우고 맵에는 그대로 남습니다. 호스트든 클라이언트든 직후에 다시 호스팅하거나 참가할 수 있습니다 |
| **Leave Easy Session** | - | Destroy Easy Session에 귀갓길까지. 네임드 세션을 지운 뒤 메뉴 맵(Game Default Map)으로 돌아갑니다. 호스트가 부르면 모두에게 "The host has left the game."을 보내고 방을 닫습니다 |
| **Start Easy Matchmaking** | `MatchmakingParams`, `PolicyClass`(선택) | 검색하고, 가장 좋은 결과에 참가하고, 없으면 직접 만듭니다. 위 세 노드를 대신 돌려주는 노드입니다 ([가이드](Guide-Matchmaking.ko.md)) |
| **Read Easy Friends** | - | `ReadFriendsList` 호출. `OnSuccess`가 `FEasySessionFriend` 배열을 표시용 순서로 넘깁니다: 이 게임 플레이 중, 온라인, 오프라인 순이고 같은 그룹 안에서는 이름순. NULL/LAN에는 친구 개념이 없어 실패합니다 |
| **Find Easy Friend Sessions** | - | 친구 목록을 읽은 뒤, 이 게임을 플레이 중인 친구마다 `FindFriendSession`을 호출합니다. `OnSuccess`가 `FEasyFriendSession` 배열을 넘깁니다. 모든 친구가 나열되고, 참가 가능한 세션에 있는 친구는 그 세션을 들고 맨 위로 정렬됩니다. NULL/LAN에서는 실패합니다 |

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
| Get Easy Session Activity | `GetActivity` | 지금 어떤 작업이 도는지: Creating, Searching, Joining, Leaving, Updating, Starting, Ending, Matchmaking, Traveling. Is Easy Session Busy가 false일 때만 None. 메뉴가 시작하지 않은 초대 참가나 복구도 이름을 붙입니다 |
| Get Easy Session Display Name | `GetSessionDisplayName` | 세션이 광고되는 이름 |
| Get Easy Session Password | `GetSessionPassword` | 이 게임이 호스팅 중인 세션의 비밀번호. 호스트에게 보여주기 위한 것으로, **클라이언트에서는 빈 값**입니다. 비밀번호는 호스트를 떠나지 않습니다 |
| Get Easy Session Player Names | `GetSessionPlayerNames` | 세션에 있는 모두의 이름. 호스트와 클라이언트 양쪽에서 동작합니다 |
| Get Easy Session Player Infos | `GetSessionPlayerInfos` | 같은 목록에 호스트/로컬 플레이어 여부까지. 참가자 목록 UI용 |
| Get Easy Session Player Count | `GetSessionPlayerCount` | 지금 세션에 있는 플레이어 수 |
| Get Easy Session Max Players | `GetSessionMaxPlayers` | 정원. 세션이 없으면 0 |
| Get Last Easy Search Results | `GetLastSearchResults` | 마지막 검색 결과이며 어디서든 읽을 수 있습니다. 새 검색이 도는 동안에는 비어 있습니다 |
| Is Easy Matchmaking Running | `IsMatchmakingRunning` | Matchmaking가 돌고 있는가 |
| Get Easy Matchmaking State | `GetMatchmakingState` | 어느 단계인가. Searching, Joining, Hosting, Complete |
| Has Pending Easy Disconnect Info | `HasPendingDisconnectInfo` | 읽지 않은 디스커넥트 사유가 있는가. 메뉴의 Event Construct에서 확인하세요 |
| Get Online Subsystem Name (EasySession) | `GetOnlineSubsystemName` | 어느 서비스가 동작 중인가. LAN이면 `NULL`, 그 외 `STEAM` 등 |
| Is Online Subsystem Available (EasySession) | `IsOnlineSubsystemAvailable` | 온라인 서브시스템이 올라와 있고 세션 인터페이스가 유효한가 |
| Get Easy Session Queue Status | `GetQueueStatus` | 요청 큐가 무엇을 하고 있는지 문자열로. 상태 UI와 버그 리포트용 |
| Get Easy Session Settings | `GetSessionSettings` | 세션이 광고 중인 설정. 한 필드만 바꿔 Update에 넘길 때 씁니다. 멤버 누구나 읽을 수 있고, 비밀번호만 호스트에서만 채워집니다 |
| Get Easy Session Join Code | `GetSessionJoinCode` | 세션이 광고 중인 참가 코드. 없으면 빈 문자열입니다. 방에 있는 누구나 읽고 공유할 수 있습니다 |

`To String (EasySessionResult)`(C++ `ResultToString`)는 결과 열거형을 텍스트로 바꿉니다.

### 2.2 서브시스템에서 호출하는 노드 (`UEasySessionSubsystem`)

`Get Easy Session Subsystem`에서 호출합니다. 블루프린트와 C++ 이름이 같아 C++ 열이 없습니다.

| 노드 | 무엇을 답하는가 |
|---|---|
| Get Active Matchmaking Policy | 실행 중인 정책 객체. 진행 상황은 서브시스템의 이벤트로도 릴레이되므로, 정책 없이도 받을 수 있습니다 |

> **이 함수들은 어떤 세션에 대해 답하는가?** 플레이어가 찾고, 참가하고, 플레이하는 게임 세션입니다.
> 프로세스당 정확히 하나만 존재하므로(README의 제약 사항 참고) 세션을 인자로 받는 함수가 없습니다.
> 나중에 매치와 나란히 존재하는 파티 같은 두 번째 종류의 세션이 추가되더라도, 이 함수들의 의미를
> 바꾸는 대신 자체 노드를 함께 들여올 것입니다. `Is In Session`은 게임 세션이 존재하는 한 계속
> 게임 세션에 대해 답합니다.
>
> 여기 있는 것 중 일부는 애초에 세션에 대한 질문이 아닙니다. `Is Easy Session Busy`와
> `Get Easy Session Queue Status`는 작업 큐를, `Is Easy Matchmaking Running`,
> `Get Easy Matchmaking State`, `Get Online Subsystem Name (EasySession)`,
> `Is Online Subsystem Available (EasySession)`은 프로세스를 설명합니다. 이들은 세션이 무엇이든 의미가 그대로입니다.

### 2.3 UI 텍스트 헬퍼 (`UEasySessionUIStatics`)

세션 UI가 보여줄 텍스트를 만드는 순수 함수들입니다. 세션 상태를 건드리지 않으므로 메뉴가 문자열을 직접 조립할 필요가 없습니다.

| 노드 | C++ | 반환 |
|---|---|---|
| Get Result Message | `GetResultMessage` | 결과를 플레이어에게 보여줄 한 문장으로. 예: "The session is full". Success는 "Done" |
| Get Activity Message | `GetActivityMessage` | Get Easy Session Activity 값을 "Creating the session..." 같은 상태 줄로. None은 빈 텍스트라 상태 줄을 지우는 데 그대로 씁니다 |
| Format Matchmaking Status | `FormatMatchmakingStatus` | 매치메이킹 상태와 경과 초로 "Searching... 12s" 같은 상태 줄. Idle은 "Ready" |
| Format Session Slots | `FormatSessionSlots` | 검색 결과의 "1/4   ping 32ms" |
| Get Region Display Name | `GetRegionDisplayName` | 리전의 표시명. 예: "North America East" |
| Get Region Options | `GetRegionOptions` | 모든 리전의 표시명을 열거형 순서대로. 콤보 박스용 |
| Region From Index | `RegionFromIndex` | 콤보 박스 인덱스의 리전. 범위를 벗어나면 Any |

## 3. 동작 블루프린트 노드

비동기가 아니라 즉시 반환합니다. 상태를 바꾸고 실행 핀이 있습니다.

돌려주는 값은 "요청을 받았다"는 뜻이지 "끝났다"는 뜻이 아닙니다. `Cancel Easy Matchmaking`는
진행 중이던 온라인 작업이 끝난 뒤에야 실제로 취소되고, `Server Travel Easy Session`은 맵이 로드되기
전에 돌아옵니다.

### 3.1 바로 쓰는 노드 (`UEasySessionStatics`)

2.1과 같은 규약입니다. C++ 열은 static 함수 이름이 아니라 서브시스템 메서드입니다.

| 노드 | C++ | 하는 일 |
|---|---|---|
| Consume Last Easy Disconnect Info | `ConsumeLastDisconnectInfo` | 디스커넥트 사유를 읽고 비웁니다. 맵 Travel을 넘어 보존되므로 메뉴에서 읽을 수 있습니다 |
| Cancel Easy Matchmaking | `CancelMatchmaking` | 진행 중인 Matchmaking를 `Canceled`로 끝냅니다. 이미 성사되던 참가나 생성은 되돌려집니다 |
| Send Easy Session Invite To Friend | `SendSessionInviteToFriend` | 플랫폼 초대 |
| Show Easy Invite UI | `ShowInviteUI` | 플랫폼 초대 오버레이 |
| Show Easy Profile UI | `ShowProfileUI` | 친구의 프로필 오버레이 |
| Show Easy Profile UI For Player | `ShowProfileUIForPlayer` | 세션에 있는 사람의 프로필 오버레이 |
| Server Travel Easy Session | `ServerTravelToMap` | 세션 전체를 새 맵으로 옮깁니다. 세션 권한 필요 |
| Destroy Easy Session For Everyone | `DestroyEasySessionForEveryone` | 세션을 끝내고 모든 클라이언트를 사유와 함께 메뉴로 돌려보냅니다. 세션 권한 필요 |

초대와 프로필 노드는 플랫폼 서비스가 필요합니다. NULL/LAN에서는 false를 반환합니다.

마지막 두 노드는 세션 권한을 스스로 확인합니다. 클라이언트가 호출하면 아무것도 바뀌지 않고 로그에 경고만 남으므로, 호출이 무해하다는 데 기대지 말고 `Is Easy Session Authority`가 true일 때만 버튼을 보여주세요.

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
| `OnSessionSettingsChanged` | - | 호스트가 바꾼 설정이 클라이언트에 도착했을 때. 이미 일반 게터가 새 값을 돌려주는 상태이니, 게터로 UI만 갱신하면 됩니다 |
| `OnSessionDestroyed` | `Result`, `ErrorMessage` | Destroy Easy Session이 끝났을 때. 호스트든 나가는 클라이언트든 똑같이 발화합니다 |
| `OnMatchmakingStarted` | - | Matchmaking 실행이 받아들여지고 정책이 등록됐을 때. 한 실행의 이벤트 중 언제나 첫 번째입니다 |
| `OnMatchmakingStateChanged` | `OldState`, `NewState` | Matchmaking 상태가 바뀌었을 때 (`Searching`, `Joining`, `Hosting`, `Complete`) |
| `OnMatchmakingUpdated` | `State`, `ElapsedSeconds` | Matchmaking 상태가 바뀔 때 + 실행 중 1초마다. 경과 시간 표시를 만드는 이벤트입니다 |
| `OnMatchmakingComplete` | `Result`, `ErrorMessage` | Matchmaking 한 번이 끝났을 때. 참가했든, 호스트가 됐든, 취소됐든(`Result` = `Canceled`) 발화합니다. 어느 쪽인지는 `Is Easy Session Host`로 확인합니다 |
| `OnSessionFailure` | `Reason`(String) | 작업이 끝난 것이 아닙니다. 연결이 끊기거나 네트워크 오류가 났을 때이며, 죽은 세션은 알아서 정리됩니다 |
| `OnBusyChanged` | `bBusy` | Is Easy Session Busy가 바뀌었을 때. 한 번 바인딩해 두고 이 플래그로 세션 버튼을 켜고 끄면 매 틱 폴링이 필요 없습니다. true로 바뀐 순간 Get Easy Session Activity가 어떤 작업이 시작됐는지 알려줍니다 |
| `OnSessionInviteAccepted` | `Session`(`FEasySessionSearchResult`) | 플랫폼 오버레이에서 초대를 수락했을 때. Auto Join Accepted Invites가 켜져 있으면 참가가 이어서 진행됩니다. 단 이미 세션에 있다면 `bAcceptInvitesWhileInSession`이 켜져 있어야 합니다 |

`Result`와 `ErrorMessage`는 해당 노드의 출력 핀으로 받았을 값과 같습니다.

## 5. 구조체

### 5.1 FEasySessionSettings
`SessionDisplayName`(String), `MaxPlayers`(int), `bShouldAdvertise`, `bHidden`, `Password`(String), `bFriendsBypassPassword`, `bAllowJoinInProgress`, `bAllowInvites`, `Region`(`EEasySessionRegion`), `bUseJoinCode`, `CustomSettings`(Map String->String)

세션이 자기 자신에 대해 광고하는 값들입니다. `Update Easy Session`이 받는 구조체가 정확히
이것이라, 여기 있는 필드는 전부 살아있는 세션이 바꿀 수 있습니다.

### 5.2 FEasySessionHostParams *(FEasySessionSettings에 더해서)*
`MapName`(String), `HostMode`(`EEasySessionHostMode`), `bIsLANMatch`, `bStartListening`, `bUsePresence`, `AdditionalTravelOptions`(String)

호스팅은 위 설정에 서버를 띄우는 방법을 더한 것입니다. 여기 더해진 필드들은 세션을 만들 때
한 번만 읽히고, 그래서 Update가 바꿀 수 없습니다.

각 필드의 동작은 [세션 가이드](Guide-Sessions.ko.md)에 있습니다. `bHidden`은 세션을 광고하되
Find 결과에서는 빼므로, 초대로만 들어올 수 있게 됩니다. `Password`와 `bFriendsBypassPassword`는
[같은 가이드의 비밀번호 절](Guide-Sessions.ko.md#비밀번호로-잠근-세션)에서 다룹니다.
`AdditionalTravelOptions`는 호스트의 Travel URL 뒤에
붙으며(예: `GameMode=Deathmatch?MyOption=1`), 서버에서 `Parse Option`으로 읽습니다.
`Region`과 `bUseJoinCode`는 [세션 가이드](Guide-Sessions.ko.md)의 지역 절과 참가 코드 절에서 다룹니다.

### 5.3 FEasySessionSearchParams
`MaxResults`(int), `bLANQuery`, `TimeoutSeconds`(float), `MinOpenSlots`(int), `MaxPingMs`(int), `RequiredCustomSettings`(Map String->String), `Region`(`EEasySessionRegion`), `bIncludeInProgressSessions`, `JoinCode`(String), `SearchMode`(`EEasySessionSearchMode`), `SearchTargetId`(Unique Net Id), `OwnerId`(Unique Net Id)

이 중 넷은 무엇을 찾을지 묘사하는 대신 특정 세션 하나를 지목합니다. `JoinCode`와 `OwnerId`는 일반 검색 위의 필터라 위의 모든 값과 조합됩니다. `SearchMode`는 서비스에 다른 호출을 하도록 바꾸고(By Friend 또는 By Session Id), `SearchTargetId`가 누구인지 또는 어느 세션인지를 지정합니다. 이때 발견용 필드는 무시되고 필터는 그대로 적용됩니다. 특정 세션을 지목한 검색은 숨긴 세션도 보며, 그 결과는 `On Sessions Found`와 `Get Last Easy Search Results`에 실리지 않습니다.

### 5.4 FEasySessionSearchResult *(읽기 전용)*
`SessionDisplayName`, `HostName`, `PingInMs`, `MaxPlayers`, `OpenSlots`, `bIsDedicatedServer`, `bPasswordProtected`, `Region`, `bMatchInProgress`, `CustomSettings`

두 노드를 잇는 구조체입니다. `Find Easy Sessions`가 돌려주고 `Join Easy Session`이 받습니다.
구조체를 통째로 들고 계세요. 서버 브라우저의 각 행은 표시할 이름만이 아니라 이 구조체를
저장해야 합니다.

### 5.5 FEasyFriendSession
`Friend`(`FEasySessionFriend`), `bHasSession`, `Session`(`FEasySessionSearchResult`)

`Find Easy Friend Sessions`가 친구 한 명당 하나씩 돌려주는 구조체입니다. `Session`은
`bHasSession`이 true일 때만 유효하고, 다른 검색 결과처럼 그대로 참가에 씁니다.

`bPasswordProtected`는 `Join Easy Session` 전에 비밀번호를 물어볼지 판단하는 근거입니다.

### 5.6 FEasyMatchmakingParams
`Search`(SearchParams), `Host`(HostParams - `bAllowHostFallback`이 켜진 동안만 읽음), `bAllowHostFallback`, `JoinPassword`(String), `MaxSearchPasses`(int), `DelayBetweenPassesSeconds`(float)

### 5.7 FEasySessionPlayerInfo *(읽기 전용)*
`PlayerName`, `bIsLocalPlayer`, `bIsHost`(데디케이티드 서버에서는 항상 false), `PlayerId`(온라인 서비스의 플레이어 id - 이름은 겹칠 수 있지만 이것은 겹치지 않습니다)

### 5.8 FEasySessionFriend *(읽기 전용)*
`DisplayName`, `bIsOnline`, `bIsPlayingThisGame`, `NativeId`(Unique Net Id)

`Read Easy Friends`가 돌려주며, 초대와 프로필 함수에 그대로 넘기면 됩니다.
`NativeId`를 검색의 `SearchTargetId`에 넣으면 그 친구가 있는 세션을 찾습니다.

### 5.9 FEasyDisconnectInfo *(읽기 전용)*
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
| **`JoinSessionFull`** | 방이 꽉 찼습니다. 트래블 전에는 호스트가, 그 뒤에는 온라인 서비스가 거절합니다 |
| **`JoinSessionDoesNotExist`** | 참가 시점에 방이 사라졌습니다. 다시 검색하세요 |
| **`WrongPassword`** | 호스트가 거절했습니다: 비밀번호가 맞지 않습니다. 다시 입력받으세요 |
| **`JoinRefused`** | 호스트가 다른 이유로 거절했습니다. 예: 더 이상 플레이어를 받지 않는 매치. `ErrorMessage`는 호스트가 쓴 문장이라 그대로 보여줘도 됩니다 |
| **`ResolveFailure`** | 참가는 됐지만 호스트 주소가 동작하지 않습니다. 대개 호스트가 리슨 서버가 되지 못한 경우입니다 ([FAQ](FAQ.ko.md)) |
| **`RequiresSessionAuthority`** | 그 세션을 만든 게임만 할 수 있는 일입니다. `Is Easy Session Authority`가 true일 때만 버튼을 보여주세요 |
| **`Timeout`** | 온라인 서비스가 끝내 답하지 않았습니다. 결과를 알 수 없으므로 남은 것이 있으면 정리됩니다. `RequestTimeoutSeconds` 참고 |
| **`Canceled`** | `Cancel Easy Matchmaking`가 Matchmaking를 중단시켰습니다 |
| `NoOnlineSubsystem` | 온라인 서브시스템이 없습니다. `DefaultEngine.ini`를 확인하세요 |
| `InvalidParams` | 성립할 수 없는 파라미터입니다. 예: 폴백 Map Name 없는 Matchmaking |
| `MatchmakingAlreadyInProgress` | Matchmaking가 이미 돌고 있습니다 |
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

### 6.4 EEasyMatchmakingState

`Idle`, `Searching`, `Joining`, `Hosting`, `Complete` - Matchmaking 한 번의 진행 단계이며 `OnStateChanged`로 알려줍니다.

### 6.5 EEasySessionHostMode

`ListenServer`(호스트 플레이어의 게임이 곧 서버) 또는 `DedicatedServer`(코드 경로는 있으나 1.0에서는 검증되지 않음).

### 6.6 EEasySessionRegion

`Any`에 `NorthAmericaEast`부터 `Oceania`까지 큰 단위의 세계 지역 아홉 개를 더한 열거형입니다. 한 지역 안이면 쾌적한 핑으로 플레이할 수 있도록 나눴습니다. 게임 고유의 분할이 필요하면 `Any`로 두고 `CustomSettings` 키로 필터하세요 ([가이드](Guide-Sessions.ko.md)).

### 6.7 EEasySessionSearchMode

`Default`는 필터가 묘사하는 세션들을 찾습니다. `ByFriend`와 `BySessionId`는 대신 특정 세션 하나를 서비스에 물으며, 누구인지 또는 어느 세션인지는 `SearchTargetId`에서 읽습니다.

### 6.8 EEasySessionActivity

`None`, `Creating`, `Searching`, `Joining`, `Leaving`, `Updating`, `Starting`, `Ending`, `Matchmaking`, `Traveling` - 플러그인이 지금 하고 있는 일. `Get Easy Session Activity`로 읽습니다. 누가 시작했든 작업에 이름을 붙이므로, 상태 위젯이 메뉴가 요청한 적 없는 초대 참가나 연결 끊김 복구도 서술할 수 있습니다. `Get Activity Message`가 이를 문장으로 바꿉니다.

## 7. UEasyMatchmakingPolicy

`Start Easy Matchmaking` 뒤에서 실제로 일하는 객체입니다. 검색하고, 찾은 것 중 가장 좋은 방에 참가하고, 없으면 직접 호스트가 됩니다.

블루프린트나 C++로 서브클래스를 만들고, 매치메이킹 기준을 바꾸려면
**`ScoreSession(Session) -> float`**(값이 클수록 먼저 참가)만 오버라이드하면 됩니다.
편집 가능한 기본값은 `PingBucketsMs`(기본 `[50, 100, 150]`), `TopCandidateRandomization`(기본 3)입니다.
상태는 `GetState`와 `GetElapsedSeconds`로 조회하거나 `OnStateChanged` / `OnUpdated`에 바인딩하세요.

## 8. UEasySessionConfig (Project Settings -> Plugins -> EasySession)

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
`StartMatchmaking`. 블루프린트와 C++은 같은 코드 경로를 지납니다.

`OnModifyServerTravelURL`과 `OnModifyClientTravelURL`은 서브시스템의 C++ 전용 델리게이트입니다.
Travel 직전에 URL을 넘겨주므로 원하는 옵션을 덧붙일 수 있습니다. 시작할 때 한 번 바인딩하세요.
훅은 그 작업의 완료 콜백보다 먼저 발화하므로, 완료 콜백 안에서 바인딩하면 정작 그 Travel에는
적용되지 않습니다. 고정된 문자열로 표현할 수 있는 것이라면 `AdditionalTravelOptions` 쪽이 낫습니다.

## 10. 콘솔 명령 *(개발 빌드 전용)*

`EasySession.Host [Map]`, `EasySession.Find`, `EasySession.Join [Index] [Password]`, `EasySession.Matchmaking [Map]`, `EasySession.Travel <Map>`, `EasySession.Destroy`, `EasySession.Start`, `EasySession.End`, `EasySession.Cancel`, `EasySession.Status`, `EasySession.Friends`, `EasySession.InviteUI`, `EasySession.Diagnose`
