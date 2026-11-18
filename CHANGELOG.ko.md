# 변경 내역

*[English](CHANGELOG.md)*

## 1.1.0 - 출시 전

### 바뀐 점

- **Is Easy Session Busy는 이제 로컬 플레이어가 있는 세션이나 맵이 바뀌는 중인지를 뜻합니다.** Create Easy Session, Create Easy Party, Join Easy Session, Leave Easy Session, Start Easy Matchmaking, Restore Easy Party가 실행 중이거나 대기 중일 때 true입니다. 플러그인이 스스로 시작한 참가와 나가기도 포함되고, 호스트가 되거나 참가하거나 나간 뒤 이어지는 맵 로드 동안에도 true입니다.
- Find Easy Sessions, Update Easy Session, Start Easy Session, End Easy Session은 더 이상 Is Easy Session Busy를 true로 만들지 않습니다. 그래서 버튼을 Is Easy Session Busy에 연결한 메뉴가 Update Easy Session을 부를 때마다 버튼이 꺼졌다 켜지는 일이 없어집니다.
- Find Easy Sessions나 Update Easy Session이 도는 동안 Is Easy Session Busy로 자기 버튼을 꺼 두던 위젯은, 이제 그 노드의 On Success나 On Failure 핀이 나갈 때까지 직접 버튼을 꺼 두어야 합니다.
- Get Easy Session Activity는 이 네 노드도 계속 알려 줍니다. 그래서 Is Easy Session Busy가 false인 동안에도 None이 아닐 수 있습니다.
- 예제 위젯도 이 방식으로 자기 버튼을 끕니다.
  - `WBP_SessionBrowser`는 Find Easy Sessions의 On Success나 On Failure 핀이 나갈 때까지 검색 버튼과 결과 목록을 꺼 둡니다.
  - `WBP_UpdateSessionPopup`은 Update Easy Session의 On Success나 On Failure 핀이 나갈 때까지 `ApplyButton`을 꺼 둡니다.
  - `WBP_LobbyScreen`은 `StartMatchButton`을 누르면 그 버튼을 끄고, Start Easy Session의 On Failure 핀에서 다시 켭니다.
  - `WBP_EscPopup`은 `ReturnToLobbyButton`을 누르면 그 버튼을 끄고, 열릴 때마다 버튼 상태를 다시 맞춥니다.
- `WBP_SessionStatus`는 On Busy Changed 대신 On Activity Changed로 상태 줄을 갱신합니다.

### 추가된 점

- **On Activity Changed** 이벤트를 추가했습니다. EasySession 서브시스템(Get Easy Session Subsystem)과, 부모 클래스가 EasySessionWidget인 위젯에서 받을 수 있습니다. Get Easy Session Activity가 바뀔 때 호출되므로, 매 틱 Get Easy Session Activity를 읽지 않아도 상태 텍스트를 갱신할 수 있습니다.

### 고친 점

- 다른 프로그램이 비콘 포트를 잡고 있으면 Create Easy Party가 이제 `CreateFailure`로 실패합니다. 전에는 파티 비콘이 다음 빈 포트에 열리는데 파티는 설정된 포트를 광고해서, 파티는 만들어지지만 멤버의 Join Easy Session은 모두 `JoinRefused`("The party host did not answer.")로 끝났습니다. 호스트 로그에 두 포트가 남습니다.
- `WBP_PartyCard`가 이제 Event Construct에서 Is Easy Session Busy를 읽습니다. 전에는 Is Easy Session Busy가 true일 때 만들어진 카드가 On Busy Changed가 호출될 때까지 버튼을 켜 둔 채로 있었습니다.
- Steam 설정 가이드에서 "다른 프로젝트의 세션에 참가하면 `OutdatedClient`로 실패한다"는 문장을 고쳤습니다. EEasySessionResult와 EEasyDisconnectReason에는 그런 이름의 값이 없습니다. 이제 가이드는 "게임 버전이 달라 그런 세션에는 참가할 수 없다"고 안내합니다.

## 1.0.0

- Fab에 처음 출시했습니다.
