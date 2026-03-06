project(
  'q3vibe-engine',
  'c',
  version : '0.1.0',
  license : 'GPL-3.0-or-later',
  meson_version : '>=1.3.0',
  default_options : [
    'warning_level=1',
    'b_vscrt=static_from_buildtype',
    'default_library=static',
  ],
)

system_name = host_machine.system()
cpu_family = host_machine.cpu_family()

is_windows = system_name == 'windows'
is_linux = system_name == 'linux'
is_darwin = system_name == 'darwin'
is_unix = is_linux or is_darwin

if not (is_windows or is_linux or is_darwin)
  error('The Meson build currently supports Windows, Linux, and macOS only.')
endif

if is_windows and cpu_family != 'x86_64'
  error('The Windows Meson build currently supports x86_64 only.')
endif

if is_unix and not (cpu_family == 'x86_64' or cpu_family == 'aarch64')
  error('The Linux/macOS Meson build currently supports x86_64 and aarch64 only.')
endif

cc = meson.get_compiler('c')
python = import('python').find_installation(required : true)

if is_windows
  windows_mod = import('windows')
endif

cname = 'quake3e'
dname = 'quake3e.ded'
renderer_prefix = cname
default_renderer = get_option('default_renderer')

build_opengl = get_option('build_opengl')
build_vulkan = get_option('build_vulkan')
build_opengl2 = get_option('build_opengl2')
use_curl = get_option('use_curl')
use_ogg_vorbis = get_option('use_ogg_vorbis')
use_sdl_opt = get_option('use_sdl')

if not (build_opengl or build_vulkan or build_opengl2)
  error('At least one renderer must be enabled.')
endif

if default_renderer == 'opengl' and not build_opengl
  error('default_renderer=opengl requires -Dbuild_opengl=true.')
endif
if default_renderer == 'vulkan' and not build_vulkan
  error('default_renderer=vulkan requires -Dbuild_vulkan=true.')
endif
if default_renderer == 'opengl2' and not build_opengl2
  error('default_renderer=opengl2 requires -Dbuild_opengl2=true.')
endif

platform_use_sdl = false
if is_windows
  platform_use_sdl = use_sdl_opt.enabled()
  if platform_use_sdl
    error('The Windows Meson path currently uses the native Win32 backend. SDL on Windows is not wired into Meson yet.')
  endif
else
  platform_use_sdl = not use_sdl_opt.disabled()
  if not platform_use_sdl
    error('The Linux/macOS Meson path currently requires the SDL backend.')
  endif
endif

render_arch_string = ''
exe_arch_suffix = ''

if cpu_family == 'x86_64'
  render_arch_string = 'x86_64'
  if is_darwin
    exe_arch_suffix = 'x86_64'
  else
    exe_arch_suffix = 'x64'
  endif
elif cpu_family == 'aarch64'
  render_arch_string = 'aarch64'
  exe_arch_suffix = 'aarch64'
endif

binext = '.' + exe_arch_suffix
renderer_prefix_define = '-DRENDERER_PREFIX="@0@"'.format(renderer_prefix)

project_c_args = []
if is_windows
  project_c_args += [
    '-D_CRT_SECURE_NO_WARNINGS',
    '-D_CRT_SECURE_NO_DEPRECATE',
    '-D_WINSOCK_DEPRECATED_NO_WARNINGS',
  ]
endif
if is_darwin
  project_c_args += ['-DMACOS_X']
endif

if project_c_args.length() > 0
  add_project_arguments(project_c_args, language : 'c')
endif

if is_windows
  engine_includes = include_directories(
    'code',
    'code/asm',
    'code/botlib',
    'code/client',
    'code/game',
    'code/qcommon',
    'code/renderer',
    'code/renderer2',
    'code/renderercommon',
    'code/renderercommon/vulkan',
    'code/renderervk',
    'code/server',
    'code/ui',
    'code/win32',
  )
else
  engine_includes = include_directories(
    'code',
    'code/asm',
    'code/botlib',
    'code/client',
    'code/game',
    'code/qcommon',
    'code/renderer',
    'code/renderer2',
    'code/renderercommon',
    'code/renderercommon/vulkan',
    'code/renderervk',
    'code/server',
    'code/ui',
    'code/unix',
    'code/sdl',
  )
endif

subdir('build-support/meson/sources')

qcommon_vm_arch_sources = []
if cpu_family == 'x86_64'
  qcommon_vm_arch_sources = qcommon_vm_x86_sources
elif cpu_family == 'aarch64'
  qcommon_vm_arch_sources = qcommon_vm_aarch64_sources
endif

