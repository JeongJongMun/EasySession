# Changelog

*[한국어](CHANGELOG.ko.md)*

## 1.1.0 - Unreleased

### Changed

- **Is Easy Session Busy now means that the session or the map the local player is in is changing.** It is true while Create Easy Session, Create Easy Party, Join Easy Session, Leave Easy Session, Start Easy Matchmaking or Restore Easy Party runs or waits, including the joins and leaves the plugin starts on its own, and during the map load that follows hosting, joining or leaving.
- Find Easy Sessions, Update Easy Session, Start Easy Session and End Easy Session no longer make Is Easy Session Busy true. A menu whose buttons are bound to Is Easy Session Busy no longer disables and re-enables them every time it calls Update Easy Session.
- If a widget disabled its own buttons with Is Easy Session Busy while its Find Easy Sessions or Update Easy Session call ran, disable them until that node's On Success or On Failure pin fires instead.
- Get Easy Session Activity still reports those four calls, so it can be other than None while Is Easy Session Busy is false.
- The example widgets disable their own buttons this way:
  - `WBP_SessionBrowser` disables its search buttons and its result list until Find Easy Sessions fires On Success or On Failure.
  - `WBP_UpdateSessionPopup` disables `ApplyButton` until Update Easy Session fires On Success or On Failure.
  - `WBP_LobbyScreen` disables `StartMatchButton` when it is clicked, and enables it again when Start Easy Session fires On Failure.
  - `WBP_EscPopup` disables `ReturnToLobbyButton` when it is clicked, and refreshes its buttons every time it opens.
- `WBP_SessionStatus` updates the status line from On Activity Changed instead of On Busy Changed.

### Added

- **On Activity Changed**, an event on the EasySession subsystem (Get Easy Session Subsystem) and on widgets whose parent class is EasySessionWidget. It fires when Get Easy Session Activity changes, so a status text can update without reading Get Easy Session Activity every tick.

### Fixed

- `WBP_PartyCard` now reads Is Easy Session Busy in Event Construct. Before, a card created while Is Easy Session Busy was true kept its buttons enabled until On Busy Changed fired.
- The Steam setup guide no longer says that joining another project's session fails with `OutdatedClient`. No value of EEasySessionResult or EEasyDisconnectReason has that name. The guide now says that such a join does not work, because the game versions differ.

## 1.0.0

- First release on Fab.
