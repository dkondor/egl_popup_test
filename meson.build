project(
	'egl-popup-test',
	'c',
	license: 'MIT',
	meson_version: '>=0.59.0',
	default_options: [
		'c_std=c11',
		'warning_level=2',
		'werror=true',
	],
)

cc = meson.get_compiler('c')

add_project_arguments(cc.get_supported_arguments([
	'-Wundef',
	'-Wlogical-op',
	'-Wmissing-include-dirs',
	'-Wold-style-definition',
	'-Wpointer-arith',
	'-Winit-self',
	'-Wstrict-prototypes',
	'-Wimplicit-fallthrough=2',
	'-Wendif-labels',
	'-Wstrict-aliasing=2',
	'-Woverflow',
	'-Wmissing-prototypes',
	'-Walloca',

	'-Wno-missing-braces',
	'-Wno-missing-field-initializers',
	'-Wno-unused-parameter',
]), language: 'c')

subdir('protocol')

wayland_egl = dependency('wayland-egl')
wayland_cursor = dependency('wayland-cursor')
wayland_client = dependency('wayland-client')
egl = dependency('egl', required: false, disabler: true)
glesv2 = dependency('glesv2', required: false, disabler: true)

clients = {
	'egl-popup-test': {
		'src': ['egl-popup-test.c', 'egl_common.c'],
		'dep': [wayland_egl, wayland_cursor, egl, glesv2],
		'proto': [
			'wlr-foreign-toplevel-management-unstable-v1',
			'xdg-shell',
		],
	},
}

foreach name, info : clients
	extra_src = []
	foreach p : info.get('proto')
		extra_src += protocols_code[p]
		extra_src += protocols_client_header[p]
	endforeach

	executable(
		name,
		[info.get('src'), extra_src],
		dependencies: [wayland_client, info.get('dep', [])],
	)
endforeach
