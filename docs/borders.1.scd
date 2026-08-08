borders(1)

# NAME

JankyBorders - A window border system for macOS

# SYNOPSIS

*borders [options]*

# DESCRIPTION

*JankyBorders* is a lightweight tool designed to add colored borders to
user windows on macOS 14.0+. It enhances the user experience by visually
highlighting the currently focused window without relying on the accessibility
API, thereby being faster than comparable tools.

# OPTIONS

*style=<style>*
	Specifies the style of the border (either _round_ or _square_)
	For adaptive round borders, windows reporting a corner radius of _0_ or _1_
	remain completely square across both double-border layers.

*animation=<modes>*
	Specifies one or more animation modes for the focused window
	border, separated by commas. Available modes: _none_ (no
	animation), _fade_ (opacity fades in), _ramp_ (glow grows
	from zero to full), _slide_ (border slides from old window
	position), _pulse_ (adaptive breathing effect that expands thin borders
	more than thick borders). Multiple modes can be combined, e.g.
	animation=fade,ramp or animation=slide,pulse. Selected modes
	run simultaneously. The old border
	immediately switches to its inactive color and only the newly focused
	border animates. Focus changes while the primary mouse button is held
	are applied without animation. Ramp requires a glow color. Use
	animation=none by itself to disable animations.

*animation_duration=<seconds>*
	Sets the duration shared by all selected focus animations in seconds.
	The value must be finite and greater than zero. The default is _0.25_.

*animation_easing=<easing>*
	Sets the slide easing curve. Available values: _linear_,
	_ease_in_expo_, _ease_out_expo_, and _ease_in_out_expo_. The
	default is _linear_. Other animation modes are not affected.

*active_color=<color>*
	Sets the border color for the focused window (format: _0xAARRGGBB_). For
	instance, active_color=0xff00ff00 creates a green border. Use
	_double(<color>,<color>)_ to draw independently styled outer and inner
	borders. Each nested color accepts the complete color syntax described
	below.

*inactive_color=<color>*
	Sets the border color for all windows not in focus.

*background_color=<color>*
	Sets the background fill color for all windows (only _0xAARRGGBB_ arguments supported).
	The background is kept below the application window regardless of the border order.
	Acts as the fallback when a focused-state override is not configured.

*active_background_color=<color>*
	Overrides the background fill color for the focused window.

*inactive_background_color=<color>*
	Overrides the background fill color for unfocused windows.

*background_host=<host>*
	Selects the window that owns background fill and blur. _auto_ (the default)
	uses the border window with _order=below_ and a target-sized companion window
	with _order=above_. _border_ forces the original single-window implementation
	with either order; with _order=above_, fill and blur are composited above the
	application. _companion_ forces the target-sized companion with either order
	and keeps blur inside the application bounds.

*blur_radius=<float>*
	Sets the background blur radius. Values are clamped to _50_; _0_ disables blur.
	The blur layer remains below the application window and works with both
	_order=above_ and _order=below_. Below-order borders reuse their existing window;
	above-order borders use a companion window. Blur uses a private macOS API and
	may affect performance. Acts as the fallback when a focused-state override is
	not configured. Hidden and non-visible companion windows are released after a
	ten-second grace period unless their target becomes visible again first.

*active_blur_radius=<float>*
	Overrides the background blur radius for the focused window.

*inactive_blur_radius=<float>*
	Overrides the background blur radius for unfocused windows.

*width=<float>*
	Determines the width of the border. For example, width=5.0 creates a border
	5.0 points wide. For double colors, _width=double(4.0,2.0)_ configures the
	outer and inner widths respectively. A scalar width applies to both layers;
	for a single color, only the first double width is used.

*double_gap=<float>*
	Sets the transparent gap between double-border layers. The value must be
	finite and non-negative. The default is _0_.

*hidpi=<boolean>*
	If set to _on_, the border will be drawn with retina resolution.

*ax_focus=<boolean>*
	If set to _on_, the (slower) accessibility API is used to resolve the
	focused window. Enabled automatically if the (parent) process has
	accessibility permissions. Improves compatibility with other tools
	modifying window properties on a system level (e.g. yabai).

*blacklist=<application_list>*
	The applications specified here are excluded from being bordered. For
	example, blacklist="Safari,kitty" excludes Safari and kitty from being
	bordered.

*whitelist=<application_list>*
	Once this list is populated, only applications listed here are considered
	for receiving a border. If the whitelist is empty (default) it is inactive.

If an instance of *borders* is already running, subsequent invocations will
update the existing process with the new arguments.

If no instance of *borders* is running and no arguments are supplied, we try
to execute a file at ~/.config/borders/bordersrc where a configuration command
could be issued.

# NOMENCLATURE

*<float>*
	A floating point number.

*<boolean>*
	Either _on_ or _off_.

*<style>*
	Either _round_, _square_, or _uniform_.

*<color>*
	A color formatted as _0xAARRGGBB_, where _A_ is the alpha channel, _R_ the
	red channel, _G_ the green channel and _B_ the blue channel. All
	constituents take a base 16 number, i.e. _0-F_.

	The color argument can take the special values: ++
	_double(<color>,<color>)_ ++
	_multi(left=0xAARRGGBB,top=0xAARRGGBB,right=0xAARRGGBB,bottom=0xAARRGGBB)_ ++
	_gradient(top_left=0xAARRGGBB,bottom_right=0xAARRGGBB)_ ++
	_gradient(top_right=0xAARRGGBB,bottom_left=0xAARRGGBB)_ ++
	_glow(0xAARRGGBB)_ ++
	_glow(gradient(top_left=0xAARRGGBB,bottom_right=0xAARRGGBB))_ ++
	_glow(gradient(top_right=0xAARRGGBB,bottom_left=0xAARRGGBB))_ ++
(You might need to quote these arguments depending on your shell)
	The first double color is the outer border and the second is the inner
	border. Each can independently be a solid color, glow, gradient, or glowing
	gradient. For example: ++
	_double(glow(gradient(top_left=0xffff0000,bottom_right=0xff0000ff)),glow(0xffffffff))_ ++
	Wrap each nested color independently; _glow(double(...))_ is not supported.
	_multi(...)_ colors each edge independently. All four solid colors are
	required; it cannot be nested inside _double(...)_. Corners use their
	adjacent vertical edge color.

	The glow color for a gradient is the alpha-weighted blend of its two
	endpoint colors.

*<application_list>*
	A comma separated list of application names. This argument should be
	quoted.

# BUGS

Report them at https://github.com/FelixKratz/JankyBorders/issues.

# MAINTAINERS

Felix Kratz <felix.kratz@tu-dortmund.de>
