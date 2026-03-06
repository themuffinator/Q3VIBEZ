project('sdl3', 'c', version : '3.4.2', license : 'zlib')

cmake = import('cmake')

sdl3_options = cmake.subproject_options()
sdl3_options.add_cmake_defines({
  'SDL_SHARED' : 'ON',
  'SDL_STATIC' : 'OFF',
  'SDL_TESTS' : 'OFF',
  'SDL_TEST_LIBRARY' : 'OFF',
  'SDL_INSTALL' : 'OFF',
  'SDL_INSTALL_TESTS' : 'OFF',
})

sdl3_proj = cmake.subproject('sdl3-upstream', options : sdl3_options)
sdl3_headers_dep = sdl3_proj.dependency('SDL3_Headers')
sdl3_dep = declare_dependency(
  dependencies : [sdl3_headers_dep],
  link_with : sdl3_proj.target('SDL3-shared'),
)
