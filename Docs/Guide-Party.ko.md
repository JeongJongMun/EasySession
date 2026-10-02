# 가이드 - 파티

*[English](Guide-Party.en.md)*

파티는 게임 세션 밖에서 함께 움직이는 소수의 플레이어 묶음입니다. 예를 들어 친구들이 플레이하기 전에 메인 메뉴에서 모이는 경우입니다. 행선지는 리더가 정합니다. 리더가 게임 세션을 만들거나, 참가하거나, 매치메이킹으로 들어가면 모든 멤버가 따라갑니다.

파티는 선택 사항입니다. 파티를 한 번도 만들지 않는 게임은 지금과 똑같이 동작하고, 모든 Session 노드는 계속 게임 세션에 대해서만 답합니다. 파티는 **EasySession | Party** 아래에 자기 노드를 따로 가집니다.

| | 게임 세션 | 파티 |
|---|---|---|
| 무엇인가 | 플레이어가 찾고, 참가하고, 플레이하는 매치 | 매치와 매치 사이를 함께 이동하는 묶음 |
| 맵 | 있습니다. 호스트가 서버입니다 | 없습니다. 파티를 만들거나 참가해도 아무도 이동하지 않습니다 |
| 수명 | 호스트가 나가거나 없앨 때까지 | 게임 세션 밖에서만 존재합니다. 게임 세션에 들어가면 닫히고, 매치가 끝나면 돌아옵니다 |
| 개수 | 플레이어당 하나 | 플레이어당 하나, 게임 세션과 따로 |

## 파티 만들기

`Create Easy Party`에 `FEasyPartySettings`를 넘깁니다.

