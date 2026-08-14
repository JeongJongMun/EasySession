# 설정 - Steam

*[English](Setup-Steam.en.md)*

세션을 스팀에서 돌립니다. 인터넷 플레이, 친구, 초대, 프레즌스를 쓸 수 있습니다.

> UE 5.8에서 PC 두 대와 스팀 계정 두 개로 검증했습니다(세션, 검색, 참가, 비밀번호, 초대, 친구, 오버레이).

## 준비물

테스트하는 **모든** PC에서 스팀 클라이언트가 실행 중이고, 서로 다른 계정으로 로그인되어 있어야 합니다.

## 1. 플러그인 켜기

Edit -> Plugins에서 둘 다 켜세요(재시작 필요).

- **Online Subsystem Steam** - 세션, 친구, 초대, 프레즌스.
- **Steam Sockets** - 게임 트래픽이 스팀 네트워크를 지나갑니다(NAT 통과 포함).

스팀 세션 주소는 `steam.7656...` 형태라, 넷드라이버가 이 형식을 이해해야 접속할 수 있습니다. 그 일을 하는 것이 Steam Sockets입니다.

> 오래된 가이드는 대신 `SteamNetDriver`를 지목합니다. 그건 다른 플러그인인 Socket Subsystem Steam (IP)의 것이니 켜지 마세요. 설정에 그 드라이버를 가리키는 줄이 남아 있으면 엔진이 **조용히** IP 드라이버로 폴백하고 모든 참가가 실패합니다.

## 2. DefaultEngine.ini

그대로 복사하세요.

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
```

`SteamDevAppId`는 Development 빌드에서만 읽힙니다. Shipping 빌드라면 실행 파일 옆, 즉 `Windows\<프로젝트>\Binaries\Win64\`에 `steam_appid.txt`를 두고 같은 값을 적으세요. **480**은 Valve가 공개한 테스트용 AppId(Spacewar)라 그대로 써도 됩니다.

`!NetDriverDefinitions=ClearArray` 줄이 중요합니다. 엔진 기본 설정에 이미 `GameNetDriver` 항목이 있고 **먼저 일치하는 항목이 이기므로**, 비우지 않고 덧붙이면 스팀 드라이버가 죽은 설정으로 남습니다.

같은 줄이 엔진 기본 `BeaconNetDriver`도 함께 지웁니다. 그래서 위 블록이 그걸 다시 넣어 줍니다. 비콘은 별도의 가벼운 연결로, 엔진과 다른 플러그인들이 로비, 좌석 예약, 질의에 사용합니다. 정의가 하나도 남지 않으면 비콘 생성 자체가 실패합니다. 프로젝트의 어떤 것도 비콘을 쓰지 않는다고 확신할 때만 이 줄을 빼세요.

> 테스트할 때 폴백을 주의하세요. SteamSockets가 시작하지 못하면 엔진은 실패시키는 대신 조용히 `IpNetDriver`로 내려갑니다. 그러면 LAN에서는 통과하고 인터넷에서 깨지므로, 접속됐다는 사실만 믿지 말고 로그에서 드라이버 클래스를 확인하세요.

## 3. 테스트 체크리스트

- 에디터 PIE로는 스팀 사용자 두 명을 표현할 수 없습니다. **PC 두 대에서 서로 다른 스팀 계정으로**, 패키지 빌드나 `-game`으로 실행해 테스트하세요.
- AppId 480은 Spacewar로 테스트하는 모두와 로비 공간을 공유하지만, 엔진이 빌드 ID가 다른 세션을 버리므로 검색에는 보통 자기 세션만 나옵니다. 같은 빌드로 테스트하는 팀이 여럿이라면 Host Params에 커스텀 세팅(예: `GameName = MyGameDev`)을 넣고 Search Params의 `Required Custom Settings`에 같은 값을 넣어 필터링하세요.

## 자주 겪는 함정

| 증상 | 원인 |
|---|---|
| `LogOnline: STEAM: Steam API failed to initialize` | 스팀 클라이언트가 실행 중이 아니거나, AppId를 못 찾음(`SteamDevAppId` 누락, Shipping이면 `steam_appid.txt` 누락) |
| 두 PC 사이에서 세션이 안 보임 | 양쪽이 같은 스팀 계정이거나, AppId가 다르거나, 한쪽 빌드가 낡음 |
| 초대/오버레이는 되는데 참가가 "connection to the host has been lost"로 실패 | 넷드라이버가 Steam Sockets가 아님. 플러그인이 꺼져 있거나, `ClearArray` 줄이 없거나, 설정이 아직 구형 `SteamNetDriver`를 가리킴. `EasySession.Diagnose`를 실행하세요 |
| PIE에서는 다 되는데 패키지에서는 안 됨 | PIE가 조용히 NULL을 쓰고 있었음. 로그의 `EasySessionSubsystem initialized. Online subsystem: STEAM` 줄을 확인하세요 |

막히면 시작 로그에서 `===== EasySession diagnostics =====` 블록을 찾거나, 아무 때나 `EasySession.Diagnose`를 실행하세요. 활성 서브시스템, 위의 모든 ini 키, 설정한 넷드라이버 클래스가 실제로 로드되는지, 스팀 로그인 상태까지 확인해 줍니다.
