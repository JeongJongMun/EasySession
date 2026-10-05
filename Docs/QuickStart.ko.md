# Quick Start - 5분 만에 세션 만들고 참가하기

*[English](QuickStart.en.md)*

빈 프로젝트에서 두 게임 인스턴스가 LAN으로 함께 플레이하는 데까지, 블루프린트만으로 갑니다. 커스텀 GameInstance도, C++도, 설정 파일을 직접 고칠 일도 없습니다.

## 1. 플러그인 켜기

1. **Edit -> Plugins**에서 **EasySession**을 찾아 활성화합니다.
2. 재시작하라는 안내가 뜨면 재시작합니다.

설정은 이걸로 끝입니다. EasySession은 별도 설정 없이 NULL(LAN) 온라인 서브시스템에서 동작하며, 계정도 키도 필요 없습니다.

## 2. 먼저 예제를 실행해보기

플러그인에 메인 메뉴, 로비, 매치가 완성된 예제가 들어 있습니다. 첫 노드를 배선하는 것보다
빠르고, 완성된 흐름이 어떤 모습인지 보여줍니다.

1. 콘텐츠 브라우저에서 **Settings -> Show Plugin Content**를 켭니다.
2. **Project Settings -> Maps & Modes -> Game Default Map**을 `L_Example_MainMenu`로
   설정합니다. 세션을 나가거나 연결이 끊기면 플레이어는 Game Default Map으로
   돌아옵니다. 예제 메뉴를 가리켜야 왕복이 출발한 곳에서 끝납니다. 나중에 자기
   게임에서도 같은 원리입니다 - 메뉴 맵이 이 자리에 들어갑니다.
