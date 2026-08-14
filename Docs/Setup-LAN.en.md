# Setup - LAN (NULL Subsystem)

*[한국어](Setup-LAN.ko.md)*

LAN play through the NULL subsystem is EasySession's default. It needs **no accounts, no keys, and usually no configuration at all**.

## Minimal configuration

If your project has never touched online settings, add this to `Config/DefaultEngine.ini`:

```ini
[OnlineSubsystem]
DefaultPlatformService=NULL
```

That is the entire setup. (Most projects work even without this line - NULL is the engine default - but being explicit avoids surprises when other plugins touch the config.)

## How discovery works

Sessions are advertised by UDP broadcast on the local network. Anything that blocks local UDP breaks discovery:

- **Windows Firewall** - allow the game and the editor when Windows asks.
- **VPNs / virtual adapters** - the LAN beacon can bind to an adapter that is not your LAN card. Disable the VPN, or name the bind address with the `-MultiHome=<your LAN IP>` launch argument.
- **Different subnets** - broadcast does not cross routers. Both machines must be on the same subnet.

## Testing on one machine

Two instances on the same PC can host and join each other:

- **PIE**: Number of Players = 2, Net Mode = Play Standalone, and turn *Run Under One Process* off. With it on, both instances share one process and one LAN beacon port, so discovery works only some of the time.
- **Packaged / `-game`**: launch two instances. `-game` runs the editor binary as a game, so this needs no packaging:

  ```
  <Engine>\Binaries\Win64\UnrealEditor.exe MyProject.uproject -game -log
  ```

  Add `-MultiHome=127.0.0.1` to keep traffic on loopback if you have exotic network adapters.

## Limitations of NULL

- LAN only - no internet play, no NAT traversal.
- No friends, invites, or presence.
- Player IDs are per-machine random IDs, not real accounts.

When you outgrow these, switch to [Steam](Setup-Steam.md) - your Blueprints stay the same.
