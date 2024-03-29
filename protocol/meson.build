wayland_protos = dependency('wayland-protocols',
	version: '>=1.31',
	fallback: 'wayland-protocols',
	default_options: ['tests=false'],
)
wl_protocol_dir = wayland_protos.get_variable('pkgdatadir')

wayland_scanner_dep = dependency('wayland-scanner', native: true)
wayland_scanner = find_program(
	wayland_scanner_dep.get_variable('wayland_scanner'),
	native: true,
)

protocols = {
	# Stable upstream protocols
	'xdg-shell': wl_protocol_dir / 'stable/xdg-shell/xdg-shell.xml',

	# Other protocols
	'wlr-foreign-toplevel-management-unstable-v1': 'wlr-foreign-toplevel-management-unstable-v1.xml',
}

protocols_code = {}
protocols_client_header = {}
foreach name, path : protocols
	code = custom_target(
		name.underscorify() + '_c',
		input: path,
		output: '@BASENAME@-protocol.c',
		command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
	)

	client_header = custom_target(
		name.underscorify() + '_client_h',
		input: path,
		output: '@BASENAME@-client-protocol.h',
		command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
		build_by_default: false,
	)

	protocols_code += { name: code }
	protocols_client_header += { name: client_header }
endforeach
