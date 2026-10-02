# Guide - Party

*[한국어](Guide-Party.ko.md)*

A party is a small group of players that stays together outside game sessions, for example friends gathering in the main menu before they play. The leader decides where the party goes: when the leader creates, joins or matchmakes into a game session, every member follows.

A party is optional. A game that never creates one works exactly as before, and every Session node keeps answering about the game session only. The party has its own nodes, all under **EasySession | Party**.

| | Game session | Party |
|---|---|---|
| What it is | The match players find, join and play in | A group that moves between matches together |
| Map | Has one. The host is the server | None. Nothing travels when a party is created or joined |
| Lives | Until the host leaves or destroys it | Outside game sessions. Entering one closes the party, and it comes back after the match |
| How many | One per player | One per player, next to the game session |

## Create a party

`Create Easy Party` with `FEasyPartySettings`:

| Field | Default | Notes |
|---|---|---|
| Max Members | 4 | How many players the party holds, the leader included. At least 2 |
| Privacy | Invite Only | Who may join, see [who may join](#who-may-join) below |

The local player becomes the leader. Creating needs a player logged in to the online subsystem, because members are told apart by their ids. NULL logs in on its own, and Steam logs in the Steam user.

The create fails with `SessionAlreadyExists` while the player is already in a party, and while they are in a game session, because a party lives outside game sessions.

On Steam a party is a Steam lobby, so it needs presence and works across the internet like a game session. On NULL it is a LAN session.

## Who may join

`Privacy` decides two things together: whether searches list the party, and whom the leader admits.

| Privacy | Find Easy Parties | The leader admits |
|---|---|---|
| Invite Only | Hidden | Players the leader invited, the leader's friends, and the members of the party before a match |
| Join Code | Hidden, found only with the code | Anyone who has the code |
| Public | Listed | Anyone |

Invite Only is not only hidden. The leader checks every player as they connect, so a player who found the party some other way is still refused. This is why `Privacy` is one value instead of the `Hidden` and `Use Join Code` flags a game session has: a hidden game session lets in anyone who has its search result, and only a password keeps players out.

The platform invite overlay does not tell the game whom it invited, so an Invite Only party admits every friend of the leader. A friend invited with `Send Easy Party Invite To Friend` is admitted the same way.

A Join Code party advertises a generated six character code. `Get Easy Party Join Code` reads it, for the leader to show and share. Every member can read it too.

`Get Easy Party Settings` returns the settings the party was created with, on the leader and on every member. Use it to show the party as "2/4" or to label it "Invite Only".

## Find and join a party

`Find Easy Parties` takes the same `FEasySessionSearchParams` as `Find Easy Sessions` and returns only parties:

- With an empty `Join Code`, it lists the Public parties.
- With a `Join Code`, it returns the one party that advertises that code.
- Each result's `Session Display Name` and `Host Name` are the leader's name. `Max Players` and `Open Slots` count members.
- Parties advertise no region, custom settings or match state, so `Region`, `Required Custom Settings` and `Include In Progress Sessions` are ignored. The search params a game uses for game sessions work here too.

`Join Easy Party` takes one of those results. The leader decides the join, and a refusal fails the node with `JoinRefused` and the leader's reason, for example "The party is full." or "This party only admits players the leader invited.". On success `Get Easy Party Members` already lists the local player.

The join fails with `SessionAlreadyExists` for a player who is already in a party or in a game session. Call `Leave Easy Party` or `Leave Easy Session` first.

## Invites

Only the leader invites:

- `Send Easy Party Invite To Friend` invites one friend that `Read Easy Friends` returned.
- `Show Easy Party Invite UI` opens the platform invite overlay for the party.

Both return `RequiresPartyLeader` on a member and `NotSupportedByService` on NULL/LAN, which has no invites.

When a player accepts a party invite, `OnSessionInviteAccepted` fires with a result whose `Is Party` is true. With **Auto Join Accepted Invites** on, the default, the plugin joins the party right after the event:

- A running Matchmaking is canceled first.
- A player who is already in a party leaves it first.
- A player in a game session does not join, because one click in the overlay must not end their match. The log says so, and the game can call `Leave Easy Session` and then `Join Easy Party` with the event's result.

A failed join arrives on `OnSessionFailure`, because no node waits for it. With Auto Join Accepted Invites off, only the event fires: check `Is Party` and call `Join Easy Party` or `Join Easy Session` yourself.

## Members, ready and kick

`Get Easy Party Members` returns every member, the leader included, as `FEasyPartyMemberInfo`: name, whether it is the local player, whether it leads the party, whether it is ready, and the player id. `OnPartyMembersChanged` fires on the leader and on every member when someone joins, leaves or changes whether they are ready. The list is already updated when it fires, so a party panel reads it and redraws. `Show Easy Profile UI For Party Member` opens a member's platform profile, for example when the player clicks a member row.

`Set Easy Party Ready` changes whether the local player is ready. The plugin only shares the value. The game decides what being ready allows, for example enabling the leader's Play button once everyone is ready.

`Kick Easy Party Member` removes a member and keeps them out of this party. The member receives `OnPartyLeft` with `Kicked` and the leader's reason. Leader only: a member gets `RequiresPartyLeader`.

## Leaving a party

`Leave Easy Party` leaves the party. A leader who leaves ends the party for every member, because the leader holds it.

`OnPartyLeft` fires on the local player whenever they are no longer in the party, with one of these reasons:

| Reason | When |
|---|---|
| `Left` | The local player called `Leave Easy Party` |
| `Kicked` | The leader removed the local player. `Reason Text` is the leader's reason |
| `LeaderLeft` | The leader left, which ends the party |
| `ConnectionLost` | The connection to the leader was lost, or the leader did not come back after a match |
| `MovedToGameSession` | The party entered a game session, which closes it. This is the normal end of every party that plays a match, so do not show it as an error |

## The leader decides where the party goes

The leader uses the normal session nodes, and the party comes along:

| Leader calls | What happens |
|---|---|
| `Create Easy Session` | The host's reservation holds every member. Max Players must fit the party, or the create fails with `InvalidParams` |
| `Join Easy Session` | The leader asks the host for room for the whole party. A session without room fails with `JoinSessionFull`, and nobody moves |
| `Start Easy Matchmaking` | The search only considers sessions with room for the whole party |

The members are told to follow and join the same session by themselves. Each member keeps searching for the leader's session for up to 30 seconds, because a new session appears only once the leader's map has loaded. A member needs no password: the leader's reservation already holds them.

A member who calls one of those three nodes is refused with `InParty`, and the log says why. Disable those buttons for members:

```
Is Enabled = NOT (Is In Easy Party AND NOT Is Easy Party Leader)
```

Entering the game session closes the party. Every member, and the leader, receives `OnPartyLeft` with `MovedToGameSession`.

## After the match

With **Restore Party After Match** on, the default, the party comes back in the first map without a game session after the match:

- The leader creates the party again at once, with the same settings. The members of the last party are admitted again, even into an Invite Only party.
- Every member looks for the leader's party every two seconds, for up to **Party Restore Wait Seconds** (120 by default), because the leader may stay in the match longer. A member whose leader does not come back in time receives `OnPartyLeft` with `ConnectionLost`.

While this runs, `Is Easy Party Restoring` is true, and `Is In Easy Party` stays false on a member until the party is found. Show a waiting message then. Calling `Create Easy Party`, `Join Easy Party`, `Leave Easy Party`, `Create Easy Session`, `Join Easy Session` or `Start Easy Matchmaking` stops the restore, because the player chose something else.

## Changing maps outside a match

A party needs no map, so each player stays in their own map. When the leader opens another map, the party continues there, and every member stays where they are and reconnects to the leader within **Party Reconnect Seconds** (15 by default).

A party does not move members between maps. To bring everyone to the same map, such as a lobby map before the match, the leader hosts a game session with `Create Easy Session`, and the party follows like it does for a match.

## Settings

In Project Settings -> Plugins -> EasySession, under Party:

| Setting | Default | Effect |
|---|---|---|
| Restore Party After Match | true | Gets the party back after a match, as described above |
| Party Restore Wait Seconds | 120 | How long a member looks for the leader's party after a match |
| Party Reconnect Seconds | 15 | How long a member keeps connecting to the leader after the connection closed without a reason, for example while the leader's next map loads |

## C++ API

The same functions are on `UEasySessionSubsystem`: `CreateParty`, `FindParties`, `JoinParty`, `LeaveParty`, `KickPartyMember`, `SetPartyReady`, `IsInParty`, `IsPartyLeader`, `GetPartyMembers`, `GetPartySettings`, `GetPartyJoinCode`, `IsRestoringParty`, `SendPartyInviteToFriend`, `ShowPartyInviteUI` and `ShowProfileUIForPartyMember`.
