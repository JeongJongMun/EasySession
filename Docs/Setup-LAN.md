# Setup — LAN (NULL Subsystem)

LAN play through the NULL subsystem is EasySession's default. It needs **no accounts, no keys, and usually no configuration at all**.

## Minimal configuration

If your project has never touched online settings, add this to `Config/DefaultEngine.ini`:

```ini
[OnlineSubsystem]
DefaultPlatformService=NULL
```

That is the entire setup. (Most projects work even without this line — NULL is the engine default — but being explicit avoids surprises when other plugins touch the config.)

## How discovery works

Sessions are advertised by UDP broadcast on the local network. Anything that blocks local UDP breaks discovery:

- **Windows Firewall** — allow your game/editor when Windows asks, or add an inbound rule for UDP.
- **VPNs / virtual adapters** — broadcasts may go out on the wrong interface. Disable the VPN or force the bind address with the `-MultiHome=<your LAN IP>` launch argument.
- **Different subnets** — broadcast does not cross routers. Both machines must be on the same subnet.

## Testing on one machine

Two instances on the same PC can host and join each other:

- **PIE**: Number of Players = 2, Net Mode = Play Standalone. If instances cannot see each other, disable *Run Under One Process*.
- **Packaged / -game**: launch two instances; add `-MultiHome=127.0.0.1` to keep traffic on loopback if you have exotic network adapters.

## Limitations of NULL

- LAN only — no internet play, no NAT traversal.
- No friends, invites, or presence.
- Player IDs are per-machine random IDs, not real accounts.

When you outgrow these, switch to [Steam](Setup-Steam.md) — your Blueprints stay the same.
