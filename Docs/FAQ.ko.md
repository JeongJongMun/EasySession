# FAQ & 문제 해결

*[English](FAQ.en.md)*

실제로 겪는 문제들을, 보통 만나게 되는 순서대로 정리했습니다.

## "Find에서 세션이 하나도 안 나옵니다"

가능성이 높은 것부터 확인하세요.

1. **정말로 누군가 호스팅 중인가요?** 호스트가 `Should Advertise = true`로 `Create Easy Session`을 끝냈어야 합니다.
2. **한 대에서 PIE로 테스트 중** - Number of Players = 2, Net Mode = *Play Standalone*으로 설정하고 *Run Under One Process*를 끄세요. 한 프로세스를 공유하면 LAN 비콘 포트도 공유하게 되어 검색이 불안정해집니다.
3. **방화벽 / VPN** - LAN 검색은 UDP 브로드캐스트입니다. Windows 방화벽에서 게임을 허용하고, VPN을 끄거나, `-MultiHome=<LAN IP>`를 넘기세요.
4. **스팀: 같은 계정이거나 같은 PC에서 테스트** - 서로 다른 스팀 계정 두 개와 PC 두 대가 필요합니다. AppId 480을 쓴다면 커스텀 세팅으로 남들의 테스트 세션을 걸러내세요 ([스팀 설정](Setup-Steam.ko.md) 참고).
5. **다른 서브시스템이 돌고 있음** - 로그를 확인하세요: `EasySessionSubsystem initialized. Online subsystem: NULL/STEAM`. 패키지 빌드는 에디터 상태가 아니라 패키징된 ini를 읽습니다.

## "세션은 찾았는데 참가가 타임아웃되거나 ResolveFailure로 실패합니다"

세션은 광고되고 있지만 그 호스트가 **리슨 서버로 돌고 있지 않습니다** - 포트가 0인 주소가 광고된 상태입니다. EasySession은 접속 타임아웃을 기다리지 않고 즉시 `ResolveFailure`로 실패시키며, 참가하려던 쪽 로그에 사유가 남습니다.

```
LogEasySession: Warning: Session operation failed: ResolveFailure (The host address 'steam.0:0' is not connectable
- the host is not running as a listen server. Make sure the host creates its session with Start Listening enabled
or travels to a map with the ?listen option.)
```

**호스트** 쪽에서 고치세요. Host Params의 `Start Listening`을 켠 채로 두거나(기본값), `Map Name`을 지정해 호스트가 `?listen`과 함께 이동하게 하세요. 호스트가 Map Name을 지정했는데도 서버가 되지 않았다면 이동이 실패한 것입니다. 맵 경로(`/Game/Maps/YourMap`)를 확인하고, PIE라면 *Run Under One Process*가 꺼져 있는지 확인하세요.

## "스팀에서 첫 번째 플레이어만 들어오고, 그다음부터 참가가 실패합니다"

Host Params의 **Allow Join In Progress가 꺼져 있습니다.** 스팀에서는 켜 두세요.

이런 식으로 에러 로그가 뜰 수 있습니다.

```
LogEasySession: Joining session 'My Session' hosted by 'HostPlayer'
LogOnline: Warning: OSS: Async task 'FOnlineAsyncTaskSteamJoinLobby bWasSuccessful: 0 Session: GameSession LobbyId: Lobby[0x18600003DDB1FE9] Result: '3' k_EChatRoomEnterResponseNotAllowed (General Denied - You don't have the permissions needed to join the chat)' failed in 0.228409 seconds
LogEasySession: Warning: Session operation failed: JoinFailure (The online subsystem failed to join the session.)
```

스팀 세션은 로비이고, 엔진은 누가 로비에 들어오거나 나갈 때마다 그 로비가 플레이어를 받을지를 다시 계산합니다. 그 계산은 빈자리 검사에 `bAllowJoinInProgress`를 곱하는데, 매치가 시작됐는지는 보지 않습니다.

```cpp
// OnlineSessionAsyncLobbySteam.cpp, FillMembersFromLobbyData
bool bLobbyJoinable = Session.SessionSettings.bAllowJoinInProgress && (LobbyMemberCount < MaxLobbyMembers);
```

그래서 이 설정이 꺼져 있으면 첫 참가가 로비를 닫고, 아무것도 다시 열지 못합니다 - 플레이어가 나가도, `End Easy Session`을 불러도 마찬가지입니다. 새로 만든 로비가 열려 있는 이유는 생성 경로가 이 계산을 한 번도 돌리지 않기 때문이고, 그래서 딱 한 명만 들어옵니다. 닫힌 로비는 모두를 거부하므로 초대받은 친구도 거부됩니다.

EasySession은 이걸 우회하지 않습니다. 설정을 받은 그대로 온라인 서비스에 넘기고, 난입 거부는 매치 상태를 실제로 확인하는 참가 승인 비콘과 `PreLogin`이 직접 합니다. 스팀에서 "매치가 시작되면 참가 금지"가 필요하다면, Allow Join In Progress는 켜 두고 그 검사들에 맡기세요.

이렇게 참가가 실패한 플레이어는, 초대 때문에 원래 있던 세션을 먼저 나온 경우라면 메인 메뉴로 돌아갑니다. 세션 없는 맵에 남지 않습니다.

