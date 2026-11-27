#============================================================================
# Copyright (C) 2013 - 2018, OpenJK contributors
#
# This file is part of the OpenJK source code.
#
# OpenJK is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.
#============================================================================

# Make sure the user is not executing this script directly
if(NOT InOpenJK)
	message(FATAL_ERROR "Use the top-level cmake script!")
endif(NOT InOpenJK)

set(MPGameIncludeDirectories
	"${MPDir}"
	"${SharedDir}"
	"${GSLIncludeDirectory}"
	"${CMAKE_BINARY_DIR}/shared"
	"${MPDir}/game/detour"
	)
if(WIN32)
	set(MPGameLibraries "winmm")
endif(WIN32)
set(MPGameDefines ${MPSharedDefines} "_GAME" )
set(MPGameGameFiles
	"${MPDir}/game/ai_bot2.c"
	"${MPDir}/game/ai_bot2.h"
	"${MPDir}/game/ai_bot2_internal.h"
	"${MPDir}/game/ai_bot2_localnav.c"
	"${MPDir}/game/ai_bot2_movement.c"
	"${MPDir}/game/ai_bot2_objective.c"
	"${MPDir}/game/bot2_aim.c"
	"${MPDir}/game/bot2_aim.h"
	"${MPDir}/game/bot2_commands.c"
	"${MPDir}/game/bot2_commands.h"
	"${MPDir}/game/bot2_corner.c"
	"${MPDir}/game/bot2_corner.h"
	"${MPDir}/game/bot2_defender.c"
	"${MPDir}/game/bot2_defender.h"
	"${MPDir}/game/bot2_finecmd.c"
	"${MPDir}/game/bot2_finecmd.h"
	"${MPDir}/game/bot2_forecast.c"
	"${MPDir}/game/bot2_forecast.h"
	"${MPDir}/game/bot2_graph.c"
	"${MPDir}/game/bot2_graph.h"
	"${MPDir}/game/bot2_kick.c"
	"${MPDir}/game/bot2_kick.h"
	"${MPDir}/game/bot2_move.c"
	"${MPDir}/game/bot2_move.h"
	"${MPDir}/game/bot2_movebench.c"
	"${MPDir}/game/bot2_movebench.h"
	"${MPDir}/game/bot2_movecontrol.c"
	"${MPDir}/game/bot2_movecontrol.h"
	"${MPDir}/game/bot2_movecost.c"
	"${MPDir}/game/bot2_movecost.h"
	"${MPDir}/game/bot2_mover.c"
	"${MPDir}/game/bot2_mover.h"
	"${MPDir}/game/bot2_probe.c"
	"${MPDir}/game/bot2_probe.h"
	"${MPDir}/game/bot2_route_play.c"
	"${MPDir}/game/bot2_route_play.h"
	"${MPDir}/game/bot2_route_record.c"
	"${MPDir}/game/bot2_route_record.h"
	"${MPDir}/game/bot2_svcmds.c"
	"${MPDir}/game/bot2_svcmds.h"
	"${MPDir}/game/bot2_threat.c"
	"${MPDir}/game/bot2_threat.h"
	"${MPDir}/game/bot2_wallprobe.c"
	"${MPDir}/game/bot2_wallprobe.h"
	"${MPDir}/game/ai_main.c"
	"${MPDir}/game/ai_util.c"
	"${MPDir}/game/ai_wpnav.c"
	"${MPDir}/game/AnimalNPC.c"
	"${MPDir}/game/be_force.c"
	"${MPDir}/game/be_force.h"
	"${MPDir}/game/be_collections.c"
	"${MPDir}/game/be_collections.h"
	"${MPDir}/game/be_combat.c"
	"${MPDir}/game/be_combat.h"
	"${MPDir}/game/be_pmove.c"
	"${MPDir}/game/be_pmove.h"
	"${MPDir}/game/be_stats.c"
	"${MPDir}/game/be_stats.h"
	"${MPDir}/game/be_table.c"
	"${MPDir}/game/be_table.h"
	"${MPDir}/game/bg_g2_utils.c"
	"${MPDir}/game/bg_misc.c"
	"${MPDir}/game/bg_panimate.c"
	"${MPDir}/game/bg_pmove.c"
	"${MPDir}/game/bg_saber.c"
	"${MPDir}/game/bg_saberLoad.c"
	"${MPDir}/game/bg_saga.c"
	"${MPDir}/game/bg_slidemove.c"
	"${MPDir}/game/bg_vehicleLoad.c"
	"${MPDir}/game/bg_bike.c"
	"${MPDir}/game/bg_bikemove.c"
	"${MPDir}/game/bg_weapons.c"
	"${MPDir}/game/FighterNPC.c"
	"${MPDir}/game/g_active.c"
	"${MPDir}/game/g_bot.c"
	"${MPDir}/game/g_client.c"
	"${MPDir}/game/g_cmds.c"
	"${MPDir}/game/g_combat.c"
	"${MPDir}/game/g_cvar.c"
	"${MPDir}/game/g_damagefeedback.c"
	"${MPDir}/game/g_damagefeedback.h"
	"${MPDir}/game/g_exphysics.c"
	"${MPDir}/game/g_labs.c"
	"${MPDir}/game/g_labs.h"
	"${MPDir}/game/g_ICARUScb.c"
	"${MPDir}/game/g_items.c"
	"${MPDir}/game/g_crash_handler.c"
	"${MPDir}/game/g_crash_handler.h"
	"${MPDir}/game/g_log.c"
	"${MPDir}/game/g_main.c"
	"${MPDir}/game/g_mem.c"
	"${MPDir}/game/g_misc.c"
	"${MPDir}/game/g_missile.c"
	"${MPDir}/game/g_mover.c"
	"${MPDir}/game/g_nav.c"
	"${MPDir}/game/g_navnew.c"
	"${MPDir}/game/g_navmesh.cpp"
	"${MPDir}/game/g_navmesh.h"
	"${MPDir}/game/g_object.c"
	"${MPDir}/game/g_saga.c"
	"${MPDir}/game/g_session.c"
	"${MPDir}/game/g_spawn.c"
	"${MPDir}/game/g_svcmds.c"
	"${MPDir}/game/g_syscalls.c"
	"${MPDir}/game/g_target.c"
	"${MPDir}/game/g_team.c"
	"${MPDir}/game/g_timer.c"
	"${MPDir}/game/g_trigger.c"
	"${MPDir}/game/g_tick.c"
	"${MPDir}/game/g_tick.h"
	"${MPDir}/game/g_turret.c"
	"${MPDir}/game/g_turret_G2.c"
	"${MPDir}/game/g_utils.c"
	"${MPDir}/game/g_vehicles.c"
	"${MPDir}/game/g_vehicleTurret.c"
	"${MPDir}/game/g_weapon.c"
	"${MPDir}/game/NPC.c"
	"${MPDir}/game/NPC_AI_Atst.c"
	"${MPDir}/game/NPC_AI_Default.c"
	"${MPDir}/game/NPC_AI_Droid.c"
	"${MPDir}/game/NPC_AI_GalakMech.c"
	"${MPDir}/game/NPC_AI_Grenadier.c"
	"${MPDir}/game/NPC_AI_Howler.c"
	"${MPDir}/game/NPC_AI_ImperialProbe.c"
	"${MPDir}/game/NPC_AI_Interrogator.c"
	"${MPDir}/game/NPC_AI_Jedi.c"
	"${MPDir}/game/NPC_AI_Mark1.c"
	"${MPDir}/game/NPC_AI_Mark2.c"
	"${MPDir}/game/NPC_AI_MineMonster.c"
	"${MPDir}/game/NPC_AI_Rancor.c"
	"${MPDir}/game/NPC_AI_Remote.c"
	"${MPDir}/game/NPC_AI_Seeker.c"
	"${MPDir}/game/NPC_AI_Sentry.c"
	"${MPDir}/game/NPC_AI_Sniper.c"
	"${MPDir}/game/NPC_AI_Stormtrooper.c"
	"${MPDir}/game/NPC_AI_Utils.c"
	"${MPDir}/game/NPC_AI_Wampa.c"
	"${MPDir}/game/NPC_behavior.c"
	"${MPDir}/game/NPC_combat.c"
	"${MPDir}/game/NPC_goal.c"
	"${MPDir}/game/NPC_misc.c"
	"${MPDir}/game/NPC_move.c"
	"${MPDir}/game/NPC_reactions.c"
	"${MPDir}/game/NPC_senses.c"
	"${MPDir}/game/NPC_sounds.c"
	"${MPDir}/game/NPC_spawn.c"
	"${MPDir}/game/NPC_stats.c"
	"${MPDir}/game/NPC_utils.c"
	"${MPDir}/game/SpeederNPC.c"
	"${MPDir}/game/BikeNPC.c"
	"${MPDir}/game/tri_coll_test.c"
	"${MPDir}/game/w_force.c"
	"${MPDir}/game/w_saber.c"
	"${MPDir}/game/g_saberspear.c"
	"${MPDir}/game/WalkerNPC.c"
	"${MPDir}/game/ai.h"
	"${MPDir}/game/ai_main.h"
	"${MPDir}/game/anims.h"
	"${MPDir}/game/b_local.h"
	"${MPDir}/game/b_public.h"
	"${MPDir}/game/bg_local.h"
	"${MPDir}/game/bg_public.h"
	"${MPDir}/game/bg_saga.h"
	"${MPDir}/game/bg_vehicles.h"
	"${MPDir}/game/bg_bike.h"
	"${MPDir}/game/bg_weapons.h"
	"${MPDir}/game/chars.h"
	"${MPDir}/game/g_ICARUScb.h"
	"${MPDir}/game/g_local.h"
	"${MPDir}/game/g_nav.h"
	"${MPDir}/game/g_public.h"
	"${MPDir}/game/g_team.h"
	"${MPDir}/game/g_xcvar.h"
	"${MPDir}/game/inv.h"
	"${MPDir}/game/match.h"
	"${MPDir}/game/npc_headers.h"
	"${MPDir}/game/say.h"
	"${MPDir}/game/surfaceflags.h"
	"${MPDir}/game/teams.h"
	"${MPDir}/game/w_saber.h"
	)
