# EasySession

언리얼 엔진의 Online Subsystem(OSS) 위에 올린, **초심자를 위한 세션 & 매치메이킹 플러그인**.

방 만들기, 찾기, 참가, 퀵매치를 블루프린트 노드 몇 개로 - 커스텀 `GameInstance`도, C++도, 설정 파일도 없이 시작할 수 있습니다.

> 아직 개발 중입니다.

*[English README](README.md)*

## 설치

1. 이 폴더를 프로젝트의 `Plugins/` 아래에 복사합니다 (`내프로젝트/Plugins/EasySession/`).
2. 프로젝트를 열고 **Edit -> Plugins**에서 **EasySession**을 찾아 활성화한 뒤 재시작합니다.
3. C++ 프로젝트라면 `.Build.cs`의 `PublicDependencyModuleNames`에 `"EasySession"`을 추가합니다.

LAN 플레이는 이걸로 끝입니다. NULL 서브시스템은 계정도 키도 필요 없습니다. 스팀은 [Steam 설정 문서](Docs/Setup-Steam.md)를 따르세요.

## 어떻게 생겼나

메뉴 위젯에서 방 만들기:

```
[버튼 클릭] -> [Create Easy Session]
                 Session Display Name = "My First Session"
                 Map Name = "/Game/Maps/Lobby"
                 OnSuccess -> 이미 리슨 서버로 트래블까지 끝난 상태
                 OnFailure -> 결과 enum + 사람이 읽을 수 있는 에러 메시지
```

찾아서 들어가기:

```
[버튼 클릭]   -> [Find Easy Sessions] -> OnSuccess (Results) -> 서버 목록 UI 구성
[행 클릭]     -> [Join Easy Session]  -> OnSuccess -> 호스트로 트래블 중
```

서버 브라우저 없이 바로:

```
[버튼 클릭] -> [Quick Match Easy Session]   <- 검색 -> 최적 세션 참가 -> 없으면 직접 호스트
```

C++에서도 같은 API:

```cpp
UEasySessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UEasySessionSubsystem>();

FEasySessionHostParams Params;
Params.SessionDisplayName = TEXT("My First Session");
Params.MapName = TEXT("/Game/Maps/Lobby");

Sessions->CreateEasySession(Params, FEasySessionCompleteDelegate::CreateLambda(
    [](EEasySessionResult Result, const FString& ErrorMessage)
    {
        // Result로 무슨 일이 있었는지 정확히 알 수 있고, ErrorMessage는 그대로 유저에게 보여줘도 됩니다.
    }));
```

블루프린트 노드는 이 서브시스템을 얇게 감싼 것이라 양쪽 동작이 완전히 같습니다.

## 특징

- **노드 하나로 플레이** - `Quick Match Easy Session`이 검색하고, 가장 적합한 세션에 참가하고, 없으면 직접 호스트가 됩니다.
- **거부가 아니라 순서대로 처리** - OSS는 *같은 종류*의 중복 호출은 막지만, *서로 다른* 작업이 겹치는 건 막지 않습니다. EasySession은 모든 작업을 큐에 넣어 하나씩 실행하므로, 유저가 버튼을 연타해도 에러 대신 순서대로 처리됩니다.
- **"작업 중"의 범위가 정확함** - `Is Busy`는 큐에 있는 작업, 여러 단계로 진행되는 퀵매치, 그리고 호스트나 참가 뒤에 이어지는 레벨 로드까지 포함합니다. 여기에 UI를 묶으면 유저가 체감하는 작업 전 구간 동안 정확하게 동작합니다.
- **실패를 조용히 넘기지 않음** - 모든 작업이 결과 enum과 유저에게 보여줄 수 있는 메시지를 돌려줍니다. 온라인 서비스가 끝내 응답하지 않는 요청은 워치독이 실패시켜, 뒤에 쌓인 작업이 막히지 않게 합니다.
- **비밀번호와 난입 차단을 호스트가 강제** - `PreLogin`에서 검사하므로, 오래된 검색 결과나 직접 접속으로 진행 중인 매치에 들어올 수 없습니다.
- **확장 가능한 매치메이킹** - `ScoreSession` 함수 하나만 블루프린트나 C++로 오버라이드하면 원하는 기준을 넣을 수 있습니다.

## 지원하는 온라인 서브시스템

| 서브시스템 | 상태 |
|---|---|
| NULL (LAN) | 지원 |
| Steam | 지원 |
| EOS | 지원하지 않음 |

## 제약 사항

- **세션은 한 번에 하나.** 엔진의 `NAME_GameSession` 슬롯을 사용하므로 파티나 동시 다중 세션은 지원하지 않습니다.
- **로컬 플레이어 0번만.** 스플릿스크린은 지원하지 않습니다.
- **매치 중 맵 전환은 심리스 트래블이어야 합니다.** 호스트의 입장 게이트는 새 연결을 새 플레이어로 취급하므로, 매치 도중 하드 트래블을 하면 자기 플레이어들이 막힙니다. 플러그인이 수행하는 트래블은 이미 올바르게 처리합니다.
- **직접 `ClientTravel`로 비밀번호 세션에 들어갈 때**는 비밀번호 옵션을 직접 붙여야 합니다. 플러그인은 자신이 수행하는 트래블에만 붙입니다.
- **데디케이티드 서버**는 코드 경로는 있으나 아직 검증되지 않았습니다. 검증된 구성은 리슨 서버입니다.

## 엔진 지원

- 주 개발 버전: **UE 5.8**
- 지원 목표: UE 5.5 - 5.8

## 문서

문서는 영문으로 제공됩니다.

- [Quick Start](Docs/QuickStart.md) - 5분 만에 방 만들고 참가하기
- [Concepts](Docs/Concepts.md) - 세션, OSS, 리슨 서버와 데디케이티드의 차이
- 설정: [LAN](Docs/Setup-LAN.md) | [Steam](Docs/Setup-Steam.md)
- 가이드: [Sessions](Docs/Guide-Sessions.md) | [Quick Match matchmaking](Docs/Guide-QuickMatch.md) | [Dedicated servers](Docs/Guide-DedicatedServer.md)
- [API Reference](Docs/API.md)
- [FAQ & Troubleshooting](Docs/FAQ.md)

## 모듈

| 모듈 | 타입 | 설명 |
|---|---|---|
| `EasySession` | Runtime | 코어 서브시스템, 세션 | 매치메이킹 API, 블루프린트 노드 |
| `EasySessionEditor` | Editor | 설정 검증 및 에디터 도구 |

## 라이선스

[MIT](LICENSE) - 상업과 비상업 프로젝트 모두 자유롭게 사용할 수 있습니다.
