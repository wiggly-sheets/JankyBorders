# JankyBorders

<img align="right" width="50%" src="images/screenshot.png" alt="Screenshot">

*JankyBorders* is a lightweight tool designed to add colored borders to
user windows on macOS 14.0+. It enhances the user experience by visually
highlighting the currently focused window without relying on the accessibility
API, thereby being faster than comparable tools.

## Usage
### Install
The binary can be made available by installing it through Homebrew:
```bash
brew tap FelixKratz/formulae
brew install borders
```

For a comprehensive overview of all available options and commands, consult the
man page: `man borders`. A rendered version of the man page is available in the
[Wiki](https://github.com/FelixKratz/JankyBorders/wiki/Man-Page).

### Bootstrap with yabai
For example, if you are using `yabai`, you could add:
```bash
borders active_color=0xffe1e3e4 inactive_color=0xff494d64 width=5.0 &
```
to the very end of your `yabairc`. This will start the borders with the
specified options along with yabai.

### Bootstrap with AeroSpace
You could add:
```toml
after-startup-command = [
  'exec-and-forget borders active_color=0xffe1e3e4 inactive_color=0xff494d64 width=5.0'
]
```
to you `aerospace.toml`. This will start borders with the specified options
along with AeroSpace.

### Bootstrap with brew
If you want to run this as a separate service, you could use:
```bash
brew services start borders
```

### Configuring the appearance
You can either configure the appearance directly when starting the borders
process (as shown in "Bootstrap with yabai") or use a configuration file.
The appearance can be adapted at any point in time.

#### Animating focus changes
Focus animations are disabled by default. Enable one or more modes with a
comma-separated `animation=` value:

```bash
borders animation=fade,slide animation_duration=0.35 animation_easing=ease_out_expo
```

The available modes are:

- `fade` fades in the newly focused border.
- `ramp` grows its glow from zero to full and therefore requires an active
  color configured with `glow(...)`.
- `slide` moves the active border from the previously focused window to the
  newly focused window.
- `pulse` temporarily expands the active border. The expansion is strongest
  for thin borders and progressively gentler as the configured width grows.
- `none` disables focus animations and should be used by itself.

Comma-separated modes run simultaneously and share `animation_duration`,
which defaults to `0.25` seconds. The old border immediately switches to its
inactive color; only the newly focused border animates. Focus changes while
the primary mouse button is held are also applied without animation.

Slide supports `animation_easing=linear`, `ease_in_expo`, `ease_out_expo`, or
`ease_in_out_expo`. Easing affects only slide; the other modes keep their
linear timing. For example, a glow ramp combined with pulse can be configured
as:

```bash
borders 'active_color=glow(0xffe2e2e3)' animation=ramp,pulse
```

#### Using a configuration file (Optional)
If the primary `borders` process is started without any arguments (or launched
as a service by brew), it will search for a file at
`~/.config/borders/bordersrc` and execute it on launch if found.

An example configuration file could look like this:
`~/.config/borders/bordersrc`
```bash
#!/bin/bash

options=(
	style=round
	width=6.0
	hidpi=off
	active_color=0xffe2e2e3
	inactive_color=0xff414550
	animation=fade,slide
	animation_duration=0.25
	animation_easing=ease_out_expo
)

borders "${options[@]}"
```

#### Updating the border properties during runtime
If a `borders` process is already running, invoking a new `borders` instance
with any combination of the available options will update the properties of
the already running instance.

## Documentation
Local documentation is available as `man borders` and as a rendered version in
the [Wiki](https://github.com/FelixKratz/JankyBorders/wiki/Man-Page).
