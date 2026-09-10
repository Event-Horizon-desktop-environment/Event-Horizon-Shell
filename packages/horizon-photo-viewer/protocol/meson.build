# List of protocol XML files to generate headers + code for
protocol_xmls = [
  'xdg-shell.xml',
  'viewporter.xml',
  'fractional-scale-v1.xml',
  'tearing-control-v1.xml',
  'color-management-v1.xml',
  'idle-inhibit-unstable-v1.xml',
  'keyboard-shortcuts-inhibit-unstable-v1.xml',
  'linux-dmabuf-v1.xml',
  'single-pixel-buffer-v1.xml',
  'xdg-output-unstable-v1.xml',
  'ext-data-control-v1.xml',
  'ext-foreign-toplevel-list-v1.xml',
  'ext-image-capture-source-v1.xml',
  'ext-image-copy-capture-v1.xml',
  'wlr-data-control-unstable-v1.xml',
  'wlr-foreign-toplevel-management-unstable-v1.xml',
  'wlr-screencopy-unstable-v1.xml',
]

# Generate public headers (usable by external code)
protocol_headers = []
foreach p : protocol_xmls
  xml = join_paths(meson.current_source_dir(), p)
  header = custom_target(p + '-header',
    input: xml,
    output: '@BASENAME@-client-protocol.h',
    command: [wl_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
  protocol_headers += header
endforeach

# Generate private code (compiled into our library)
protocol_code = []
foreach p : protocol_xmls
  xml = join_paths(meson.current_source_dir(), p)
  code = custom_target(p + '-code',
    input: xml,
    output: '@BASENAME@-protocol.c',
    command: [wl_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
  protocol_code += code
endforeach

# wlr-layer-shell-unstable-v1 needs the 'namespace' -> 'name_space' sed
# workaround because it uses 'namespace' as a property name, which is a
# reserved keyword in C++.
wlr_ls_xml = 'wlr-layer-shell-unstable-v1.xml'
wlr_ls_h = custom_target('wlr-layer-shell-unstable-v1-header',
  input: wlr_ls_xml,
  output: 'wlr-layer-shell-unstable-v1-client-protocol.h',
  command: [
    'sh', '-c',
    '"$1" client-header "$2" "$3" && sed s/namespace/name_space/g "$3" > "$3.tmp" && mv "$3.tmp" "$3"',
    '_', wl_scanner.full_path(), '@INPUT@', '@OUTPUT@',
  ],
)
wlr_ls_c = custom_target('wlr-layer-shell-unstable-v1-code',
  input: wlr_ls_xml,
  output: 'wlr-layer-shell-unstable-v1-client-protocol.c',
  command: [wl_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  depends: [wlr_ls_h],
)
protocol_headers += wlr_ls_h
protocol_code += wlr_ls_c

# Bundle into a dependency for src/
protocol_dep = declare_dependency(
  sources: protocol_headers + protocol_code,
  include_directories: include_directories('.'),
)