common_platform_deps = []
client_link_deps = []
q3ui_build_deps = []
renderer_platform_deps = []
platform_core_sources = []
platform_ui_sources = []
platform_resource_sources = []
client_runtime_sources = []
q3ui_defines = []

if is_windows
  common_platform_deps = [
    cc.find_library('comctl32'),
    cc.find_library('gdi32'),
    cc.find_library('ole32'),
    cc.find_library('psapi'),
    cc.find_library('winmm'),
    cc.find_library('ws2_32'),
    cc.find_library('wsock32'),
  ]

  client_link_deps = common_platform_deps
  renderer_platform_deps = common_platform_deps
  platform_core_sources = win32_core_sources
  platform_ui_sources = win32_ui_sources
  platform_resource_sources = [windows_mod.compile_resources('code/win32/win_resource.rc')]

  if build_opengl or build_opengl2
    platform_ui_sources += win32_qgl_sources
    q3ui_defines += ['-DUSE_OPENGL_API']
  endif
  if build_vulkan
    platform_ui_sources += win32_qvk_sources
    q3ui_defines += ['-DUSE_VULKAN_API']
  endif
else
  sdl3_dep = dependency('sdl3', required : true, fallback : ['sdl3', 'sdl3_dep'])
  q3ui_build_deps += [sdl3_dep]
  client_link_deps += [sdl3_dep]

  m_dep = cc.find_library('m', required : false)
  if m_dep.found()
    common_platform_deps += [m_dep]
    renderer_platform_deps += [m_dep]
  endif

  dl_dep = cc.find_library('dl', required : false)
  if dl_dep.found()
    common_platform_deps += [dl_dep]
    renderer_platform_deps += [dl_dep]
  endif

  client_link_deps += common_platform_deps
  platform_core_sources = unix_core_sources
  platform_ui_sources = sdl_ui_sources

  if is_linux
    q3ui_defines += ['-DUSE_ICON']
  endif
  if build_opengl or build_opengl2
    q3ui_defines += ['-DUSE_OPENGL_API']
  endif
  if build_vulkan
    q3ui_defines += ['-DUSE_VULKAN_API']
  endif
endif

if is_unix and cpu_family == 'x86_64'
  unix_asm_compiler = find_program('cc', 'clang', 'gcc', required : true)
  unix_asm_args = ['-x', 'assembler-with-cpp']
  if is_linux
    unix_asm_args += ['-DELF']
  endif

  client_runtime_sources += custom_target(
    'snd_mix_x86_64',
    input : 'code/asm/snd_mix_x86_64.s',
    output : 'snd_mix_x86_64.o',
    command : [unix_asm_compiler] + unix_asm_args + ['-c', '@INPUT@', '-o', '@OUTPUT@'],
  )
endif

jpeg_dep = dependency('libjpeg', fallback : ['libjpeg', 'libjpeg_dep'], required : true)

client_external_deps = [jpeg_dep]
ogg_vorbis_defines = []

if use_ogg_vorbis
  vorbisfile_dep = dependency('vorbisfile', fallback : ['vorbis', 'vorbisfile_dep'], required : true)
  client_external_deps += [vorbisfile_dep]
  ogg_vorbis_defines += ['-DUSE_OGG_VORBIS']
endif

curl_defines = []
if use_curl
  if is_windows
    curl_dep = dependency('libcurl', fallback : ['libcurl', 'libcurl_dep'], required : true)
  else
    curl_dep = dependency('libcurl', required : true)
  endif
  client_external_deps += [curl_dep]
  curl_defines += ['-DUSE_CURL']
endif

botlib = static_library(
  'q3vibe_botlib',
  botlib_sources,
  include_directories : engine_includes,
  c_args : ['-DBOTLIB'],
)

qcommon_client = static_library(
  'q3vibe_qcommon_client',
  qcommon_sources + qcommon_vm_arch_sources + server_sources,
  include_directories : engine_includes,
)

qcommon_ded = static_library(
  'q3vibe_qcommon_ded',
  qcommon_sources + qcommon_vm_arch_sources + server_sources,
  include_directories : engine_includes,
  c_args : ['-DDEDICATED'],
)

client_defines = [
  '-DUSE_RENDERER_DLOPEN',
  renderer_prefix_define,
  '-DRENDERER_DEFAULT=@0@'.format(default_renderer),
] + curl_defines + ogg_vorbis_defines

client_all_sources = client_sources
if use_curl
  client_all_sources += client_curl_sources
endif

if build_opengl or build_opengl2
  client_defines += ['-DUSE_OPENGL_API']
endif
if build_vulkan
  client_defines += ['-DUSE_VULKAN_API']
endif

client_lib = static_library(
  'q3vibe_client',
  client_all_sources,
  include_directories : engine_includes,
  c_args : client_defines,
  dependencies : client_external_deps,
)