3. `/EasySession/Examples/Maps/L_Example_MainMenu`을 엽니다.
4. [6단계](#6-pie로-테스트하기)대로 플레이어를 2명으로 맞추고 Play를 누릅니다.
5. 한쪽 창에서 **CREATE SESSION**을 눌러 세션을 만듭니다. 다른 창에서 **FIND SESSIONS**, **SEARCH**, **JOIN** 순서로 누릅니다.

이 예제를 구성하는 위젯은 `/EasySession/Examples/UI/`에 있습니다. 위젯마다 한 가지 일을 맡고
그 일에 필요한 노드를 직접 부르므로, 하나만 떼어 읽거나 자기 게임으로 복사해 가도 됩니다.
`WBP_MainMenu`는 화면을 배치하고 전환하는 일만 합니다. 화면 아래 상태 줄은
`Modules/WBP_SessionStatus`입니다. 플러그인 이벤트를 한 번 바인딩해 두고, 누가 시작했든
지금 도는 요청을 서술하는 위젯이라 세션 진행을 보여줄 화면 어디에나 올려놓으면 됩니다.

| 위젯 | 하는 일 | 주요 노드 |
|---|---|---|
| `WBP_MainMenu` | Home, Create Session, Find Sessions, Create Party, Find Parties 화면. 브라우저가 요청한 결과에 참가합니다 | Start Easy Matchmaking, Create Easy Session, Join Easy Session, Create Easy Party, Join Easy Party |
| `Modules/WBP_SessionSettingsForm` | 세션 설정 입력. Create Session과 설정 변경 팝업이 함께 씁니다 | Make / Break Easy Session Settings |
| `Modules/WBP_SessionBrowser` | Public, Friends, Code 탭이 있는 Find Sessions와 Find Parties | Find Easy Sessions, Find Easy Friend Sessions, Find Easy Parties |
| `Modules/WBP_PartyCard` | 내 파티: 만들기, 코드로 참가, 찾기, 준비, 나가기 | Join Easy Party, Set Easy Party Ready, Leave Easy Party, Get Easy Party Settings, Get Easy Party Join Code |
| `Modules/WBP_CreatePartyForm` | 새 파티의 최대 인원, 숨김, 참가 코드 | Make Easy Party Settings |
| `Modules/WBP_PartyMemberList` | 파티 멤버. 빈 자리를 누르면 초대 오버레이, 멤버를 누르면 프로필이 열립니다 | Get Easy Party Members, Show Easy Party Invite UI, Show Easy Profile UI For Party Member, Kick Easy Party Member |
| `WBP_Lobby` | 로비: 준비, 매치 시작, 세션 설정, 나가기 | Set Easy Session Ready, Start Easy Session, Server Travel Easy Session, Leave Easy Session |
| `Modules/WBP_PlayerList` | 세션 플레이어. 빈 자리와 프로필 클릭은 파티 목록과 같습니다 | Get Easy Session Player Infos, Show Easy Invite UI, Show Easy Profile UI For Player, Kick Easy Session Player |
| `Modules/WBP_SessionInfo` | 세션 설정을 한눈에 | Get Easy Session Settings, Get Easy Session Join Code, Get Easy Session State |
| `Modules/WBP_SessionStatus` | 상태 줄 | Get Easy Session Activity, Get Activity Message, On Session Failure |
| `WBP_InGame`, `Popups/WBP_EscPopup` | 매치와 Esc 메뉴: 나가기, 로비로 돌아가기, 세션 끝내기 | Leave Easy Session, End Easy Session, Server Travel Easy Session, Destroy Easy Session For Everyone |
| `Popups/WBP_UpdateSessionPopup` | 로비에서 세션 설정을 바꿉니다 | Update Easy Session |
| `Popups/WBP_JoinPasswordPopup`, `Popups/WBP_DisconnectPopup` | 비밀번호 입력, 그리고 지난 세션이나 파티가 끝난 이유 | Join Easy Session, Consume Pending Easy Disconnect Info (메인 메뉴가 부름) |

`Modules/`의 나머지는 플러그인 노드를 쓰지 않는 공통 부품입니다. `WBP_MenuButton`, `WBP_TabBar`,
`WBP_InfoRow`, `WBP_PopupFrame`, `WBP_PlayerRow`와 입력 위젯들입니다. 파티를 쓰지 않는 게임은
메인 메뉴에서 `WBP_PartyCard`만 빼면 되고, 나머지는 그대로 동작합니다.

## 3. 세션 만들기

아무 블루프린트에서나 됩니다(메뉴 위젯의 버튼이든, 빠르게 확인하려면 레벨 블루프린트든).

```
[Button Clicked] -> [Create Easy Session]
                      HostParams:
                        Session Display Name = "My First Session"
                        Initial Map Name = "/Game/Maps/Lobby"   <- 여기에 본인 맵
                      OnSuccess -> (이제 호스트입니다)
                      OnFailure -> [Print String: ErrorMessage]
```

`Create Easy Session` 하나가 호스트에게 필요한 일을 전부 합니다.

- 세션을 만들고 광고합니다
- Initial Map Name으로 `?listen`을 붙여 Travel하며, 이것이 이 게임을 서버로 만듭니다
- **Initial Map Name**은 필수입니다. 맵이 없으면 접속할 서버도 없으므로 생성이 `InvalidParams`로 실패합니다
- 본인을 참가자로 등록하므로 세션의 인원 수가 정확하게 표시됩니다

지원하는 구성은 리슨 서버입니다. 데디케이티드 서버에서 세션을 호스팅하는 것은
아직 지원하지 않습니다.

## 4. 다른 인스턴스에서 찾아 참가하기

```
[Button Clicked] -> [Find Easy Sessions]
                      OnSuccess (Results) -> [ForEach] -> 서버 목록 UI에 행 추가
                      OnFailure -> [Print String: ErrorMessage]

[Row Clicked] -> [Join Easy Session]
                   SearchResult = (그 행의 검색 결과)
                   OnSuccess -> (호스트로 자동 이동 중)
                   OnFailure -> [Print String: ErrorMessage]
```

모든 실패 핀이 `Result` 열거형과 플레이어에게 그대로 보여줄 수 있는 메시지를 넘겨줍니다.

각 행에는 표시할 이름만이 아니라 `SearchResult` 구조체를 통째로 들고 계세요.
`Join Easy Session`이 그것을 다시 받습니다.

비밀번호가 틀리거나, 세션이 꽉 찼거나, 매치가 참가를 마감했으면 노드가 바로 여기서 실패합니다. `Result`가
어느 쪽인지 말해주고 `ErrorMessage`에 호스트가 쓴 이유가 담기며, 로딩 화면은 뜨지 않습니다.
[호스트에게 먼저 묻기](Guide-Sessions.ko.md#호스트에게-먼저-묻기)를 보세요.

## 5. 아니면 Matchmaking 하나로 끝내기

```
[Button Clicked] -> [Start Easy Matchmaking]
                      MatchmakingParams:
                        Host -> Initial Map Name = "/Game/Maps/Lobby"   <- 여기에 본인 맵
                      OnSuccess -> (가장 좋은 세션에 참가했거나, 직접 호스트가 됨)
                      OnFailure -> [Print String: ErrorMessage]
```

Matchmaking는 검색하고, 가장 좋은 세션(핑이 좋고 더 찬 세션 우선)에 참가하고, 없으면 직접 세션을
만듭니다. 어느 쪽이 됐는지는 `Is Easy Session Host`로 확인합니다.

**Host > Initial Map Name을 비워두면** 직접 호스트가 될 때 지금 있는 맵에서 리슨 서버를 엽니다.
메뉴에서 Matchmaking를 부른다면 메뉴 맵이 경기장이 되므로, 대개는 채우는 게 맞습니다. 이
직접 호스트가 되게 하려면 **Allow Host Fallback**을 켜세요. 기본값은 꺼짐입니다.

## 6. PIE로 테스트하기

1. **Edit -> Editor Preferences -> Level Editor -> Play**에서 **Number of Players = 2**,
   **Net Mode = Play Standalone**으로 설정합니다.
2. Play를 누르면 창이 두 개 뜹니다.
3. 1번 창에서 세션을 만들고, 2번 창에서 찾아 참가합니다.

같은 설정에서 **Run Under One Process**를 끄세요. 켜면 두 창이 한 프로세스와 하나의 LAN 비콘
포트를 공유해서, 서로를 찾을 때도 있고 못 찾을 때도 있습니다. 프로세스를 분리하면 패키징된
빌드와 같은 네트워크 경로를 씁니다.

> 팁: UI 없이 콘솔 명령(`~` 키)만으로도 전부 테스트할 수 있습니다.
> `EasySession.Host /Game/Maps/Lobby`, `EasySession.Find`, `EasySession.Join 0`, `EasySession.Matchmaking`,
> `EasySession.Destroy`, `EasySession.Status`. 개발 빌드에만 있고 Shipping 빌드에서는
> 컴파일 단계에서 빠집니다.

## 다음 단계

- [Concepts](Concepts.ko.md) - 세션이 실제로 무엇인지, NULL과 스팀이 무슨 뜻인지
- [LAN 설정](Setup-LAN.ko.md) - 로컬 검색이 깨지는 원인과 한 대에서 테스트하는 법
- [Steam 설정](Setup-Steam.ko.md) - 인터넷 플레이에 필요한 플러그인 두 개와 ini 설정
- [세션 가이드](Guide-Sessions.ko.md) - 커스텀 데이터, 필터, 비밀번호, 세션 정보 변경
- [Matchmaking 가이드](Guide-Matchmaking.ko.md) - 세션을 고르는 기준과 커스텀 점수 계산
- [API 레퍼런스](API.ko.md) - 모든 노드, 조회, 구조체, 설정
- [FAQ](FAQ.ko.md) - 자주 겪는 문제
