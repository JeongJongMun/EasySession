# 가이드 - 세션

*[English](Guide-Sessions.en.md)*

세션을 만들고, 찾고, 참가하고, 매치를 시작하고, 나가는 것에 관한 전부입니다. 여기 나오는 비동기 노드는 전부 같은 모양입니다. 왼쪽에 입력이 있고, `OnSuccess` / `OnFailure` 실행 핀에 `Result` 열거형과 `ErrorMessage` 문자열이 따라 나옵니다.

모든 요청은 **큐에 들어가 하나씩 실행됩니다.** 어떤 순서로 불러도, 심지어 같은 프레임에 불러도 온라인 서비스가 망가지지 않습니다.

## Create Session

`Create Easy Session`에 `FEasySessionHostParams`를 넘깁니다. 표의 순서는 Make 노드에 핀이 나오는 순서와 같습니다.

| 필드 | 기본값 | 설명 |
|---|---|---|
| Session Display Name | "My Session" | 검색 결과에 보이는 이름 |
| Initial Map Name | (비어 있음) | 필수. 세션을 만든 뒤 호스트가 `?listen`을 붙여 그 맵으로 이동하고, 이것이 이 게임을 서버로 만듭니다. 비어 있으면 생성이 `InvalidParams`로 실패합니다. 맵을 새로 로드하므로, 세션이 생기기 전에 접속해 있던 플레이어는 연결이 끊기고 세션에 참가해서 다시 들어와야 합니다. 세션은 자기 맵을 광고하지 않습니다 |
| Max Players | 4 | 공개 커넥션 수. 엔진의 접속 정원(Server full)도 이 값을 따릅니다 |
| Is LAN Match | false | NULL 서브시스템에서는 자동으로 켜집니다 |
| Should Advertise | true | 끄면 세션을 아예 광고하지 않습니다 |
| Hidden | false | 광고는 하되 `Find Easy Sessions` 결과에서 뺍니다. 초대로만 들어올 수 있습니다 |
| Password | (비어 있음) | 아래 [비밀번호로 잠근 세션](#비밀번호로-잠근-세션) 참고 |
| Friends Bypass Password | true | 친구는 비밀번호 없이 참가합니다. 같은 절 참고 |
| Additional Travel Options | (비어 있음) | 호스트의 Travel URL에 그대로 붙는 옵션 문자열 |
| Allow Join In Progress | true | 스팀에서는 켜 두세요. 꺼두면 첫 참가에 로비가 닫힙니다([FAQ](FAQ.ko.md)) |
| Allow Invites / Use Presence | true | Use Presence는 LAN에서 무시됩니다 |
| Custom Settings | (비어 있음) | 세션과 함께 광고되는 키-값 데이터, 아래 참고 |

### 커스텀 세션 데이터

`Custom Settings`는 세션과 함께 광고되는 문자열 맵입니다. 게임 모드, 지역, 난이도처럼 찾는 쪽이 걸러내거나 화면에 띄울 값을 넣으세요.

```
CustomSettings = { "GameMode": "CTF", "Region": "AS" }
```

찾는 쪽은 각 `FEasySessionSearchResult.CustomSettings`에서 이 값을 다시 읽고, `Required Custom Settings`로 검색 단계에서 걸러낼 수도 있습니다(모든 쌍이 정확히 일치해야 합니다).

`Update Easy Session`에 넘긴 맵이 세션의 커스텀 데이터를 그대로 대신합니다. 맵에서 뺀 키는 세션에서도 사라지므로, `Get Easy Session Settings`로 받은 맵을 고쳐서 넘기세요.

## Find Sessions

`Find Easy Sessions`에 `FEasySessionSearchParams`를 넘깁니다.

| 필드 | 기본값 | 설명 |
|---|---|---|
| Max Results | 50 | 결과를 최대 몇 개까지 받을지 |
| LAN Query | false | NULL에서는 자동으로 켜집니다 |
| Min Open Slots | 0 | 빈 자리가 이만큼 이상인 세션만 |
| Max Ping Ms | 0 | 0이면 제한 없음 |
| Required Custom Settings | (비어 있음) | 광고된 커스텀 데이터와 정확히 일치하는 것만 통과 |
| Region | Any | 이 지역을 광고하는 세션만 (지역 절 참고) |
| Include In Progress Sessions | true | 끄면 매치가 이미 시작된 세션을 숨깁니다. 난입을 거절하는 세션은 어차피 검색에 응답하지 않아 보이지 않습니다 |

`Find Easy Friend Sessions`는 친구 판 검색입니다. 친구 목록을 읽고, 이 게임을 플레이 중인
친구마다 어떤 세션에 있는지 찾아 친구 한 명당 한 항목으로 돌려줍니다. 항목은 목록에 바로
쓸 수 있는 순서로 옵니다: 참가 가능한 세션에 있는 친구가 맨 위, 나머지는 접속 상태(이 게임
플레이 중, 온라인, 오프라인)와 이름순입니다. 친구의 세션은 다른 검색 결과처럼 그대로 참가에
씁니다. 더 필요 없어진 친구 세션 검색은 `Cancel Easy Friend Search`로 멈추고, 새로고침 버튼은
다른 세션 호출과 마찬가지로 `Is Easy Session Busy`로 기다립니다. 친구 목록과 마찬가지로 NULL/LAN에서는
지원되지 않습니다.

결과는 `OnSuccess`로 옵니다.

각 `FEasySessionSearchResult`는 표시 이름, 호스트 이름, 핑, 최대 인원, 빈 자리, 데디케이티드 여부, 비밀번호 여부, 숨김 여부, 지역, 매치 진행 중 여부, 커스텀 세팅 맵을 담고 있습니다.

## Join Session

`Join Easy Session`은 검색 결과를 받습니다. 성공하면 호스트 주소를 해석해 그리로 클라이언트 Travel을 합니다. 참가는 언제나 호스트에 접속하는 것이므로, 참가하는 플레이어는 있던 맵을 떠납니다. 호스트와 이름이 같은 맵에 있었더라도 마찬가지입니다.

EasySession은 성공을 알리기 **전에** 호스트 주소를 검증합니다. 호스트에 실제로 닿을 수 없으면([FAQ: 포트 0](FAQ.ko.md)) 20초짜리 접속 타임아웃 대신 곧바로 `ResolveFailure`와 설명이 돌아오고, 반쯤 참가된 세션도 정리되므로 바로 다시 시도할 수 있습니다.

### 호스트에게 먼저 묻기

참가하는 플레이어는 Travel 전에 호스트에게 예약을 요청합니다. 호스트는 매치가 아직 플레이어를 받는지, 세션이 꽉 차지 않았는지, 비밀번호가 맞는지를 확인합니다.

매치가 더 이상 플레이어를 받지 않으면 노드가 `JoinRefused`로, 세션이 꽉 찼으면 `JoinSessionFull`로, 비밀번호가 틀리면 `WrongPassword`로 실패합니다. 세 경우 모두 맵 로드가 시작되지 않았고, `ErrorMessage`에 호스트가 쓴 문장이 담기며, 플레이어는 곧바로 다시 시도할 수 있습니다.

```
Join Easy Session
  OnFailure -> Result == WrongPassword ?
                 true  -> 비밀번호 입력창을 다시 열고 ErrorMessage 표시
                 false -> ErrorMessage 표시
```

예제의 비밀번호 팝업이 바로 이렇게 합니다. 다시 입력할 수 있도록 열린 채로 남고, 입력칸 아래에 사유를 보여줍니다(`WBP_JoinPasswordPopup`).

### 예약

승인받은 플레이어는 몇 초 동안 맵을 로드하고, 호스트는 그동안 그 플레이어의 예약을 유지합니다. 그래서 마지막 한 자리를 두 명이 동시에 승인받는 일은 생기지 않습니다. 예약은 그 플레이어가 도착할 때까지 유지되고, 45초 안에 도착하지 않으면 호스트가 지웁니다. 세션에서 나간 플레이어의 예약은 로그아웃과 함께 지워지므로, 다음 플레이어나 방금 나간 그 플레이어가 다시 들어올 수 있습니다.

호스트도 예약 하나를 가집니다. Max Players에 호스트가 포함되기 때문입니다.

검색 결과는 이미 세션에 들어와 있는 플레이어만 세고, 아직 로드 중인 플레이어의 예약은 세지 않습니다. 그래서 검색 결과에는 빈자리가 보이는데 참가하면 `JoinSessionFull`로 실패할 수 있습니다. 이때 Matchmaking은 다음 후보로 넘어갑니다.

맵을 바꾸면 이미 들어와 있던 사람들까지 전부 다시 이동하게 되므로, 호스트는 모든 예약을 새 맵에서도 유지합니다. 호스트는 각 플레이어가 도착하기를 45초 기다리며, 이 값은 엔진의 `TravelSessionTimeoutSecs`입니다. 맵 로딩이 그보다 오래 걸리면 `DefaultEngine.ini`에서 올리세요. 아래는 90초로 올리는 예입니다.

```ini
[/Script/OnlineSubsystemUtils.PartyBeaconHost]
TravelSessionTimeoutSecs=90
```

### 호스트에게 물을 수 없을 때

예약 요청은 비콘을 타고 갑니다. 비콘은 호스트로 향하는 두 번째의 가벼운 연결입니다. 프로젝트가 이미 자기 비콘 호스트를 쓰고 있다면 예약 비콘은 새 포트를 열지 않고 그 호스트에 얹혀 동작합니다. 그 비콘에 닿지 못하면(포트가 막혔거나, 같은 PC의 다른 인스턴스가 같은 포트를 먼저 쓰고 있거나, 프로젝트가 엔진의 `BeaconNetDriver` 정의를 지웠거나 - 그 줄을 되살리는 방법은 [Steam 설정](Setup-Steam.ko.md)에 있고 `EasySession.Diagnose`도 검사합니다) 열린 세션은 참가가 그대로 진행되고 대신 호스트가 도착한 플레이어를 검사합니다. 비밀번호 세션은 비밀번호를 전할 곳이 비콘뿐이므로 노드가 `JoinRefused`로 실패합니다.

비콘은 엔진의 포트를 씁니다. 기본값은 15000이고, `DefaultEngine.ini`의 `[/Script/OnlineSubsystemUtils.OnlineBeaconHost] ListenPort=`나 커맨드라인 `-BeaconPort=`로 옮길 수 있습니다. 그 포트를 잡는 쪽은 호스트뿐이고 참가자는 운영체제가 주는 임의 포트에서 접속하므로, 한 PC의 두 인스턴스는 둘 다 호스트일 때만 충돌합니다. 그때는 호스트마다 `-BeaconPort=`를 다르게 주세요. 포트가 이미 사용 중이면 엔진이 다음 빈 포트에 바인딩하는데 세션은 설정된 포트를 계속 광고하므로, 호스트가 두 포트를 모두 적은 경고를 남깁니다.

이렇게 늦게 오는 거절은 디스커넥트이므로 플레이어는 메뉴 레벨로 돌아갑니다(`bAutoReturnToMenuOnDisconnect`, 기본값 켜짐). 사유는 거기서 읽으세요.

```
Event Construct
  Has Pending Easy Disconnect Info ?
    Consume Pending Easy Disconnect Info  ->  Break Easy Disconnect Info
                                             Reason      == Rejected
                                             Reason Text == "The match is already in progress."
```

이 정보는 메뉴가 보여줄 수 있도록 Travel을 넘어 보존됩니다. 문자열을 비교하지 말고 `Reason`을 보세요. `Reason`은 네 가지입니다.

| Reason | 언제 |
|---|---|
| `ConnectionLost` | 호스트가 나갔거나, 죽었거나, 네트워크가 끊김. 조인 도중 호스트가 죽은 경우도 여기 |
| `HostDestroyedSession` | 호스트가 `Destroy Easy Session For Everyone`으로 모두를 내보냄 |
| `TravelFailure` | 세션의 맵으로 이동하지 못함 |
| `Rejected` | 호스트가 도착한 연결을 거절함. 매치가 닫혀 있거나, 예약 비콘을 거치지 않고 비밀번호 세션에 도착함. 사유는 `Reason Text`에 |

비콘이 잘 동작하더라도 이 핸들러는 남겨 두세요. 연결이 끊기는 모든 경우를 받아내는 안전망입니다.

## 비밀번호로 잠근 세션

### 세션 잠그기

호스트 파라미터의 `Password`를 설정하세요. 이게 전부입니다.

```
Create Easy Session
  Host Params > Password = "1234"
```

비밀번호 자체는 광고되지 않습니다. "비밀번호가 걸려 있음"이라는 표시만 세션과 함께 나가고, `Find Easy Sessions`가 그것을 각 검색 결과의 `Password Protected`로 돌려줍니다. 입력을 받을지 말지 이 값으로 정하세요.

### 잠긴 세션에 참가하기

플레이어가 입력한 값을 `Join Easy Session`의 `Password` 핀에 넘기세요. 호스트가 Travel 전에 예약 비콘으로 그 값을 확인하고([호스트에게 먼저 묻기](#호스트에게-먼저-묻기)), 비밀번호는 Travel URL에 들어가지 않으므로 엔진의 Travel 로그에도 남지 않습니다. 비콘에서 승인받지 않은 플레이어는 도착할 때 거절되므로, `open` 콘솔 명령 같은 직접 접속으로는 비밀번호 세션에 들어올 수 없습니다.

### 친구는 비밀번호를 건너뜁니다

`Friends Bypass Password`의 기본값은 **true**입니다. 플랫폼 초대에는 비밀번호를 입력할 자리가 없어서, 이게 없으면 초대받은 친구가 정작 그 세션에서 쫓겨납니다. 호스트가 플랫폼 친구 목록으로 친구인지 확인하므로 참가하는 쪽이 속일 수 없습니다. 친구라는 개념이 없는 NULL/LAN에서는 아무 영향이 없습니다.

## Start Session / End Session

`Start Easy Session`은 세션을 InProgress 상태로 옮깁니다. `Allow Join In Progress`가 꺼져 있다면 이 시점부터 매치가 끝날 때까지 새 플레이어가 거절됩니다. 스팀은 예외로, 첫 참가부터 이미 막혀 있습니다([FAQ](FAQ.ko.md)).

`End Easy Session`은 매치를 끝내고 세션을 다시 참가할 수 있는 상태로 되돌립니다.

둘 다 광고되는 매치 진행 중 키를 갱신합니다. 검색의 `Include In Progress Sessions` 필터와
결과의 `bMatchInProgress`가 읽는 값이 바로 이것입니다.

둘 다 호스트 전용입니다. 클라이언트가 부르면 `RequiresSessionAuthority`로 실패하므로, 버튼은 `Is Easy Session Authority`가 true일 때만 보여주세요.

## Update Session

`Update Easy Session`에 호스트 파라미터를 다시 넘기면 세션이 새 값으로 광고됩니다. 호스트 전용입니다.

무엇을 바꿀 수 있는지는 `FEasySessionSettings`가 답합니다. 표시 이름, 최대 인원, 광고 여부,
숨김 여부, 난입 허용 여부, 초대 허용 여부, 지역, 참가 코드, 비밀번호(와 친구 예외), 커스텀
세팅이 전부입니다. `Get Easy Session Settings`로 현재 값을 받아 고치면 됩니다.

호스팅 전용 필드는 한 단계 위인 `FEasySessionHostParams`에 있고, Update에는 아예 닿지
않습니다.

| 필드 | 왜 |
|---|---|
| Initial Map Name | 세션을 만들 때만 읽습니다. 맵은 `Server Travel Easy Session`으로 옮깁니다 |
| Is LAN Match | 세션이 LAN에 있는지 온라인 서비스에 있는지는 만들 때 정해집니다 |
| Use Presence | 살아있는 세션에서는 스팀이 거절합니다. `Can't change presence settings on existing session` 경고만 남고 이전 값이 유지됩니다 |
| Additional Travel Options | 세션을 만들 때 Travel에 한 번 쓰이는 값이라 이후에는 읽지 않습니다 |

> LAN(NULL)에서는 광고 여부를 바꿔도 LAN 비콘이 그대로입니다. 엔진의 `FOnlineSessionNull::UpdateSession`이 설정만 갈아끼우고 비콘을 다시 계산하지 않기 때문입니다.

참가 중인 플레이어는 업데이트를 자동으로 전달받습니다. 플러그인이 멤버에게 보여도 되는
설정(표시 이름, 최대 인원, 플래그들, 리전, 참가 코드, 커스텀 설정)을 모든 클라이언트에
복제하고, 로컬 세션 사본을 고쳐서 일반 게터가 새 값을 돌려주게 만든 뒤
`OnSessionSettingsChanged`를 발화합니다. 호스트도 자기 UI를 위해 같은 이벤트를 발화합니다.
UI는 이 이벤트에서 게터로 갱신하면 됩니다. 비밀번호와 친구 예외만 호스트에 남습니다.

나머지 절반은 `OnSessionStateChanged`가 맡습니다. 세션 상태가 바뀔 때 호스트와 모든
클라이언트에서 발생하므로, 클라이언트는 폴링 없이 호스트의 매치 시작과 종료를 알 수 있습니다.

## Destroy Session

`Destroy Easy Session`은 이 게임의 네임드 세션만 지웁니다. 플레이어는 지금 맵에 그대로
남는데, 매치 사이의 호스트에게는 그게 맞습니다. 세션을 나가려는 클라이언트가 원하는 것은
`Leave Easy Session`입니다. 네임드 세션을 지운 뒤 메뉴 맵으로 돌아갑니다. 호스트가 Leave를
누르면 세션이 모두에게 닫히고, 클라이언트들은 "The host has left the game."을 사유로
읽습니다. 어느 쪽이든 직후에 곧바로 다시 세션을 만들거나 참가할 수 있습니다.

호스트가 이 노드를 부르면 클라이언트들은 연결이 끊긴 것으로 보고 `ConnectionLost`로 메뉴에 돌아갑니다. 왜 끝났는지 알려주고 싶다면 `Destroy Easy Session For Everyone`에 사유를 넘기세요. 세션을 내리기 전에 모두에게 그 문구를 먼저 보내고, 받는 쪽은 `HostDestroyedSession`으로 읽습니다.

## 매치 도중 맵 옮기기

`Server Travel Easy Session`(호스트 전용)은 세션 전체를 새 맵으로 옮기며, 서버가 데디케이티드이거나 옵션을 직접 적은 경우가 아니면 `?listen`을 붙입니다. 클라이언트는 자동으로 따라옵니다. 다른 노드와 달리 이건 비동기 노드가 아니라 성공 여부를 bool로 즉시 돌려줍니다.

맵 전환은 항상 이 노드로 하세요. 이 노드는 맵이 바뀌기 전에 예약 비콘을 멈추고, 모든 예약을 새 맵에서도 유지합니다. 그냥 `ServerTravel`을 하면 포트가 계속 잡혀 있어서 새 맵이 자기 비콘을 띄우지 못합니다.

맵 로드가 실패해도(오타, 쿠킹에서 빠진 맵) 세션과 접속자는 그대로입니다. 실패는 `OnSessionFailure`로 알려지니, 올바른 맵 이름으로 다시 부르면 됩니다.

## 지역

호스팅은 `Region`을 광고하고(기본값 `Any`) 검색은 그걸로 거릅니다. 양쪽에 같은 지역을
넣으면 플레이어는 쾌적하게 플레이할 수 있는 세션만 보게 됩니다. 검색의 `Any`는 모든 지역을
나열하고, 호스트의 `Any`는 지역 필터가 없는 검색에만 걸립니다. 지역은 일부러 큰 단위입니다.
한 지역 안이면 플레이할 만한 핑이라는 뜻이 되도록요. 게임 고유의 분할(국가 서버, 단일 지역)이
필요하면 `Any`로 두고 `Required Custom Settings`로 거르는 `Custom Settings` 키를 쓰세요.

## 참가 코드

호스팅할 때 `Use Join Code`를 켜면 생성된 6자리 코드가 세션에 광고되고, 세션에 있는 누구나
`Get Easy Session Join Code`로 읽을 수 있습니다. 참가는 검색 한 번 거리입니다.
`Find Easy Sessions`에 `Join Code`를 넣으면 숨긴 세션이라도 그 세션 하나가 결과로 오니, 보여준
뒤 `Join Easy Session`에 넘기면 됩니다. 같은 `Join Code`로 매치메이킹을 돌리면 찾기와
참가를 한 번에, 세션이 생길 때까지 재시도까지 해줍니다. `Hidden`에 코드를 더하면 친구 전용
세션이 됩니다. 어떤 브라우저에도 안 보이지만 코드를 아는 사람은 들어옵니다.

코드는 세션을 가리키는 이름이지 지키는 장치가 아닙니다. 지키는 것은 `Password`이고 둘은
조합됩니다. 코드가 세션을 찾고, 비밀번호가 여전히 문을 지킵니다.

코드는 검색 필터로도 동작합니다. `Find Easy Sessions`에 `Join Code`를 넣으면 숨긴 세션이라도
그 세션 하나가 결과로 오므로, 참가하기 전에 세션 정보를 보여주는 UI를 만들 수 있습니다.

## 이벤트와 상태 조회

요청 하나의 결과는 그 노드의 출력 핀으로 옵니다. 세션 전체를 지켜보는 UI라면 서브시스템(`Get Easy Session Subsystem`)에서
아래 이벤트를 바인딩하세요.

- `OnBusyChanged` - 요청이 시작됐거나 모두 끝났습니다. `Get Easy Session Activity`가 무엇인지 알려 주므로, 로딩 표시에 무엇을 기다리는지 적을 수 있습니다
- `OnSessionFailure` - 노드 결과로는 알릴 수 없는 실패가 났습니다. 연결이 끊겼거나, EasySession이 시작한 맵 이동이나 리슨 서버 열기가 실패했거나(예: 잘못된 Initial Map Name), 수락한 초대의 참가가 실패한 경우입니다. 세션을 잃은 클라이언트는 메뉴로 돌아가며, 플레이어에게 보여줄 이유는 `Consume Pending Easy Disconnect Info`에 있습니다
- `OnMatchmakingStarted`, `OnMatchmakingStateChanged`, `OnMatchmakingUpdated`, `OnMatchmakingComplete` - Matchmaking 한 번의 진행 전체. 자세한 내용은 [Matchmaking 가이드](Guide-Matchmaking.ko.md)에 있습니다

어디서나 쓸 수 있는 순수 조회 노드도 있습니다. 상태는 `Is In Easy Session`, `Is Easy Session Host`, `Is Easy Session Busy`, `Get Easy Session State`, 내용은 `Get Easy Session Display Name`, `Get Easy Session Player Infos`, `Get Easy Session Player Count`, `Get Easy Session Max Players`, 환경은 `Get Online Subsystem Name (EasySession)`. 전체 목록은 [API 레퍼런스](API.ko.md)에 있습니다.

## C++ API

위의 모든 것은 `UEasySessionSubsystem`을 얇게 감싼 것입니다. C++에서는 같은 함수를 네이티브 델리게이트와 함께 호출합니다.

```cpp
UEasySessionSubsystem* Session = GetGameInstance()->GetSubsystem<UEasySessionSubsystem>();
Session->CreateEasySession(HostParams,
	FEasySessionCompleteDelegate::CreateUObject(this, &UMyClass::OnHosted));
```

블루프린트와 C++이 같은 코드 경로를 지나므로 둘의 동작이 갈라지지 않습니다.