## "초대를 수락했는데 아무 일도 일어나지 않습니다"

이미 세션에 들어가 있는 상태이고, **Accept Invites While In Session이 꺼져 있습니다.** 기본값입니다. 초대받은 세션에 참가하지 않으며, 로그가 그 사실을 알려줍니다.

```
LogEasySession: Warning: Not joining the invited session: this player is already in one, and Accept Invites While In Session is disabled.
```

초대 수락은 플랫폼 오버레이의 클릭 한 번이고, 참가하면 이 플레이어가 있던 세션이 파괴됩니다. 그 세션을 호스팅 중이었다면 나머지 플레이어가 전부 끊깁니다. 그래서 기본 동작은 아무것도 하지 않고 게임이 결정하게 두는 것입니다.

`On Session Invite Accepted`는 그대로 발생하므로, 여기에 바인딩해서 플레이어에게 먼저 물어본 뒤 `Join Easy Session`을 직접 부르세요. 바로 참가하게 하려면 Project Settings -> Plugins -> EasySession에서 **Accept Invites While In Session**을 켜세요.

## "Travel 중에 Warning: Player ... is not part of session (GameSession)이 뜹니다"

**클라이언트 Travel 중 한 번 나오는 것은 정상이고, 엔진이 내는 것입니다.** 클라이언트가 이전 맵을 떠날 때 그 맵의 `APlayerState`가 파괴되면서, 온라인 서비스가 그 플레이어를 찾을 수 없는 세션에서 빼내려 시도합니다. 무시하세요. `LogOnlineSession`의 로그 레벨을 낮추면 진짜 경고까지 같이 가려집니다.

## "접속은 잘 됐는데 상대 플레이어가 내 화면에서 안 움직입니다"

세션 문제가 아니라, 폰에 클라이언트에서 서버로 가는 이동 복제가 없는 것입니다. 엔진의 `DefaultPawn`(GameMode 없이 나오는 날아다니는 구체)은 클라이언트에서 로컬로만 움직입니다. `ACharacter` 기반 폰을 쓰세요 (`CharacterMovementComponent`에 네트워크 이동이 전부 들어 있습니다). 예를 들어 Third Person 콘텐츠 팩을 추가하고 그 GameMode를 맵에 지정하면 됩니다.

## "엔진 내장 Create Session / Find Sessions 노드와 뭐가 다른가요?"

엔진에도 최소한의 세션 노드(`Create Session`, `Find Sessions` 등)가 있습니다. 빠른 프로토타입에는 쓸 만하지만, 옵션이 거의 없이 온라인 서비스를 직접 호출하고 **실패 사유를 주지 않습니다** (OnFailure 핀이 아무것도 싣지 않습니다). EasySession의 노드는 서브시스템을 거치며 다음을 더합니다. 작업 큐잉(EasySession 자신의 호출끼리는 절대 겹쳐서 서비스를 망가뜨리지 않음), 리슨 서버 자동 설정, 정확한 플레이어와 슬롯 집계, 타임아웃 대신 즉시 오는 상세한 에러, 커스텀 세션 데이터와 필터, 그리고 Quick Match 매치메이킹.

둘 중 하나를 골라서 계속 쓰세요. 큐잉은 EasySession을 거치는 호출만 덮으므로, 엔진 노드가 같은 시각에 돌면 그건 여전히 서비스에 따로 도달합니다. 바로 아래 항목을 참고하세요.

## "Another session search is already running, so this one was dropped"

EasySession 밖의 무언가가 검색을 돌리고 있고, 온라인 서비스는 한 번에 하나만 처리합니다. 두 번째 요청은 버려지면서 성공으로 보고되므로 콜백이 영영 오지 않습니다. EasySession은 이걸 알아채고, 노드를 타임아웃까지 매달아두는 대신 즉시 실패시킵니다.

대개는 옛 위젯에 그대로 연결된 엔진 내장 `Find Sessions` 노드이거나, 다른 세션 플러그인이거나, EasySession을 넣기 전에 쓰던 세션 코드입니다. 프로젝트에서 `FindSessions`를 직접 부르는 곳을 찾아 `Find Easy Sessions`로 바꾸거나, 둘이 동시에 돌지 않게 하세요.

`Find Easy Sessions`를 연달아 여러 번 부르는 것은 문제가 아닙니다. EasySession은 자기 요청을 큐에 넣고 순서대로 실행합니다.

## "SessionAlreadyExists가 뜨는데, 저는 세션에 없는 것 같은데요?"

들어가 있습니다. 대개 이전에 실패한 흐름이 남긴 것입니다. `Destroy Easy Session`을 먼저 부르고(상황이 꼬여 있어도 안전합니다) 다시 시도하세요. EasySession은 네트워크 연결이 끊기면 죽은 세션을 자동으로 파괴하므로, 이 문제는 주로 네트워크와 무관한 로직 버그(예: 이중 호스팅) 뒤에 생깁니다.

## "진행 중인 Quick Match를 취소할 수 있나요?"

가능합니다. `Cancel Easy Matchmaking`을 부르세요. Quick Match 노드가 `Canceled`와 함께 `OnFailure`를 발생시킵니다. 진행 중이던 온라인 작업은 먼저 끝나야 하므로(호출 도중에는 중단할 수 없습니다) 취소에 잠시 걸릴 수 있습니다.
