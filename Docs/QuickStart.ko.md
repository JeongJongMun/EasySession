# Quick Start - 5분 만에 방 만들고 참가하기

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
5. 한쪽 창에서 방을 만들고, 다른 창에서 Find와 Join을 합니다.

이 예제를 구성하는 위젯은 `/EasySession/Examples/UI/`에 있습니다. `WBP_MainMenu`를 먼저
보세요. 아래 단계에 나오는 노드를 전부 씁니다.

## 3. 방 만들기

아무 블루프린트에서나 됩니다(메뉴 위젯의 버튼이든, 빠르게 확인하려면 레벨 블루프린트든).

```
[Button Clicked] -> [Create Easy Session]
                      HostParams:
                        Session Display Name = "My First Session"
                        Map Name = "/Game/Maps/Lobby"   <- 여기에 본인 맵
                      OnSuccess -> (이제 호스트입니다)
                      OnFailure -> [Print String: ErrorMessage]
```

`Create Easy Session` 하나가 호스트에게 필요한 일을 전부 합니다.

- 세션을 만들고 광고합니다
- Map Name으로 `?listen`을 붙여 Travel하며, 이것이 이 게임을 서버로 만듭니다
- **Map Name**을 비워두면 대신 현재 맵에서 리슨을 시작합니다
- 본인을 참가자로 등록하므로 세션의 인원 수가 정확하게 표시됩니다

두 Travel 동작은 기본값인 **Host Mode = Listen Server**를 전제로 합니다. 데디케이티드
서버는 실행된 맵을 그대로 유지합니다. [데디케이티드 서버 가이드](Guide-DedicatedServer.md)를 보세요.

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

모든 실패 핀이 `Result` enum과 플레이어에게 그대로 보여줄 수 있는 메시지를 넘겨줍니다.

각 행에는 표시할 이름만이 아니라 `SearchResult` 구조체를 통째로 들고 계세요.
`Join Easy Session`이 그것을 다시 받습니다.

비밀번호가 틀리거나 매치가 참가를 마감했으면 노드가 바로 여기서 실패합니다. `Result`가
어느 쪽인지 말해주고 `ErrorMessage`에 호스트가 쓴 이유가 담기며, 로딩 화면은 뜨지 않습니다.
[비밀번호로 잠근 세션](Guide-Sessions.md#password-protected-sessions)을 보세요.

## 5. 아니면 Quick Match 하나로 끝내기

```
[Button Clicked] -> [Quick Match Easy Session]
                      QuickMatchParams:
                        Host -> Map Name = "/Game/Maps/Lobby"   <- 필수
                      OnSuccess -> (가장 좋은 방에 참가했거나, 직접 호스트가 됨)
                      OnFailure -> [Print String: ErrorMessage]
```

Quick Match는 검색하고, 가장 좋은 방(핑이 좋고 더 찬 방 우선)에 참가하고, 없으면 직접 방을
만듭니다. 어느 쪽이 됐는지는 `Is Easy Session Host`로 확인합니다.

**Host > Map Name에는 기본값이 없습니다.** 매치메이킹이 매치를 어디서 할지 대신 정해줄 수
없기 때문입니다. 비워두면 아무도 접속할 수 없는 방을 만드는 대신 `InvalidParams`로 즉시
실패합니다. 이 게임이 참가만 해야 한다면 **Allow Host Fallback**을 끄세요.

## 6. PIE로 테스트하기

1. **Edit -> Editor Preferences -> Level Editor -> Play**에서 **Number of Players = 2**,
   **Net Mode = Play Standalone**으로 설정합니다.
2. Play를 누르면 창이 두 개 뜹니다.
3. 1번 창에서 방을 만들고, 2번 창에서 찾아 참가합니다.

두 창이 서로의 세션을 못 찾으면 같은 설정에서 **Run Under One Process**를 끄세요. 프로세스를
분리하면 패키징된 빌드와 같은 네트워크 경로를 씁니다.

> 팁: UI 없이 콘솔 명령(`~` 키)만으로도 전부 테스트할 수 있습니다.
> `EasySession.Host`, `EasySession.Find`, `EasySession.Join 0`, `EasySession.QuickMatch`,
> `EasySession.Destroy`, `EasySession.Status`. 개발 빌드에만 있고 Shipping 빌드에서는
> 컴파일 단계에서 빠집니다.

## 다음 단계

- [Concepts](Concepts.ko.md) - 세션이 실제로 무엇인지, NULL과 스팀이 무슨 뜻인지
- [LAN 설정](Setup-LAN.md) - 로컬 검색이 깨지는 원인과 한 대에서 테스트하는 법
- [Steam 설정](Setup-Steam.md) - LAN을 넘어 인터넷으로
- [세션 가이드](Guide-Sessions.md) - 커스텀 데이터, 필터, 비밀번호, 세션 정보 변경
- [Quick Match 가이드](Guide-QuickMatch.md) - 방을 고르는 기준과 커스텀 점수 계산
- [API 레퍼런스](API.ko.md) - 모든 노드, 조회, 구조체, 설정
- [FAQ](FAQ.md) - 다들 물어보는 것들
