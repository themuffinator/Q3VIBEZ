baseq3a_common_compile_args = []
if is_windows
  baseq3a_common_compile_args += [
    '-DWIN32',
    '-D_CRT_SECURE_NO_WARNINGS',
  ]
elif is_darwin
  baseq3a_common_compile_args += [
    '-DID_INLINE=inline',
    '-DPATH_SEP=\'/\'',
  ]
endif

if cc.has_argument('-fno-strict-aliasing')
  baseq3a_common_compile_args += ['-fno-strict-aliasing']
endif

baseq3a_module_deps = []
if not is_windows
  m_dep = cc.find_library('m', required : false)
  if m_dep.found()
    baseq3a_module_deps += [m_dep]
  endif
endif

baseq3a_includes = include_directories(
  '.',
  'sgame',
  'cgame',
  'ui',
  'q3_ui',
)

baseq3a_runtime_common = static_library(
  'q3vibe_baseq3a_common',
  files(
    'sgame/q_math.cpp',
    'sgame/q_shared.cpp',
  ),
  include_directories : baseq3a_includes,
  c_args : baseq3a_common_compile_args,
  cpp_args : baseq3a_common_compile_args,
  pic : true,
)

baseq3a_common_dep = declare_dependency(
  compile_args : baseq3a_common_compile_args,
  include_directories : baseq3a_includes,
  link_with : baseq3a_runtime_common,
)

baseq3a_qagame_sources = files(
  'sgame/g_main.cpp',
  'sgame/ai_chat.cpp',
  'sgame/ai_cmd.cpp',
  'sgame/ai_dmnet.cpp',
  'sgame/ai_dmq3.cpp',
  'sgame/ai_main.cpp',
  'sgame/ai_team.cpp',
  'sgame/ai_vcmd.cpp',
  'sgame/bg_lib.cpp',
  'sgame/bg_misc.cpp',
  'sgame/bg_pmove.cpp',
  'sgame/bg_slidemove.cpp',
  'sgame/g_active.cpp',
  'sgame/g_arenas.cpp',
  'sgame/g_bot.cpp',
  'sgame/g_client.cpp',
  'sgame/g_cmds.cpp',
  'sgame/g_combat.cpp',
  'sgame/g_items.cpp',
  'sgame/g_mem.cpp',
  'sgame/g_misc.cpp',
  'sgame/g_missile.cpp',
  'sgame/g_mover.cpp',
  'sgame/g_rotation.cpp',
  'sgame/g_session.cpp',
  'sgame/g_spawn.cpp',
  'sgame/g_svcmds.cpp',
  'sgame/g_syscalls.cpp',
  'sgame/g_target.cpp',
  'sgame/g_team.cpp',
  'sgame/g_trigger.cpp',
  'sgame/g_utils.cpp',
  'sgame/g_unlagged.cpp',
  'sgame/g_weapon.cpp',
)

baseq3a_cgame_sources = files(
  'cgame/cg_main.cpp',
  'sgame/bg_lib.cpp',
  'sgame/bg_misc.cpp',
  'sgame/bg_pmove.cpp',
  'sgame/bg_slidemove.cpp',
  'cgame/cg_consolecmds.cpp',
  'cgame/cg_draw.cpp',
  'cgame/cg_drawtools.cpp',
  'cgame/cg_effects.cpp',
  'cgame/cg_ents.cpp',
  'cgame/cg_event.cpp',
  'cgame/cg_info.cpp',
  'cgame/cg_localents.cpp',
  'cgame/cg_marks.cpp',
  'cgame/cg_players.cpp',
  'cgame/cg_playerstate.cpp',
  'cgame/cg_predict.cpp',
  'cgame/cg_scoreboard.cpp',
  'cgame/cg_servercmds.cpp',
  'cgame/cg_snapshot.cpp',
  'cgame/cg_syscalls.cpp',
  'cgame/cg_view.cpp',
  'cgame/cg_weapons.cpp',
)