q3ui = static_library(
  'q3vibe_q3ui',
  platform_ui_sources,
  include_directories : engine_includes,
  c_args : q3ui_defines,
  dependencies : q3ui_build_deps,
)

renderer_common_args = ['-DUSE_RENDERER_DLOPEN']
renderer_base_sources = renderer_common_sources + renderer_shared_aux_sources

if build_opengl
  if is_darwin
    shared_library(
      renderer_prefix + '_opengl_' + render_arch_string,
      renderer_gl_sources + renderer_base_sources,
      include_directories : engine_includes,
      c_args : renderer_common_args,
      dependencies : renderer_platform_deps,
      name_prefix : '',
    )
  else
    shared_module(
      renderer_prefix + '_opengl_' + render_arch_string,
      renderer_gl_sources + renderer_base_sources,
      include_directories : engine_includes,
      c_args : renderer_common_args,
      dependencies : renderer_platform_deps,
      name_prefix : '',
    )
  endif
endif

if build_vulkan
  if is_darwin
    shared_library(
      renderer_prefix + '_vulkan_' + render_arch_string,
      renderer_vk_sources + renderer_base_sources,
      include_directories : engine_includes,
      c_args : renderer_common_args,
      dependencies : renderer_platform_deps,
      name_prefix : '',
    )
  else
    shared_module(
      renderer_prefix + '_vulkan_' + render_arch_string,
      renderer_vk_sources + renderer_base_sources,
      include_directories : engine_includes,
      c_args : renderer_common_args,
      dependencies : renderer_platform_deps,
      name_prefix : '',
    )
  endif
endif

if build_opengl2
  renderer2_stringified_sources = []
  foreach shader : renderer2_glsl_sources
    shader_name = shader.split('/')[-1].split('.')[0]
    renderer2_stringified_sources += custom_target(
      'renderer2_' + shader_name,
      input : shader,
      output : shader_name + '.c',
      command : [python, files('scripts/stringify_glsl.py'), '@INPUT@', '@OUTPUT@', shader_name],
    )
  endforeach

  if is_darwin
    shared_library(
      renderer_prefix + '_opengl2_' + render_arch_string,
      renderer2_sources + renderer_base_sources + renderer2_stringified_sources,
      include_directories : engine_includes,
      c_args : renderer_common_args + ['-DUSE_RENDERER2'],
      dependencies : renderer_platform_deps,
      name_prefix : '',
    )
  else
    shared_module(
      renderer_prefix + '_opengl2_' + render_arch_string,
      renderer2_sources + renderer_base_sources + renderer2_stringified_sources,
      include_directories : engine_includes,
      c_args : renderer_common_args + ['-DUSE_RENDERER2'],
      dependencies : renderer_platform_deps,
      name_prefix : '',
    )
  endif
endif

client_link_with = [
  qcommon_client,
  botlib,
  client_lib,
  q3ui,
]

dedicated_link_with = [
  qcommon_ded,
  botlib,
]

if is_windows
  client_exe = executable(
    cname + binext,
    platform_core_sources + platform_resource_sources + client_runtime_sources,
    include_directories : engine_includes,
    link_with : client_link_with,
    dependencies : client_external_deps + client_link_deps,
    name_prefix : '',
    win_subsystem : 'windows',
  )

  dedicated_exe = executable(
    dname + binext,
    platform_core_sources + platform_resource_sources,
    include_directories : engine_includes,
    c_args : ['-DDEDICATED'],
    link_with : dedicated_link_with,
    dependencies : common_platform_deps,
    name_prefix : '',
    win_subsystem : 'windows',
  )
else
  client_exe = executable(
    cname + binext,
    platform_core_sources + client_runtime_sources,
    include_directories : engine_includes,
    link_with : client_link_with,
    dependencies : client_external_deps + client_link_deps,
    name_prefix : '',
  )

  dedicated_exe = executable(
    dname + binext,
    platform_core_sources,
    include_directories : engine_includes,
    c_args : ['-DDEDICATED'],
    link_with : dedicated_link_with,
    dependencies : common_platform_deps,
    name_prefix : '',
  )
endif

summary(
  {
    'Platform' : system_name + ' ' + cpu_family,
    'Client binary' : client_exe.full_path(),
    'Dedicated binary' : dedicated_exe.full_path(),
    'SDL backend' : platform_use_sdl,
    'libcurl' : use_curl,
    'OGG/Vorbis' : use_ogg_vorbis,
    'OpenGL renderer' : build_opengl,
    'Vulkan renderer' : build_vulkan,
    'OpenGL2 renderer' : build_opengl2,
  },
  section : 'Q3VIBE',
  bool_yn : true,
)
