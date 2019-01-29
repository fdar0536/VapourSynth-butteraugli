project('vsbutteraugli', 'cpp',
  default_options : ['buildtype=release', 'b_ndebug=if-release', 'cpp_std=c++11'],
  meson_version : '>=0.48.0',
  version : '1'
)

sources = [
  'vsbutteraugli.cpp',
  'butteraugli/butteraugli/butteraugli.cc',
  'butteraugli/butteraugli/butteraugli.h'
]

vapoursynth_dep = dependency('vapoursynth').partial_dependency(compile_args : true, includes : true)

shared_module('vsbutteraugli', sources,
  dependencies : vapoursynth_dep,
  install : true,
  include_directories : include_directories('./butteraugli'),
  install_dir : join_paths(vapoursynth_dep.get_pkgconfig_variable('libdir'), 'vapoursynth'),
  gnu_symbol_visibility : 'hidden'
)