baseq3a_ui_sources = files(
  'q3_ui/ui_main.cpp',
  'sgame/bg_misc.cpp',
  'sgame/bg_lib.cpp',
  'q3_ui/ui_addbots.cpp',
  'q3_ui/ui_atoms.cpp',
  'q3_ui/ui_cdkey.cpp',
  'q3_ui/ui_cinematics.cpp',
  'q3_ui/ui_confirm.cpp',
  'q3_ui/ui_connect.cpp',
  'q3_ui/ui_controls2.cpp',
  'q3_ui/ui_credits.cpp',
  'q3_ui/ui_demo2.cpp',
  'q3_ui/ui_display.cpp',
  'q3_ui/ui_gameinfo.cpp',
  'q3_ui/ui_ingame.cpp',
  'q3_ui/ui_loadconfig.cpp',
  'q3_ui/ui_menu.cpp',
  'q3_ui/ui_mfield.cpp',
  'q3_ui/ui_mods.cpp',
  'q3_ui/ui_network.cpp',
  'q3_ui/ui_options.cpp',
  'q3_ui/ui_playermodel.cpp',
  'q3_ui/ui_players.cpp',
  'q3_ui/ui_playersettings.cpp',
  'q3_ui/ui_preferences.cpp',
  'q3_ui/ui_qmenu.cpp',
  'q3_ui/ui_removebots.cpp',
  'q3_ui/ui_saveconfig.cpp',
  'q3_ui/ui_serverinfo.cpp',
  'q3_ui/ui_servers2.cpp',
  'q3_ui/ui_setup.cpp',
  'q3_ui/ui_sound.cpp',
  'q3_ui/ui_sparena.cpp',
  'q3_ui/ui_specifyserver.cpp',
  'q3_ui/ui_splevel.cpp',
  'q3_ui/ui_sppostgame.cpp',
  'q3_ui/ui_spskill.cpp',
  'q3_ui/ui_startserver.cpp',
  'q3_ui/ui_syscalls.cpp',
  'q3_ui/ui_team.cpp',
  'q3_ui/ui_teamorders.cpp',
  'q3_ui/ui_video.cpp',
)

baseq3a_link_deps = [baseq3a_common_dep] + baseq3a_module_deps

if is_darwin
  baseq3a_qagame = shared_library(
    'qagame' + render_arch_string,
    baseq3a_qagame_sources,
    include_directories : baseq3a_includes,
    c_args : ['-DQAGAME'],
    cpp_args : ['-DQAGAME'],
    dependencies : baseq3a_link_deps,
    gnu_symbol_visibility : 'hidden',
    name_prefix : '',
  )

  baseq3a_cgame = shared_library(
    'cgame' + render_arch_string,
    baseq3a_cgame_sources,
    include_directories : baseq3a_includes,
    c_args : ['-DCGAME'],
    cpp_args : ['-DCGAME'],
    dependencies : baseq3a_link_deps,
    gnu_symbol_visibility : 'hidden',
    name_prefix : '',
  )

  baseq3a_ui = shared_library(
    'ui' + render_arch_string,
    baseq3a_ui_sources,
    include_directories : baseq3a_includes,
    dependencies : baseq3a_link_deps,
    gnu_symbol_visibility : 'hidden',
    name_prefix : '',
  )
else
  baseq3a_qagame = shared_module(
    'qagame' + render_arch_string,
    baseq3a_qagame_sources,
    include_directories : baseq3a_includes,
    c_args : ['-DQAGAME'],
    cpp_args : ['-DQAGAME'],
    dependencies : baseq3a_link_deps,
    gnu_symbol_visibility : 'hidden',
    name_prefix : '',
  )

  baseq3a_cgame = shared_module(
    'cgame' + render_arch_string,
    baseq3a_cgame_sources,
    include_directories : baseq3a_includes,
    c_args : ['-DCGAME'],
    cpp_args : ['-DCGAME'],
    dependencies : baseq3a_link_deps,
    gnu_symbol_visibility : 'hidden',
    name_prefix : '',
  )

  baseq3a_ui = shared_module(
    'ui' + render_arch_string,
    baseq3a_ui_sources,
    include_directories : baseq3a_includes,
    dependencies : baseq3a_link_deps,
    gnu_symbol_visibility : 'hidden',
    name_prefix : '',
  )
endif
