# 설정 - Steam

*[English](Setup-Steam.en.md)*

세션을 스팀에서 사용합니다. 인터넷 플레이, 친구, 초대, 프레즌스를 쓸 수 있습니다.

> UE 5.8에서 PC 두 대와 스팀 계정 두 개로 검증했습니다(세션, 검색, 참가, 비밀번호, 초대, 친구, 오버레이, 파티).

## 준비물

테스트하는 **모든** PC에서 스팀 클라이언트가 실행 중이고, 서로 다른 계정으로 로그인되어 있어야 합니다.

## 1. 플러그인 켜기

Edit -> Plugins에서 둘 다 켜세요(재시작 필요).

- **Online Subsystem Steam** - 세션, 친구, 초대, 프레즌스.
- **Steam Sockets** - 게임 트래픽을 스팀 네트워크로 주고받습니다(NAT 통과 포함).

스팀 세션 주소는 `steam.7656...` 형태라, 넷드라이버가 이 형식을 이해해야 접속할 수 있습니다. 그 일을 하는 것이 Steam Sockets입니다.

> 오래된 가이드에는 `SteamNetDriver`가 나옵니다. 스팀의 예전 네트워킹 API를 쓰는 드라이버이고, 엔진 버전에 따라 있는 곳이 다릅니다. UE 5.5에서는 Online Subsystem Steam 안에 `/Script/OnlineSubsystemSteam.SteamNetDriver`로 들어 있고, UE 5.8에서는 별도 플러그인인 Socket Subsystem Steam (IP)으로 옮겨졌습니다. 엔진이 불러올 수 없는 드라이버 클래스를 설정에 적으면 엔진이 **조용히** IP 드라이버로 폴백하고 모든 참가가 실패합니다. 이미 `SteamNetDriver`로 접속되는 프로젝트는 그대로 써도 되지만, 새 프로젝트는 스팀의 현재 네트워킹 API를 쓰는 Steam Sockets를 쓰세요.

## 2. DefaultEngine.ini

아래 내용을 그대로 붙여 넣으세요.

```ini
[OnlineSubsystem]
DefaultPlatformService=Steam

[OnlineSubsystemSteam]
bEnabled=true
SteamDevAppId=480
bInitServerOnClient=true

[/Script/Engine.GameEngine]
!NetDriverDefinitions=ClearArray
+NetDriverDefinitions=(DefName="GameNetDriver",DriverClassName="/Script/SteamSockets.SteamSocketsNetDriver",DriverClassNameFallback="/Script/OnlineSubsystemUtils.IpNetDriver")
+NetDriverDefinitions=(DefName="BeaconNetDriver",DriverClassName="/Script/SteamSockets.SteamSocketsNetDriver",DriverClassNameFallback="/Script/OnlineSubsystemUtils.IpNetDriver")
+NetDriverDefinitions=(DefName="DemoNetDriver",DriverClassName="/Script/Engine.DemoNetDriver",DriverClassNameFallback="/Script/Engine.DemoNetDriver")
```