source_group("game" FILES ${MPGameGameFiles})
set(MPGameFiles ${MPGameFiles} ${MPGameGameFiles})

# Console-only measurement labs behind g_labs.h; without them the hooks are stubs.
if(BuildGameLabs)
	set(MPGameLabFiles
		"${MPDir}/game/g_hitcoverage.c"
		"${MPDir}/game/g_hitcoverage.h"
		"${MPDir}/game/g_levelcheck.c"
		"${MPDir}/game/g_saberbench.c"
		"${MPDir}/game/g_saberbench.h"
		"${MPDir}/game/g_saberlab.c"
		"${MPDir}/game/g_saberlab.h"
		)
	source_group("game" FILES ${MPGameLabFiles})
	set(MPGameFiles ${MPGameFiles} ${MPGameLabFiles})
else()
	set(MPGameDefines ${MPGameDefines} "XENJK_NO_GAME_LABS")
endif()

set(MPGameCommonFiles
	"${MPDir}/qcommon/q_shared.c"
	"${MPDir}/qcommon/disablewarnings.h"
	"${MPDir}/qcommon/q_shared.h"
	"${MPDir}/qcommon/tags.h"

	${SharedCommonFiles}
	)
source_group("common" FILES ${MPGameCommonFiles})
set(MPGameFiles ${MPGameFiles} ${MPGameCommonFiles})

