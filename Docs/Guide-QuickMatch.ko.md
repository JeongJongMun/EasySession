# 가이드 - Quick Match 매치메이킹

*[English](Guide-QuickMatch.en.md)*

`Quick Match Easy Session`은 노드 하나로 게임에 들어가는 길입니다. **검색 -> 가장 좋은 세션에 참가 -> 하나도 없으면 직접 호스트**.

## 파라미터 (`FEasyQuickMatchParams`)

| 필드 | 기본값 | 설명 |
|---|---|---|
| Search | (기본값) | Find Easy Sessions와 같은 필터 |
| Host | (기본값) | 참가할 곳이 없어 직접 호스트할 때 씁니다. Map Name은 아래 참고 |
| Allow Host Fallback | false | 기본값은 검색과 참가만 하고, 아무것도 없으면 `NoSessionsFound`로 실패합니다. 켜면 그때 직접 호스트가 됩니다 |
| Max Search Passes | 3 | 포기하거나 직접 호스트하기 전까지 검색을 몇 번 돌릴지. 3이면 검색을 세 번 합니다 |
| Delay Between Passes | 2.0초 | 다음 검색까지 이만큼 쉽니다 |

### Host > Map Name을 채울지 말지

Quick Match는 `Create Easy Session`이 받는 호스트 파라미터를 그대로 받습니다. Map Name을
비워두면 호스트 폴백이 지금 있는 맵에서 리슨 서버를 엽니다. 거부되지 않습니다.

**그래도 대개는 채우는 게 맞습니다.** 메뉴 위젯에서 Quick Match를 부르는 것이 보통인데, 그때
Map Name이 비어 있으면 메뉴 맵이 경기장이 됩니다. 참가할 방을 찾던 플레이어가 자기 메뉴에서
남을 맞이하게 됩니다.

`Allow Host Fallback`은 기본값이 꺼짐이라, 켜지 않으면 Map Name은 볼 일이 없습니다.
플러그인의 예제도 꺼둔 채로 검색과 참가만 합니다.

진행 상황은 `Get Easy Session Subsystem` -> `Get Active Quick Match Policy`로 정책을 얻어 그 `OnStateChanged`를 바인딩하면 보여줄 수 있습니다.

상태는 `Searching`, `Joining`, `Hosting`, `Complete` 넷입니다. 한 줄로 흘러가지는 않습니다. 후보를 찾으면 `Joining`으로 갔다가, 전부 거절당하면 `Searching`으로 돌아와 다음 검색을 돌립니다. `Hosting`은 검색을 다 쓰고 직접 방을 만들 때만 나옵니다.

언제든 `Cancel Easy Quick Match`로 멈출 수 있고, 그 판은 `Canceled` 결과로 끝납니다.

`OnSuccess` 뒤에는 `Is Easy Session Host`로 남의 방에 들어간 건지 자기가 호스트가 된 건지 알 수 있습니다.

## "가장 좋은 세션"을 고르는 기준

기본 정책은 먼저 검색 결과에서 후보를 추리고, 그 후보들에 점수를 매겨 높은 순으로 참가를 시도합니다.

**후보에서 빠지는 세션**

- **비밀번호가 걸린 세션** - Quick Match는 비밀번호를 넘길 수 없어서 아예 건너뜁니다.
- **이미 거절당한 세션** - 참가를 거절한 곳은 그 판이 끝날 때까지 다시 시도하지 않습니다.

**남은 후보의 순서**

1. **핑 구간** - 핑을 구간으로 묶습니다(50ms 이하, 100ms 이하, 150ms 이하, 그보다 나쁨). 낮은 구간이 언제나 우선시됩니다.
2. **채워진 비율** - 같은 구간 안에서는 더 찬 세션이 우선시됩니다. 매치가 더 빨리 시작되고, 플레이어가 반쯤 빈 방들로 흩어지지 않습니다.
3. **꽉 찬 세션은 맨 뒤로** - 자리가 없는 세션은 큰 감점을 받아 항상 마지막으로 밀립니다. 버리지는 않습니다. 인원 수는 검색 시점의 값이라 그 사이에 누가 나갔을 수 있어서, 직접 방을 새로 만들기 전에 한 번은 두드려 볼 값어치가 있습니다.
4. **상위 후보 섞기** - 순서를 정한 뒤 가장 좋은 3개를 섞습니다. 같은 순간에 검색한 플레이어들이 전부 한 방으로 몰려가 `JoinSessionFull`로 튕기지 않도록 하기 위해서입니다.

## 점수 계산 바꾸기 - 함수 하나만 오버라이드

점수 계산은 `UEasyQuickMatchPolicy::ScoreSession`에 있고 **BlueprintNativeEvent**입니다. 정책을 상속받아(블루프린트든 C++든) 이 함수 하나만 오버라이드한 뒤, 그 클래스를 Quick Match의 `Policy Class` 핀에 넘기세요.

```
ScoreSession(Session) -> float   // 높을수록 먼저 참가
```

예시 - 기본 동작은 그대로 두고 원하는 모드를 돌리는 세션을 더 좋아하게 만들기.

```
Override ScoreSession:
    base = Parent: ScoreSession(Session)
    if Session.CustomSettings["GameMode"] == "CTF": return base + 50
    return base
```

핑 구간 경계(`Ping Buckets Ms`)와 섞을 개수(`Top Candidate Randomization`)도 상속받은 정책 클래스에서 기본값으로 고칠 수 있습니다.

## 정책을 새로 짤 때와 흐름을 직접 짤 때

- *어느* 세션이 이길지만 바꾸고 싶다면 `ScoreSession`을 오버라이드하면 끝입니다.
- *흐름* 자체가 다르다면(파티 인원을 고려한 재시도, 지역 폴백 같은 것) `Find` / `Join` / `Create` 노드를 직접 이어 붙여도 됩니다. Quick Match는 편의 기능이지 가둬 두는 틀이 아닙니다.
