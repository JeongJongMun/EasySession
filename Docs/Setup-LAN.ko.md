# 설정 - LAN (NULL 서브시스템)

*[English](Setup-LAN.en.md)*

NULL 서브시스템을 통한 LAN 플레이가 EasySession의 기본값입니다. **계정도, 키도, 대개는 설정조차 필요 없습니다.**

## 최소 설정

프로젝트에서 온라인 설정을 한 번도 건드린 적이 없다면, `Config/DefaultEngine.ini`에 이걸 추가하세요.

```ini
[OnlineSubsystem]
DefaultPlatformService=NULL
```

이게 설정의 전부입니다. (NULL이 엔진 기본값이라 이 줄 없이도 대부분 동작하지만, 다른 플러그인이 설정을 건드릴 때 예상 밖의 일이 생기지 않도록 명시해 두는 편이 좋습니다.)

## 검색이 동작하는 방식

세션은 로컬 네트워크에 UDP 브로드캐스트로 광고됩니다. 로컬 UDP를 막는 것은 무엇이든 검색을 깨뜨립니다.

- **Windows 방화벽** - 윈도우가 물어볼 때 게임과 에디터를 허용하세요.
- **VPN / 가상 어댑터** - LAN 비콘이 LAN 카드가 아닌 어댑터에 바인드될 수 있습니다. VPN을 끄거나, 실행 인자 `-MultiHome=<자신의 LAN IP>`로 바인드 주소를 지정하세요.
- **서로 다른 서브넷** - 브로드캐스트는 라우터를 넘지 못합니다. 두 PC가 같은 서브넷에 있어야 합니다.

## 한 대에서 테스트하기

같은 PC의 두 인스턴스가 서로 호스팅하고 참가할 수 있습니다.

- **PIE**: Number of Players = 2, Net Mode = Play Standalone으로 두고 *Run Under One Process*를 끄세요. 켜면 두 인스턴스가 한 프로세스와 하나의 LAN 비콘 포트를 공유해서, 검색이 될 때도 있고 안 될 때도 있습니다.
- **패키지 / `-game`**: 인스턴스를 두 개 실행하세요. `-game`은 에디터 실행 파일을 게임으로 띄우므로 패키징이 필요 없습니다.

  ```
  <엔진>\Binaries\Win64\UnrealEditor.exe MyProject.uproject -game -log
  ```

  네트워크 어댑터 구성이 특이하다면 `-MultiHome=127.0.0.1`을 붙여 트래픽을 루프백에 묶어 두세요.

## NULL의 한계

- LAN 전용입니다. 인터넷 플레이도, NAT 통과도 없습니다.
- 친구, 초대, 프레즌스가 없습니다.
- 플레이어 ID는 실제 계정이 아니라 PC마다 생성되는 임의의 ID입니다.

여기서 더 필요해지면 [Steam](Setup-Steam.md)으로 넘어가세요. 블루프린트는 그대로 둬도 됩니다.