set(MPGameCommonSafeFiles
	${SharedCommonSafeFiles}
	)
source_group("common/safe" FILES ${MPGameCommonSafeFiles})
set(MPGameFiles ${MPGameFiles} ${MPGameCommonSafeFiles})

set(MPGameCgameFiles
	"${MPDir}/cgame/animtable.h"
	"${MPDir}/cgame/cg_public.h"
	)
source_group("cgame" FILES ${MPGameCgameFiles})
set(MPGameFiles ${MPGameFiles} ${MPGameCgameFiles})

set(MPGameRendererFiles
	"${MPDir}/rd-common/tr_types.h"
	)
source_group("rd-common" FILES ${MPGameRendererFiles})
set(MPGameFiles ${MPGameFiles} ${MPGameRendererFiles})

set(MPGameBotlibFiles
	"${MPDir}/botlib/be_aas.h"
	"${MPDir}/botlib/be_ai_char.h"
	"${MPDir}/botlib/be_ai_chat.h"
	"${MPDir}/botlib/be_ai_gen.h"
	"${MPDir}/botlib/be_ai_goal.h"
	"${MPDir}/botlib/be_ai_move.h"
	"${MPDir}/botlib/be_ai_weap.h"
	"${MPDir}/botlib/be_ea.h"
	"${MPDir}/botlib/botlib.h"
	)