`SteamDevAppId`는 Development 빌드에서만 읽힙니다. Shipping 빌드라면 실행 파일 옆, 즉 `Windows\<프로젝트>\Binaries\Win64\`에 `steam_appid.txt`를 두고 같은 값을 적으세요. **480**은 Valve가 공개한 테스트용 AppId(Spacewar)라 그대로 써도 됩니다.

`[/Script/Engine.GameEngine]`에 적은 줄은 엔진 기본 드라이버 목록을 통째로 대체합니다.
기본값은 엔진 기본 설정의 `[/Script/Engine.Engine]`에 있고, `GameEngine` 섹션에 `NetDriverDefinitions`가 하나라도 있으면 물려받은 목록은 버려집니다.
그래서 위 블록은 스팀 드라이버만이 아니라 게임에 필요한 드라이버를 모두 적습니다.

- `GameNetDriver`는 게임 연결을 맡습니다.
- `BeaconNetDriver`는 비콘을 맡습니다. 비콘은 엔진과 플러그인이 로비, 좌석 예약, 질의에 쓰는 별도의 가벼운 연결입니다. EasySession의 예약 비콘에 필요하고, 정의가 하나도 없으면 비콘 생성 자체가 실패합니다.
- `DemoNetDriver`는 리플레이를 녹화하고 재생합니다.

`!NetDriverDefinitions=ClearArray`는 다른 설정 파일이 같은 섹션에 넣은 항목까지 비워서, 위 세 줄만 남게 합니다.

> 테스트할 때 폴백을 주의하세요. SteamSockets가 시작하지 못하면 엔진은 실패하지 않고 조용히 `IpNetDriver`로 바꿉니다. 이러면 LAN에서는 되다가 인터넷에서 안 되므로, 접속됐다는 사실만 믿지 말고 로그에서 드라이버 클래스를 확인하세요.

## 3. 테스트 체크리스트

- 에디터 PIE로는 스팀 사용자 두 명을 동시에 쓸 수 없습니다. **PC 두 대에서 서로 다른 스팀 계정으로**, 패키지 빌드나 `-game`으로 실행해 테스트하세요.
- AppId 480은 Spacewar로 테스트하는 모두와 로비 공간을 공유합니다. 엔진은 빌드 ID가 같은 세션만 남기지만, 이 ID는 엔진 버전으로 정해지므로 같은 엔진 버전을 쓰는 다른 프로젝트의 세션도 검색과 매치메이킹에 나옵니다. 그런 세션에 참가하면 `OutdatedClient`로 실패합니다. 자기 세션만 보려면 Host Params에 커스텀 세팅(예: `GameName = MyGameDev`)을 넣고 Search Params의 `Required Custom Settings`에 같은 값을 넣어 필터링하세요.

### 스팀에서 파티 확인하기

파티는 게임 세션 옆에 따로 있는 스팀 로비이고, 호스트는 예약 비콘과 같은 포트에서 파티 비콘을 엽니다. 초대 오버레이에는 친구만 나오므로 두 계정이 스팀 친구여야 합니다. 두 PC에서 콘솔(`~`)을 열고 진행합니다.

1. **초대.** A: `EasySession.CreateParty 4 hidden`(숨긴 파티), 이어서 `EasySession.PartyInviteUI`로 B를 초대합니다. B가 오버레이에서 수락하면 참가합니다. B의 로그에 호스트의 스팀 주소가 보입니다: `Connecting to the party host at steam.<id>:...`
2. **목록에 보이는 파티.** A: `EasySession.CreateParty 4`. B: `EasySession.FindParties`, 이어서 `EasySession.JoinParty`.
3. **참가 코드.** A: `EasySession.CreateParty 4 code`, 이어서 `EasySession.Party`가 코드를 출력합니다. B: `EasySession.FindParties <코드>`, 이어서 `EasySession.JoinParty`.
4. **따라가기.** A: `EasySession.Host <맵>`. B가 A의 세션으로 따라오고, 둘 다 `MovedToGameSession`이 담긴 `OnPartyLeft`를 받습니다.
5. **복원.** 매치를 끝냅니다. 예를 들어 A가 매치를 나갑니다. 메뉴에서 A가 먼저 파티 카드의 REJOIN PARTY를 누르고, 이어서 B가 누릅니다. B가 다시 멤버가 됩니다.
6. **매치 중 초대.** B가 게임 세션 안에서 파티 초대를 수락합니다. B는 참가하지 않고, 로그에 `Not joining the invited party during a game session.`이 남습니다.

## 자주 겪는 함정

| 증상 | 원인 |
|---|---|
| `LogOnline: STEAM: Steam API failed to initialize` | 스팀 클라이언트가 실행 중이 아니거나, AppId를 못 찾음(`SteamDevAppId` 누락, Shipping이면 `steam_appid.txt` 누락) |
| 두 PC 사이에서 세션이 안 보임 | 양쪽이 같은 스팀 계정이거나, AppId가 다르거나, 한쪽 빌드가 오래됨 |
| 초대/오버레이는 되는데 참가가 "connection to the host has been lost"로 실패 | 넷드라이버가 Steam Sockets가 아님. 플러그인이 꺼져 있거나, `ClearArray` 줄이 없거나, 설정이 아직 구형 `SteamNetDriver`를 가리킴. `EasySession.Diagnose`를 실행하세요 |
| PIE에서는 다 되는데 패키지에서는 안 됨 | PIE가 조용히 NULL을 쓰고 있었음. 로그의 `EasySessionSubsystem initialized. Online subsystem: STEAM` 줄을 확인하세요 |
| 초대를 수락해도 아무 일이 없고, 로그에 `search already in progress when accepting invite`가 남음 | 세션 검색이 도는 동안 수락한 초대는 엔진이 무시합니다. 검색이 끝난 뒤 다시 수락하세요([FAQ](FAQ.ko.md)) |

막히면 시작 로그에서 `===== EasySession diagnostics`로 시작하는 블록을 찾거나, 아무 때나 `EasySession.Diagnose`를 실행하세요. 활성 서브시스템, 위의 모든 ini 키, 설정한 넷드라이버 클래스가 실제로 로드되는지, 스팀 로그인 상태까지 확인해 줍니다.
