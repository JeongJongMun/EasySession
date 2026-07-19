# Setup — Epic Online Services (EOS)

EOS is Epic's free cross-platform online service: internet sessions, cross-play, no royalty.

> Status note: EasySession's EOS validation is in progress (Phase 4 of the roadmap). The configuration below is the standard OnlineSubsystemEOS setup; report issues you hit with it.

## Prerequisites

Create a (free) product in the [Epic Dev Portal](https://dev.epicgames.com/portal):

1. Create an **Organization** and a **Product**.
2. Note these values from the product page: **Product Id**, **Sandbox Id**, **Deployment Id**, **Client Id**, **Client Secret** (create a Client with the *Peer2Peer* or *GameClient* policy).

## 1. Enable the plugins

Enable **Online Subsystem EOS** (this also enables *EOS Shared* and *Online Services EOS* dependencies). Restart the editor.

## 2. DefaultEngine.ini

```ini
[OnlineSubsystem]
DefaultPlatformService=EOS

[OnlineSubsystemEOS]
bEnabled=true

[/Script/OnlineSubsystemEOS.EOSSettings]
CacheDir=CacheDir
DefaultArtifactName=MyGame
Artifacts=(ArtifactName="MyGame",ClientId="xyz...",ClientSecret="abc...",ProductId="...",SandboxId="...",DeploymentId="...",EncryptionKey="1111111111111111111111111111111111111111111111111111111111111111")

[/Script/SocketSubsystemEOS.NetDriverEOSBase]
bIsUsingP2PSockets=true
```

You can also fill these in the editor UI: **Project Settings → Plugins → Online Subsystem EOS**.

## 3. Login is required

Unlike Steam, EOS needs an explicit user login before sessions work. For development, the **Developer Auth Tool** (ships with the EOS SDK) plus these launch arguments is the quickest path:

```
-AUTH_LOGIN=localhost:8081 -AUTH_PASSWORD=YourCredentialName -AUTH_TYPE=developer
```

For release you would use Epic account login or Device ID auth. (An EasySession login helper is planned; for now use the `Login` node from the engine's Online utilities or C++ `IOnlineIdentity::Login`.)

## 4. Test checklist

- Verify the log line: `EasySessionSubsystem initialized. Online subsystem: EOS`
- Test with two machines (or two windows with two different Dev Auth credentials).
- EOS sessions use lobbies/presence by default in EasySession's host params — leave `Use Presence` on.

## Common pitfalls

| Symptom | Cause |
|---|---|
| `LogEOSSDK` errors at startup | Wrong Client Id/Secret or Deployment Id |
| Sessions not found | One of the instances is not logged in (check `IOnlineIdentity` status) |
| Works on LAN but not on EOS | `DefaultPlatformService` still `NULL` in the packaged config |