source_group("botlib" FILES ${MPGameBotlibFiles})
set(MPGameFiles ${MPGameFiles} ${MPGameBotlibFiles})

set(MPGameG2Files
	"${MPDir}/ghoul2/G2.h"
	)
source_group("ghoul2" FILES ${MPGameG2Files})
set(MPGameFiles ${MPGameFiles} ${MPGameG2Files})

set(MPGameIcarusFiles
	"${MPDir}/icarus/interpreter.h"
	"${MPDir}/icarus/Q3_Interface.h"
	)
source_group("icarus" FILES ${MPGameIcarusFiles})
set(MPGameFiles ${MPGameFiles} ${MPGameIcarusFiles})

set(MPGameUiFiles
	"${MPDir}/ui/menudef.h"
	)
source_group("ui" FILES ${MPGameUiFiles})
set(MPGameFiles ${MPGameFiles} ${MPGameUiFiles})

set(MPGameDetourFiles
	"${MPDir}/game/detour/DetourAlloc.cpp"
	"${MPDir}/game/detour/DetourAssert.cpp"
	"${MPDir}/game/detour/DetourCommon.cpp"
	"${MPDir}/game/detour/DetourNavMesh.cpp"
	"${MPDir}/game/detour/DetourNavMeshQuery.cpp"
	"${MPDir}/game/detour/DetourNode.cpp"
	"${MPDir}/game/detour/DetourAlloc.h"
	"${MPDir}/game/detour/DetourAssert.h"
	"${MPDir}/game/detour/DetourCommon.h"
	"${MPDir}/game/detour/DetourMath.h"
	"${MPDir}/game/detour/DetourNavMesh.h"
	"${MPDir}/game/detour/DetourNavMeshQuery.h"
	"${MPDir}/game/detour/DetourNode.h"
	"${MPDir}/game/detour/DetourStatus.h"
	)
source_group("detour" FILES ${MPGameDetourFiles})
set(MPGameFiles ${MPGameFiles} ${MPGameDetourFiles})

add_library(${MPGame} SHARED ${MPGameFiles})

if(NOT MSVC)
	# remove "lib" prefix for .so/.dylib files
	set_target_properties(${MPGame} PROPERTIES PREFIX "")
endif()

if(MakeApplicationBundles AND BuildMPEngine)
	install(TARGETS ${MPGame}
		LIBRARY
		DESTINATION "${JKAInstallDir}/${MPEngine}.app/Contents/MacOS/OpenJK"
		COMPONENT ${JKAMPCoreComponent})
	install(TARGETS ${MPGame}
		LIBRARY
		DESTINATION "${JKAInstallDir}/${MPEngine}.app/Contents/MacOS/base"
		COMPONENT ${JKAMPCoreComponent})
elseif(WIN32)
	install(TARGETS ${MPGame}
		RUNTIME
		DESTINATION "${JKAInstallDir}/OpenJK"
		COMPONENT ${JKAMPCoreComponent})

	# Use OpenJK modules as default
	install(TARGETS ${MPGame}
		RUNTIME
		DESTINATION "${JKAInstallDir}/base"
		COMPONENT ${JKAMPCoreComponent})
else()
	install(TARGETS ${MPGame}
		LIBRARY
		DESTINATION "${JKAInstallDir}/OpenJK"
		COMPONENT ${JKAMPCoreComponent})
	install(TARGETS ${MPGame}
		LIBRARY
		DESTINATION "${JKAInstallDir}/base"
		COMPONENT ${JKAMPCoreComponent})
endif()

set_target_properties(${MPGame} PROPERTIES COMPILE_DEFINITIONS "${MPGameDefines}")

# Hide symbols not explicitly marked public.
set_property(TARGET ${MPGame} APPEND PROPERTY COMPILE_OPTIONS ${OPENJK_VISIBILITY_FLAGS})

set_target_properties(${MPGame} PROPERTIES INCLUDE_DIRECTORIES "${MPGameIncludeDirectories}")
set_target_properties(${MPGame} PROPERTIES PROJECT_LABEL "MP Game Library")
# no libraries used
if(MPGameLibraries)
	target_link_libraries(${MPGame} ${MPGameLibraries})
endif(MPGameLibraries)
