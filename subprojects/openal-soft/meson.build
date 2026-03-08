project('openal-soft', 'c', 'cpp', version : '1.25.1', license : 'LGPL-2.0-or-later')

cmake = import('cmake')

openal_options = cmake.subproject_options()
openal_options.add_cmake_defines({
  'CMAKE_POLICY_DEFAULT_CMP0091' : 'NEW',
  'CMAKE_SUPPRESS_DEVELOPER_WARNINGS' : 'TRUE',
  'CMAKE_MSVC_RUNTIME_LIBRARY' : 'MultiThreaded$<$<CONFIG:Debug>:Debug>',
  'CMAKE_CXX_SCAN_FOR_MODULES' : 'OFF',
  'FORCE_STATIC_VCRT' : 'ON',
  'LIBTYPE' : 'STATIC',
  'ALSOFT_WERROR' : 'OFF',
  'ALSOFT_UTILS' : 'OFF',
  'ALSOFT_NO_CONFIG_UTIL' : 'ON',
  'ALSOFT_EXAMPLES' : 'OFF',
  'ALSOFT_TESTS' : 'OFF',
  'ALSOFT_INSTALL' : 'OFF',
  'ALSOFT_INSTALL_CONFIG' : 'OFF',
  'ALSOFT_INSTALL_HRTF_DATA' : 'OFF',
  'ALSOFT_INSTALL_AMBDEC_PRESETS' : 'OFF',
  'ALSOFT_INSTALL_EXAMPLES' : 'OFF',
  'ALSOFT_INSTALL_UTILS' : 'OFF',
  'ALSOFT_UPDATE_BUILD_VERSION' : 'OFF',
  'ALSOFT_BUILD_ROUTER' : 'OFF',
  'ALSOFT_BACKEND_WASAPI' : 'OFF',
  'ALSOFT_BACKEND_DSOUND' : 'OFF',
  'ALSOFT_BACKEND_WINMM' : 'ON',
  'ALSOFT_BACKEND_WAVE' : 'OFF',
  'ALSOFT_EAX' : 'ON',
  'ALSOFT_ENABLE_MODULES' : 'OFF',
  'ALSOFT_NO_UID_DEFS' : 'ON',
  'ALSOFT_EMBED_HRTF_DATA' : 'ON',
})

openal_proj = cmake.subproject('openal-soft-upstream', options : openal_options)
openal_dep = openal_proj.dependency('OpenAL')
