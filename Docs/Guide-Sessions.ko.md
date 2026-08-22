# 가이드 - 세션

*[English](Guide-Sessions.en.md)*

세션을 만들고, 찾고, 참가하고, 매치를 시작하고, 나가는 것에 관한 전부입니다. 여기 나오는 비동기 노드는 전부 같은 모양입니다. 왼쪽에 입력이 있고, `OnSuccess` / `OnFailure` 실행 핀에 `Result` 열거형과 `ErrorMessage` 문자열이 따라 나옵니다.

모든 작업은 **큐에 들어가 하나씩 실행됩니다.** 어떤 순서로 불러도, 심지어 같은 프레임에 불러도 온라인 서비스가 망가지지 않습니다.

## Create Session

`Create Easy Session`에 `FEasySessionHostParams`를 넘깁니다. 표의 순서는 Make 노드에 핀이 나오는 순서와 같습니다.

| 필드 | 기본값 | 설명 |
|---|---|---|
| Session Display Name | "My Session" | 검색 결과에 보이는 이름 |
| Map Name | (비어 있음) | `?listen`을 붙여 그 맵으로 이동합니다. 비워두면 현재 맵에서 리슨을 시작합니다 |
| Host Mode | Listen Server | 또는 Dedicated Server - 코드 경로는 있으나 1.0에서는 검증되지 않음 |
| Max Players | 4 | 공개 커넥션 수 |
| Is LAN Match | false | NULL 서브시스템에서는 자동으로 켜집니다 |
| Start Listening | true | 끄면 세션은 광고되지만 접속할 서버가 없습니다. 리슨 서버를 직접 여는 경우에만 끄세요 |
| Should Advertise | true | 끄면 세션을 아예 광고하지 않습니다 |
| Hidden | false | 광고는 하되 `Find Easy Sessions` 결과에서 뺍니다. 초대로만 들어올 수 있습니다 |
| Password | (비어 있음) | 아래 [비밀번호로 잠근 세션](#비밀번호로-잠근-세션) 참고 |
| Friends Bypass Password | true | 친구는 비밀번호 없이 참가합니다. 같은 절 참고 |
| Additional Travel Options | (비어 있음) | 호스트의 Travel URL에 그대로 붙는 옵션 문자열 |
| Allow Join In Progress | true | 스팀에서는 켜 두세요. 꺼두면 첫 참가에 로비가 닫힙니다([FAQ](FAQ.ko.md)) |
| Allow Invites / Use Presence | true | LAN과 데디케이티드 서버에서는 이 설정이 무시됩니다 |
| Custom Settings | (비어 있음) | 세션과 함께 광고되는 키-값 데이터, 아래 참고 |

### 커스텀 세션 데이터

`Custom Settings`는 세션과 함께 광고되는 문자열 맵입니다. 게임 모드, 지역, 난이도처럼 찾는 쪽이 걸러내거나 화면에 띄울 값을 넣으세요.

```
CustomSettings = { "GameMode": "CTF", "Region": "AS" }
```

찾는 쪽은 각 `FEasySessionSearchResult.CustomSettings`에서 이 값을 다시 읽고, `Required Custom Settings`로 검색 단계에서 걸러낼 수도 있습니다(모든 쌍이 정확히 일치해야 합니다).

## Find Sessions

`Find Easy Sessions`에 `FEasySessionSearchParams`를 넘깁니다.

| 필드 | 기본값 | 설명 |
|---|---|---|
| Max Results | 50 | 결과를 최대 몇 개까지 받을지 |
| LAN Query | false | NULL에서는 자동으로 켜집니다 |
| Timeout Seconds | 15 | 결과를 이만큼 기다리고 포기합니다 |
| Min Open Slots | 0 | 빈 자리가 이만큼 이상인 세션만 |
| Max Ping Ms | 0 | 0이면 제한 없음 |
| Required Custom Settings | (비어 있음) | 광고된 커스텀 데이터와 정확히 일치하는 것만 통과 |

결과는 `OnSuccess`로 오고 캐시에도 남습니다. `Get Last Easy Search Results`가 언제 어디서든 그 결과를 돌려주므로 서버 목록 UI를 만들 때 편합니다.

각 `FEasySessionSearchResult`는 표시 이름, 맵 이름, 호스트 이름, 핑, 최대 인원, 빈 자리, 데디케이티드 여부, 비밀번호 여부, 숨김 여부, 커스텀 세팅 맵을 담고 있습니다.

## Join Session

`Join Easy Session`은 검색 결과를 받습니다. 성공하면 호스트 주소를 해석해 그리로 클라이언트 Travel을 합니다. 참가는 언제나 호스트에 접속하는 것이므로, 참가하는 플레이어는 있던 맵을 떠납니다. 호스트와 이름이 같은 맵에 있었더라도 마찬가지입니다.

EasySession은 성공을 알리기 **전에** 호스트 주소를 검증합니다. 호스트에 실제로 닿을 수 없으면([FAQ: 포트 0](FAQ.ko.md)) 20초짜리 접속 타임아웃 대신 곧바로 `ResolveFailure`와 설명이 돌아오고, 반쯤 참가된 세션도 정리되므로 바로 다시 시도할 수 있습니다.

## 비밀번호로 잠근 세션

### 세션 잠그기

호스트 파라미터의 `Password`를 설정하세요. 이게 전부입니다.

```
Create Easy Session
  Host Params > Password = "1234"
```

비밀번호 자체는 광고되지 않습니다. "비밀번호가 걸려 있음"이라는 표시만 세션과 함께
나가고, `Find Easy Sessions`가 그것을 각 검색 결과의 `Password Protected`로 돌려줍니다.
입력을 받을지 말지 이 값으로 정하세요.

### 잠긴 세션에 참가하기

플레이어가 입력한 값을 `Join Easy Session`의 `Password` 핀에 넘기세요. 플러그인이 자기가
수행하는 Travel의 URL에 그 값을 붙입니다.

### 결과 읽기

다른 일이 벌어지기 전에 호스트에게 먼저 승인을 묻습니다. 비밀번호가 틀리면 노드가
`WrongPassword`로, 매치가 더 이상 플레이어를 받지 않으면 `JoinRefused`로 실패합니다.
두 경우 모두 맵 로드가 시작되지 않았고, `ErrorMessage`에 호스트가 쓴 문장이 담기며,
플레이어는 곧바로 다시 시도할 수 있습니다.

```
Join Easy Session
  OnFailure -> Result == WrongPassword ?
                 true  -> 비밀번호 입력창을 다시 열고 ErrorMessage 표시
                 false -> ErrorMessage 표시
```

예제의 비밀번호 팝업이 바로 이렇게 합니다. 다시 입력할 수 있도록 열린 채로 남고, 입력칸
아래에 사유를 보여줍니다(`WBP_JoinPasswordPopup`).

### 호스트에게 물을 수 없을 때

승인은 비콘을 타고 갑니다. 비콘은 호스트로 향하는 두 번째의 가벼운 연결입니다. 그 비콘에
닿지 못하면(포트가 막혔거나, 같은 PC의 다른 인스턴스가 같은 포트를 먼저 쓰고 있거나, 프로젝트가 엔진의 `BeaconNetDriver` 정의를 지웠거나 - 그 줄을
되살리는 방법은 [Steam 설정](Setup-Steam.ko.md)에 있고 `EasySession.Diagnose`도 검사합니다)
참가가 그대로 진행되고, 대신 호스트가 도착한 연결을 거절합니다.

이렇게 늦게 오는 거절은 디스커넥트이므로 플레이어는 메뉴 레벨로 돌아갑니다
(`bAutoReturnToMenuOnDisconnect`, 기본값 켜짐). 사유는 거기서 읽으세요.

```
Event Construct
  Has Pending Easy Disconnect Info ?
    Consume Last Easy Disconnect Info  ->  Break Easy Disconnect Info
                                             Reason      == Rejected
                                             Reason Text == "Wrong session password."
```

이 정보는 메뉴가 보여줄 수 있도록 Travel을 넘어 보존됩니다. 문자열을 비교하지 말고
`Reason`을 보세요. `Reason`은 네 가지입니다.

| Reason | 언제 |
|---|---|
| `ConnectionLost` | 호스트가 나갔거나, 죽었거나, 네트워크가 끊김 |
| `HostDestroyedSession` | 호스트가 `Destroy Easy Session For Everyone`으로 모두를 내보냄 |
| `TravelFailure` | 세션의 맵으로 이동하지 못함 |
| `Rejected` | 호스트가 연결을 거절함. 비밀번호가 틀렸거나 매치가 닫혀 있음. 사유는 `Reason Text`에 |

비콘이 잘 동작하더라도 이 핸들러는 남겨 두세요. 연결이 끊기는 모든 경우를 받아내는 안전망입니다.

### 친구는 비밀번호를 건너뜁니다

`Friends Bypass Password`의 기본값은 **true**입니다. 플랫폼 초대에는 비밀번호를 입력할 자리가
없어서, 이게 없으면 초대받은 친구가 정작 그 세션에서 쫓겨납니다. 호스트가 플랫폼 친구 목록으로
친구인지 확인하므로 참가하는 쪽이 속일 수 없습니다. 친구라는 개념이 없는 NULL/LAN에서는
아무 영향이 없습니다.

## Start Session / End Session

`Start Easy Session`은 세션을 InProgress 상태로 옮깁니다. `Allow Join In Progress`가 꺼져 있다면 이 시점부터 매치가 끝날 때까지 새 플레이어가 거절됩니다. 스팀은 예외로, 첫 참가부터 이미 막혀 있습니다([FAQ](FAQ.ko.md)).

`End Easy Session`은 매치를 끝내고 세션을 다시 참가할 수 있는 상태로 되돌립니다.

둘 다 호스트 전용입니다. 클라이언트가 부르면 `RequiresSessionAuthority`로 실패하므로, 버튼은 `Is Easy Session Host`로 막아 두세요.

## Update Session

`Update Easy Session`에 호스트 파라미터를 다시 넘기면 세션이 새 값으로 광고됩니다. 호스트 전용입니다.

바꿀 수 있는 것은 표시 이름, 최대 인원, 광고 여부, 숨김 여부, 난입 허용 여부, 초대 허용 여부, 비밀번호(와 친구 예외), 커스텀 세팅입니다.

아래 옵션들은 바꿀 수 없습니다.

| 필드 | 왜 |
|---|---|
| Map Name | 맵은 `Server Travel Easy Session`으로 옮깁니다 |
| Host Mode | 리슨이냐 데디케이티드냐는 프로세스를 띄운 방식이라 실행 중에 바뀌지 않습니다 |
| Is LAN Match | 세션이 LAN에 있는지 온라인 서비스에 있는지는 만들 때 정해집니다 |
| Use Presence | 플러그인이 넘기지 않고, 넘기더라도 스팀이 거절합니다. `Can't change presence settings on existing session` 경고만 남고 이전 값이 유지됩니다 |
| Start Listening / Additional Travel Options | 세션을 만들 때 Travel에 한 번 쓰이는 값이라 이후에는 읽지 않습니다 |

> LAN(NULL)에서는 광고 여부를 바꿔도 LAN 비콘이 그대로입니다. 엔진의 `FOnlineSessionNull::UpdateSession`이 설정만 갈아끼우고 비콘을 다시 계산하지 않기 때문입니다.

## Destroy Session

`Destroy Easy Session`은 호스트에서는 세션을 파괴하고, 클라이언트에서는 세션을 떠납니다. 나간 뒤에는 곧바로 다시 방을 만들거나 참가할 수 있습니다.

호스트가 이 노드를 부르면 클라이언트들은 연결이 끊긴 것으로 보고 `ConnectionLost`로 메뉴에 돌아갑니다. 왜 끝났는지 알려주고 싶다면 `Destroy Easy Session For Everyone`에 사유를 넘기세요. 세션을 내리기 전에 모두에게 그 문구를 먼저 보내고, 받는 쪽은 `HostDestroyedSession`으로 읽습니다.

## 매치 도중 맵 옮기기

`Server Travel Easy Session`(호스트 전용)은 세션 전체를 새 맵으로 옮기며, 서버가 데디케이티드이거나 옵션을 직접 적은 경우가 아니면 `?listen`을 붙입니다. 클라이언트는 자동으로 따라옵니다. 다른 노드와 달리 이건 비동기 노드가 아니라 성공 여부를 bool로 즉시 돌려줍니다.

맵 전환은 항상 이 노드로 하세요. 이 노드는 맵이 바뀌기 전에 참가 승인 비콘을 멈춥니다. 그냥 `ServerTravel`을 하면 포트가 계속 잡혀 있어서 새 맵이 자기 비콘을 띄우지 못합니다.

## 이벤트와 상태 조회

UI를 갱신하려면 서브시스템(`Get Easy Session Subsystem`)에서 아래 이벤트를 바인딩하세요.

- `OnSessionCreated`, `OnSessionsFound`, `OnSessionJoined`, `OnSessionUpdated`, `OnSessionStarted`, `OnSessionEnded`, `OnSessionDestroyed` - 각 작업이 끝날 때 발생합니다. 누가 시작했든 상관없습니다
- `OnSessionFailure` - 세션 연결이 끊겼거나 네트워크 오류가 났습니다. EasySession이 죽은 세션을 알아서 파괴하므로 플레이어는 즉시 다시 들어갈 수 있습니다
- `OnQuickMatchComplete` - Quick Match가 끝났습니다

어디서나 쓸 수 있는 순수 조회 노드도 있습니다. 상태는 `Is In Easy Session`, `Is Easy Session Host`, `Is Easy Session Busy`, `Get Easy Session State`, 내용은 `Get Easy Session Display Name`, `Get Easy Session Player Infos`, `Get Easy Session Player Count`, `Get Easy Session Max Players`, 환경은 `Get Online Subsystem Name`. 전체 목록은 [API 레퍼런스](API.ko.md)에 있습니다.

## C++ API

위의 모든 것은 `UEasySessionSubsystem`을 얇게 감싼 것입니다. C++에서는 같은 함수를 네이티브 델리게이트와 함께 호출합니다.

```cpp
UEasySessionSubsystem* Session = GetGameInstance()->GetSubsystem<UEasySessionSubsystem>();
Session->CreateEasySession(HostParams,
	FEasySessionCompleteDelegate::CreateUObject(this, &UMyClass::OnHosted));
```

블루프린트와 C++이 같은 코드 경로를 지나므로 둘의 동작이 갈라지지 않습니다.
