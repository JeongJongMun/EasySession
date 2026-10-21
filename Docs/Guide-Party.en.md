# Guide - Party

*[한국어](Guide-Party.ko.md)*

A party is a small group of players that stays together outside game sessions, for example friends gathering in the main menu before they play. The host decides where the party goes: when the host creates, joins or matchmakes into a game session, every member follows.

A party is optional. A game that never creates one works exactly as before, and every Session node keeps answering about the game session by default. A party is created with `Create Easy Party`, and found, joined, left and queried with the Session nodes, whose `Type` input takes Party Session. The nodes only a party has, such as members and ready, are under **EasySession | Party**.

| | Game session | Party |
|---|---|---|
| What it is | The match players find, join and play in | A group that moves between matches together |
| Map | Has one. The host is the server | None. Nothing travels when a party is created or joined |
| Lives | Until the host leaves or destroys it | Outside game sessions. Entering one closes the party, and it comes back after the match |
| How many | One per player | One per player, next to the game session |

## Create a party

`Create Easy Party` with `FEasySessionSettings`, the struct a game session advertises as well:

| Field | Default | Notes |
|---|---|---|
| Session Display Name | (empty) | Shown in party search results. Empty names the party after its host |
| Max Players | 4 | How many players the party holds, the host included |
| Hidden | false | Left out of the party search list, see [who may join](#who-may-join) below |
| Use Join Code | false | Advertise a code that finds the party, as long as **Join Code Length** says (6 by default) |
| Allow Invites | true | Whether members can invite friends to the party |
| Custom Settings | (empty) | Key-value data advertised with the party, read back with `Get Easy Session Settings` |
| Password | (empty) | A party has no password, so a non-empty one fails the create with `InvalidParams`. Hide the party or share its join code instead |
| Should Advertise | true | Every party is advertised, so members find it again after a match. Off fails the create with `InvalidParams`. Use Hidden instead |

Allow Join In Progress is advertised as given and changes nothing for a party.

The local player becomes the host. Creating needs a player logged in to the online subsystem, because members are told apart by their ids. NULL logs in on its own, and Steam logs in the Steam user.

The create fails with `SessionAlreadyExists` while the player is already in a party, and while they are in a game session, because a party exists only outside game sessions.

On Steam a party is a Steam lobby, so it needs presence and works across the internet like a game session. On NULL it is a LAN session.

## Who may join

`Hidden` and `Use Join Code` decide how players find the party, the same two flags a game session has. Neither refuses a player who reaches the party: the host refuses only a kicked player, and anyone while the party is full.

| Hidden | Use Join Code | Party search | Players join through |
|---|---|---|---|
| false (default) | false (default) | Listed | A search, or an invite |
| true | false | Not listed | An invite |
| true | true | Not listed, found with the code | The code, or an invite |
| false | true | Listed, and found with the code | A search, the code, or an invite |

`Hidden` does not lock the party: a player who finds it another way, for example by the host's id, is admitted. A party has no password.

A party with `Use Join Code` advertises a generated code of **Join Code Length** characters (6 by default). `Get Easy Join Code` with `Type` set to Party Session reads it, for the host to show and share. Every member can read it too. In the example, a player enters the code in the Find screen's CODE tab while the browser shows parties.

`Get Easy Session Settings` with `Type` set to Party Session returns the settings the party was created with, on the host and on every member. Use it to show the party as "2/4", or to label a hidden party.

## Find and join a party

`Find Easy Sessions` with `Type` set to Party Session takes the same `FEasySessionSearchParams` as a game session search and returns only parties:

- With an empty `Join Code`, it lists the parties that are not hidden.
- With a `Join Code`, it returns the one party that advertises that code, hidden or not.
- Each result's `Host Name` is the host's name, and `Session Display Name` is the party's name: the host's name unless the host set one. `Max Players` and `Open Slots` count members.
- `Required Custom Settings` and `Include In Progress Sessions` are ignored, because a party has no match state and its custom settings are not for searching. The search params a game uses for game sessions work here too.

`Join Easy Session` takes one of those results, and joins the party because the result says it is one. Leave `Password` and `Additional Travel Options` empty, or the join fails with `InvalidParams`. The host decides the join, and a refusal fails the node with `JoinRefused` and the host's reason, for example "The party is full." or "The party host removed you from this party.". On success `Get Easy Party Members` already lists the local player.

The join fails with `SessionAlreadyExists` for a player who is already in a party or in a game session. Call `Leave Easy Session` for it first.

## Join a public party automatically

A "play with anyone" button needs no extra node. Chain the two nodes above:

```
[Find Easy Sessions]  (Type Party Session, empty Join Code: listed parties only)
  OnSuccess (Results) -> first result with Open Slots > 0
    found     -> [Join Easy Session]
                   OnFailure -> try the next result, the party may have filled up meanwhile
    not found -> [Create Easy Party]  (Hidden off, so the next player finds it)
```

A party search does not filter on custom settings, so every listed party is a candidate. Matchmaking for game sessions is a separate node, `Start Easy Matchmaking`, which the host can run once the party is together.

## Invites

Every member invites, not only the host:

- `Send Easy Invite To Friend` with `Type` set to Party Session invites one friend that `Read Easy Friends` returned.
- `Show Easy Invite UI` with `Type` set to Party Session opens the platform invite overlay for the party.

Both return `NoSessionExists` outside a party and `NotSupportedByService` on NULL/LAN, which has no invites.

When a player accepts a party invite, `OnSessionInviteAccepted` fires with a result whose `Session Type` is Party Session. With **Auto Join Accepted Invites** on, the default, the plugin joins the party right after the event:

- A running Matchmaking is canceled first. One that is already `Joining` or `Hosting` is not canceled, and the invite is not joined.
- A player who is already in a party leaves it first.
- A player in a game session does not join, because one click in the overlay must not end their match. The log says so, and the game can call `Leave Easy Session` and then `Join Easy Session` with the event's result.

A failed join arrives on `OnSessionFailure`, because no node waits for it. With Auto Join Accepted Invites off, only the event fires: call `Join Easy Session` with the result yourself, which joins the party or the game session as `Session Type` says.

## Members, ready and kick

`Get Easy Party Members` returns every member, the host included, as `FEasyPartyMemberInfo`: name, whether it is the local player, whether it hosts the party, whether it is ready, and the player id. `OnPartyMembersChanged` fires on the host and on every member when someone joins, leaves or changes whether they are ready. The list is already updated when it fires, so a party panel reads it and redraws. `Show Easy Profile UI For Party Member` opens a member's platform profile, for example when the player clicks a member row.

`Set Easy Party Ready` changes whether the local player is ready. The plugin only shares the value. The game decides what being ready allows, for example enabling the host's Play button once everyone is ready.

`Kick Easy Party Member` removes a member and keeps them out of this party. The member receives `OnPartyLeft` with `Kicked` and the host's reason. Host only: a member gets `RequiresHost`.

## Leaving a party

`Leave Easy Session` with `Type` set to Party Session leaves the party. Nothing travels. A host who leaves ends the party for every member, because the host holds it.

`OnPartyLeft` fires on the local player whenever they are no longer in the party, with one of these reasons:

| Reason | When |
|---|---|
| `Left` | The local player left the party with `Leave Easy Session` |
| `Kicked` | The host removed the local player. `Reason Text` is the host's reason |
| `HostLeft` | The host left, which ends the party |
| `ConnectionLost` | The connection to the host was lost, or the host did not come back after a match |
| `MovedToGameSession` | The party entered a game session, which closes it. This is the normal end of every party that plays a match, so do not show it as an error |

## The host decides where the party goes

The host uses the normal session nodes, and the party comes along:

| Host calls | What happens |
|---|---|
| `Create Easy Session` | The host's reservation holds every member. Max Players must fit the party, or the create fails with `InvalidParams` |
| `Join Easy Session` | The party host asks the game host for room for the whole party. A session without room fails with `JoinSessionFull`, and nobody moves |
| `Start Easy Matchmaking` | The search only considers sessions with room for the whole party |

The members are told to follow and join the same session by themselves. Each member leaves the party as soon as it is told, which receives `OnPartyLeft` with `MovedToGameSession` and tells the host the notice arrived. Each member keeps searching for the host's session for up to 30 seconds, because a new session appears only once the host's map has loaded. A member needs no password: the host's reservation already holds them.

A member who calls one of those three nodes is refused with `InParty`, and the log says why. Disable those buttons for members:

```
Is Enabled = NOT (Is In Easy Session (Party Session) AND NOT Is Easy Host (Party Session))
```

Entering the game session closes the party. Every member, and the host, receives `OnPartyLeft` with `MovedToGameSession`.

## After the match

With **Restore Party After Match** on, the default, the party comes back in the first map without a game session after the match:

- The host creates the party again at once, with the same settings. The members find it by the host's id, so a hidden party comes back too.
- Every member looks for the host's party every two seconds, for up to **Party Restore Wait Seconds** (120 by default), because the host may stay in the match longer. A member whose host does not come back in time receives `OnPartyLeft` with `ConnectionLost`.

While this runs, `Is Easy Party Restoring` is true, and `Is In Easy Session` with Party Session stays false on a member until the party is found. Show a waiting message then. Calling `Create Easy Party`, `Create Easy Session`, `Join Easy Session`, `Leave Easy Session` or `Start Easy Matchmaking` stops the restore, because the player chose something else.

## Changing maps outside a match

A party needs no map, so each player stays in their own map. When the host opens another map, the party continues there, and every member stays where they are and reconnects to the host within **Party Reconnect Seconds** (15 by default).

A party does not move members between maps. To bring everyone to the same map, such as a lobby map before the match, the party host creates a game session with `Create Easy Session`, and the party follows like it does for a match.

## Settings

In Project Settings -> Plugins -> EasySession, under Party:

| Setting | Default | Effect |
|---|---|---|
| Restore Party After Match | true | Gets the party back after a match, as described above |
| Party Restore Wait Seconds | 120 | How long a member looks for the host's party after a match |
| Party Reconnect Seconds | 15 | How long a member keeps connecting to the host after the connection closed without a reason, for example while the host's next map loads |

## C++ API

The same functions are on `UEasySessionSubsystem`: `CreateParty`, `KickPartyMember`, `SetPartyReady`, `GetPartyMembers`, `IsRestoringParty` and `ShowProfileUIForPartyMember`.
`FindSessions`, `LeaveSession`, `IsInSession`, `IsHost`, `GetSessionSettings`, `GetJoinCode`, `GetDisplayName`, `GetPlayerCount`, `GetMaxPlayers`, `SendInviteToFriend` and `ShowInviteUI` take `EEasySessionType::PartySession` for the party, and `JoinSession` reads the type off the search result.
