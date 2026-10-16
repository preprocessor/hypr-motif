project('cde', 'cpp',
  version: '0.1',
  default_options: ['buildtype=release'],
)

cpp_compiler = meson.get_compiler('cpp')
if cpp_compiler.has_argument('-std=c++23')
  add_global_arguments('-std=c++23', language: 'cpp')
elif cpp_compiler.has_argument('-std=c++2b')
  add_global_arguments('-std=c++2b', language: 'cpp')
else
  error('Could not configure current C++ compiler (' + cpp_compiler.get_id() + ' ' + cpp_compiler.version() + ') with required C++ standard (C++23)')
endif

add_project_arguments(
  [
    '-Wno-narrowing',
  ],
  language: 'cpp')

hyprland = dependency('hyprland')

shared_module('cde',
  files(
    'src/main.cpp',
    'src/CCdeDecoration.cpp',
    'src/chrome.cpp',
    'src/config.cpp',
  ),
  dependencies: [hyprland],
  install: true,
)
