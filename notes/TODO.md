# TODO — deferred items

## Controller support (CoD4x_Client_pub)

- [ ] **Tighten cvar ranges.** The Stage A upper bounds are loose:
  - `cl_gamepad_sens_look`  — currently 0.0–20.0
  - `cl_gamepad_sens_ads`   — currently 0.0–20.0
  - `cl_gamepad_deadzone_left`  — currently 0.0–1.0
  - `cl_gamepad_deadzone_right` — currently 0.0–1.0
  Loose maxima let a user type a value that effectively disables the
  controller by accident (e.g. a deadzone near 1.0, or absurd
  sensitivity). Once real XInput behaviour is tuned, narrow these to
  sane ranges. Not a blocker for Stage A.

- [ ] **XInput guard.** When XInput polling logic is added to
  `IN_GamepadsMove()`, the very first line must be:
  `if (!cl_gamepad->boolean) return;`
  so the feature is fully inert when the user disables `cl_gamepad`.
  (Done in Commit 1.)

- [ ] **Developer-gated button logging.** Commit 2 replaced the Commit 1
  per-button `Com_Printf` with `Com_QueueEvent`. Consider re-adding
  button press/release logging behind a developer cvar (e.g. only when
  `developer` is set) so controller input issues can be diagnosed
  without a rebuild. Note: the `name` field was dropped from the
  `gamepad_buttons[]` table in Commit 2 (its string literals would have
  lingered in the binary); this work will need to re-add a name field
  or a keycode->name lookup.