| 필드 | 기본값 | 설명 |
|---|---|---|
| Max Members | 4 | 리더를 포함해 파티가 담을 인원. 2 이상 |
| Privacy | Invite Only | 누가 들어올 수 있는지. 아래 [누가 들어올 수 있나](#누가-들어올-수-있나) 참고 |

로컬 플레이어가 리더가 됩니다. 멤버를 id로 구분하기 때문에, 온라인 서브시스템에 로그인한 플레이어가 있어야 만들 수 있습니다. NULL은 알아서 로그인하고, 스팀은 스팀 사용자로 로그인합니다.

이미 파티에 있거나 게임 세션 안에 있으면 `SessionAlreadyExists`로 실패합니다. 파티는 게임 세션 밖에서만 존재하기 때문입니다.

스팀에서 파티는 스팀 로비이므로 Presence가 필요하고, 게임 세션처럼 인터넷 너머로 동작합니다. NULL에서는 LAN 세션입니다.

## 누가 들어올 수 있나

`Privacy`는 두 가지를 함께 정합니다. 검색에 파티가 보이는지, 그리고 리더가 누구를 받는지입니다.

| Privacy | Find Easy Parties | 리더가 받는 사람 |
|---|---|---|
| Invite Only | 숨김 | 리더가 초대한 플레이어, 리더의 친구, 매치 전 파티의 멤버 |
| Join Code | 숨김, 코드로만 찾음 | 코드를 가진 누구나 |
| Public | 보임 | 누구나 |

Invite Only는 숨기기만 하는 것이 아닙니다. 리더가 접속하는 모든 플레이어를 검사하므로, 다른 경로로 파티를 찾아낸 플레이어도 거절됩니다. 그래서 `Privacy`는 게임 세션처럼 `Hidden`과 `Use Join Code` 두 플래그가 아니라 값 하나입니다. 숨긴 게임 세션은 검색 결과를 가진 사람이면 누구나 들어올 수 있고, 막는 수단은 비밀번호뿐입니다.

플랫폼 초대 오버레이는 누구를 초대했는지 게임에 알려 주지 않습니다. 그래서 Invite Only 파티는 리더의 친구를 모두 받습니다. `Send Easy Party Invite To Friend`로 초대한 친구도 같은 방식으로 받습니다.

Join Code 파티는 자동으로 만든 6자리 코드를 광고합니다. 리더는 `Get Easy Party Join Code`로 코드를 읽어 화면에 띄우고 공유합니다. 멤버도 모두 읽을 수 있습니다.

`Get Easy Party Settings`는 파티를 만들 때 쓴 설정을 돌려줍니다. 리더와 모든 멤버에서 동작합니다. 파티를 "2/4"로 보여 주거나 "Invite Only"라고 표시할 때 씁니다.

## 파티 찾기와 참가

`Find Easy Parties`는 `Find Easy Sessions`와 같은 `FEasySessionSearchParams`를 받고, 파티만 돌려줍니다.

- `Join Code`가 비어 있으면 Public 파티를 나열합니다.
- `Join Code`가 있으면 그 코드를 광고하는 파티 하나를 돌려줍니다.
- 결과의 `Session Display Name`과 `Host Name`은 리더 이름입니다. `Max Players`와 `Open Slots`는 멤버 수를 셉니다.
- 파티는 지역, 커스텀 세팅, 매치 상태를 광고하지 않으므로 `Region`, `Required Custom Settings`, `Include In Progress Sessions`는 무시됩니다. 게임 세션 검색에 쓰던 SearchParams를 그대로 넘겨도 됩니다.

`Join Easy Party`에 그 결과 하나를 넘깁니다. 참가는 리더가 정하고, 거절되면 노드가 `JoinRefused`와 리더의 사유로 실패합니다. 예를 들어 "The party is full."이나 "This party only admits players the leader invited."입니다. 성공하면 `Get Easy Party Members`에 이미 로컬 플레이어가 들어 있습니다.

이미 파티에 있거나 게임 세션 안에 있는 플레이어는 `SessionAlreadyExists`로 실패합니다. 먼저 `Leave Easy Party`나 `Leave Easy Session`을 부르세요.

## 초대

초대는 리더만 합니다.

- `Send Easy Party Invite To Friend`는 `Read Easy Friends`가 돌려준 친구 한 명을 초대합니다.
- `Show Easy Party Invite UI`는 파티용 플랫폼 초대 오버레이를 엽니다.

멤버가 부르면 `RequiresPartyLeader`, 초대가 없는 NULL/LAN에서는 `NotSupportedByService`를 돌려줍니다.

플레이어가 파티 초대를 수락하면 `OnSessionInviteAccepted`가 `Is Party`가 true인 결과와 함께 옵니다. **Auto Join Accepted Invites**가 켜져 있으면(기본값) 이벤트 직후 플러그인이 파티에 참가합니다.

- 돌고 있는 매치메이킹은 먼저 취소됩니다.
- 이미 파티에 있는 플레이어는 먼저 그 파티를 나갑니다.
- 게임 세션 안에 있는 플레이어는 참가하지 않습니다. 오버레이 클릭 한 번으로 매치가 끝나면 안 되기 때문입니다. 로그에 그 사실이 남고, 게임은 `Leave Easy Session`을 부른 뒤 이벤트의 결과로 `Join Easy Party`를 부를 수 있습니다.

이 참가를 기다리는 노드가 없으므로, 실패는 `OnSessionFailure`로 옵니다. Auto Join Accepted Invites를 끄면 이벤트만 옵니다. `Is Party`를 보고 `Join Easy Party`나 `Join Easy Session`을 직접 부르세요.

## 멤버, 준비, 추방

`Get Easy Party Members`는 리더를 포함한 모든 멤버를 `FEasyPartyMemberInfo`로 돌려줍니다. 이름, 로컬 플레이어인지, 리더인지, 준비했는지, 플레이어 id가 들어 있습니다. 누가 들어오거나, 나가거나, 준비 상태를 바꾸면 리더와 모든 멤버에게 `OnPartyMembersChanged`가 옵니다. 이벤트가 올 때 목록은 이미 바뀌어 있으므로, 파티 패널은 목록을 읽고 다시 그리면 됩니다.

`Set Easy Party Ready`는 로컬 플레이어의 준비 상태를 바꿉니다. 플러그인은 값을 공유하기만 합니다. 준비하면 무엇을 할 수 있는지는 게임이 정합니다. 예를 들어 모두 준비하면 리더의 Play 버튼을 켜는 식입니다.

`Kick Easy Party Member`는 멤버를 내보내고 이 파티에 다시 들어오지 못하게 합니다. 내보내진 멤버에게는 `Kicked`와 리더의 사유가 담긴 `OnPartyLeft`가 옵니다. 리더 전용이라 멤버가 부르면 `RequiresPartyLeader`를 받습니다.

## 파티 나가기

`Leave Easy Party`로 파티를 나갑니다. 리더가 나가면 파티를 리더가 들고 있으므로 모든 멤버의 파티가 끝납니다.

로컬 플레이어가 더 이상 파티에 없게 되면 언제나 `OnPartyLeft`가 아래 사유 중 하나와 함께 옵니다.

| Reason | 언제 |
|---|---|
| `Left` | 로컬 플레이어가 `Leave Easy Party`를 불렀습니다 |
| `Kicked` | 리더가 로컬 플레이어를 내보냈습니다. `Reason Text`는 리더의 사유입니다 |
| `LeaderLeft` | 리더가 나가서 파티가 끝났습니다 |
| `ConnectionLost` | 리더와의 연결이 끊겼거나, 매치가 끝난 뒤 리더가 돌아오지 않았습니다 |
| `MovedToGameSession` | 파티가 게임 세션에 들어가 닫혔습니다. 매치를 하는 모든 파티가 정상적으로 이렇게 끝나므로 오류로 띄우지 마세요 |

## 행선지는 리더가 정합니다

리더는 평소의 세션 노드를 그대로 쓰고, 파티가 따라옵니다.

| 리더가 부르는 노드 | 일어나는 일 |
|---|---|
| `Create Easy Session` | 호스트의 예약이 모든 멤버를 담습니다. Max Players가 파티 인원보다 작으면 `InvalidParams`로 실패합니다 |
| `Join Easy Session` | 리더가 호스트에게 파티 전체가 들어갈 자리를 요청합니다. 자리가 없으면 `JoinSessionFull`로 실패하고 아무도 이동하지 않습니다 |
| `Start Easy Matchmaking` | 파티 전체가 들어갈 자리가 있는 세션만 고릅니다 |

멤버는 따라오라는 알림을 받고 같은 세션에 스스로 참가합니다. 새 세션은 리더의 맵이 로드된 뒤에야 검색되므로, 멤버는 리더의 세션을 최대 30초 동안 찾습니다. 리더의 예약이 이미 멤버를 담고 있으므로 비밀번호도 필요 없습니다.

멤버가 이 세 노드 중 하나를 부르면 `InParty`로 거절되고, 로그에 이유가 남습니다. 멤버에게는 그 버튼들을 꺼 두세요.

```
Is Enabled = NOT (Is In Easy Party AND NOT Is Easy Party Leader)
```

게임 세션에 들어가면 파티가 닫힙니다. 모든 멤버와 리더에게 `MovedToGameSession`이 담긴 `OnPartyLeft`가 옵니다.

## 매치가 끝난 뒤

**Restore Party After Match**가 켜져 있으면(기본값), 매치가 끝난 뒤 게임 세션이 없는 첫 맵에서 파티가 돌아옵니다.

- 리더는 같은 설정으로 즉시 파티를 다시 만듭니다. 지난 파티의 멤버는 Invite Only 파티라도 다시 받습니다.
- 리더가 매치에 더 오래 남아 있을 수 있으므로, 멤버는 리더의 파티를 2초마다 최대 **Party Restore Wait Seconds**(기본 120초) 동안 찾습니다. 그 안에 리더가 돌아오지 않으면 멤버에게 `ConnectionLost`가 담긴 `OnPartyLeft`가 옵니다.

이 동안 `Is Easy Party Restoring`은 true이고, 멤버의 `Is In Easy Party`는 파티를 찾을 때까지 false입니다. 이때 대기 메시지를 띄우세요. `Create Easy Party`, `Join Easy Party`, `Leave Easy Party`, `Create Easy Session`, `Join Easy Session`, `Start Easy Matchmaking`을 부르면 복원이 멈춥니다. 플레이어가 다른 것을 골랐기 때문입니다.

## 매치 밖에서 맵 바꾸기

파티는 맵이 필요 없으므로 각 플레이어는 자기 맵에 그대로 있습니다. 리더가 다른 맵을 열면 파티는 그 맵에서 이어지고, 멤버는 자기 자리에 있으면서 **Party Reconnect Seconds**(기본 15초) 안에 리더에게 다시 연결합니다.

파티는 멤버를 다른 맵으로 옮기지 않습니다. 매치 전 로비 맵처럼 모두를 같은 맵으로 데려가려면, 리더가 `Create Easy Session`으로 게임 세션을 호스트하세요. 그러면 매치에 들어갈 때처럼 파티가 따라옵니다.

## 설정

Project Settings -> Plugins -> EasySession의 Party 항목입니다.

| 설정 | 기본값 | 효과 |
|---|---|---|
| Restore Party After Match | true | 위에서 설명한 대로 매치가 끝난 뒤 파티를 되돌립니다 |
| Party Restore Wait Seconds | 120 | 매치가 끝난 뒤 멤버가 리더의 파티를 찾는 시간 |
| Party Reconnect Seconds | 15 | 사유 없이 연결이 닫힌 뒤 멤버가 리더에게 계속 연결을 시도하는 시간. 예를 들어 리더의 다음 맵이 로드되는 동안입니다 |

## C++ API

같은 함수가 `UEasySessionSubsystem`에 있습니다. `CreateParty`, `FindParties`, `JoinParty`, `LeaveParty`, `KickPartyMember`, `SetPartyReady`, `IsInParty`, `IsPartyLeader`, `GetPartyMembers`, `GetPartySettings`, `GetPartyJoinCode`, `IsRestoringParty`, `SendPartyInviteToFriend`, `ShowPartyInviteUI`입니다.
