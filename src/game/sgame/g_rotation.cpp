// Public Domain

#include "g_local.h"

#include <array>

namespace {

constexpr int kMinimumBspLength = 144;

bool IsBuggyVanillaVersion( const char *version ) {
	return !Q_strncmp( version, "Q3 1.32 ", 8 )
		|| !Q_strncmp( version, "Q3 1.32b ", 9 )
		|| !Q_strncmp( version, "Q3 1.32c ", 9 );
}

int RequestedRotationIndex() {
	const int requested_index = trap_Cvar_VariableIntegerValue( SV_ROTATION );
	return requested_index == 0 ? 1 : requested_index;
}

void SetRotationIndex( const int rotation_index ) {
	trap_Cvar_Set( SV_ROTATION, va( "%i", rotation_index ) );
}

bool ApplyRotationCvar( char *&cursor, const char *token, const int scope_level, const int current_index, const int requested_index ) {
	if ( token[0] != '$' || token[1] == '\0' ) {
		return false;
	}

	std::array<char, 256> cvar{};
	Q_strncpyz( cvar.data(), token + 1, cvar.size() );

	const char *assignment = COM_ParseSep( &cursor, qfalse );
	if ( assignment[0] != '=' || assignment[1] != '\0' ) {
		COM_ParseWarning( S_COLOR_YELLOW "missing '=' after '%s'", cvar.data() );
		SkipRestOfLine( &cursor );
		return true;
	}

	const char *value = COM_ParseSep( &cursor, qtrue );
	if ( scope_level == 0 || current_index == requested_index ) {
		trap_Cvar_Set( cvar.data(), value );
	}

	SkipTillSeparators( &cursor );
	return true;
}

bool HandleRotationScopeToken( const char *token, int &scope_level, const int current_index ) {
	if ( token[0] == '{' && token[1] == '\0' ) {
		if ( scope_level == 0 && current_index ) {
			++scope_level;
			return true;
		}

		COM_ParseWarning( S_COLOR_YELLOW "unexpected '{'" );
		return true;
	}

	if ( token[0] == '}' && token[1] == '\0' ) {
		if ( scope_level == 1 ) {
			--scope_level;
			return true;
		}

		COM_ParseWarning( S_COLOR_YELLOW "unexpected '}'" );
		return true;
	}

	return false;
}

bool HandleRotationMapToken( const char *token, const int requested_index, int &current_index, std::array<char, 256> &map_name, char *&cursor ) {
	if ( !G_MapExist( token ) ) {
		COM_ParseWarning( S_COLOR_YELLOW "map '%s' doesn't exists", token );
		SkipRestOfLine( &cursor );
		return true;
	}

	++current_index;
	if ( current_index == requested_index ) {
		Q_strncpyz( map_name.data(), token, map_name.size() );
	}

	return true;
}

void LoadSelectedOrCurrentMap( const char *requested_map ) {
	G_LoadMap( requested_map );
}

} // namespace

qboolean G_MapExist( const char *map ) 
{
	fileHandle_t fh;
	int len;

	if ( !map || !*map )
		return qfalse;

	len = trap_FS_FOpenFile( va( "maps/%s.bsp", map ), &fh, FS_READ );

	if ( len < 0 )
		return qfalse;

	trap_FS_FCloseFile( fh );

	return ( len >= kMinimumBspLength ) ? qtrue : qfalse ;
}


void G_LoadMap( const char *map ) 
{
	std::array<char, MAX_CVAR_VALUE_STRING> cmd{};
	std::array<char, 16> ver{};

	trap_Cvar_VariableStringBuffer( "version", ver.data(), ver.size() );
	const int version = IsBuggyVanillaVersion( ver.data() ) ? 0 : 1;

	if ( !map || !*map || !G_MapExist( map ) || !Q_stricmp( map, g_mapname.string ) ) {
		if ( level.time > 12*60*60*1000 || version == 0 || level.denyMapRestart )
			BG_sprintf( cmd.data(), "map \"%s\"\n", g_mapname.string );
		else
			Q_strcpy( cmd.data(), "map_restart 0\n" );
	} else {
		if ( !G_MapExist( map ) ) // required map doesn't exists, reload existing
			BG_sprintf( cmd.data(), "map \"%s\"\n", g_mapname.string );
		else
			BG_sprintf( cmd.data(), "map \"%s\"\n", map );
	}

	trap_SendConsoleCommand( EXEC_APPEND, cmd.data() );
	level.restarted = qtrue;
}


qboolean ParseMapRotation( void ) 
{
	std::array<char, 4096> buf{};
	std::array<char, 256> map{};
	char *s = nullptr;
	fileHandle_t fh;
	int	len;
	int reqIndex; 

	if ( g_gametype.integer == GT_SINGLE_PLAYER || !g_rotation.string[0] )
		return qfalse;

	len = trap_FS_FOpenFile( g_rotation.string, &fh, FS_READ );
	if ( fh == FS_INVALID_HANDLE ) 
	{
		Com_Printf( S_COLOR_YELLOW "%s: map rotation file doesn't exists.\n", g_rotation.string );
		return qfalse;
	}
	if ( len >= sizeof( buf ) ) 
	{
		Com_Printf( S_COLOR_YELLOW "%s: map rotation file is too big.\n", g_rotation.string );
		len = sizeof( buf ) - 1;
	}
	trap_FS_Read( buf.data(), len, fh );
	buf[len] = '\0';
	trap_FS_FCloseFile( fh );
	
	Com_InitSeparators(); // needed for COM_ParseSep()

	reqIndex = RequestedRotationIndex();

	for ( ;; ) {
		int curIndex = 0;
		int scopeLevel = 0;

		COM_BeginParseSession( g_rotation.string );

		s = buf.data();
		map[0] = '\0';

		while ( true ) 
		{
			char *tk = COM_ParseSep( &s, qtrue );
			if ( tk[0] == '\0' ) 
				break;

			if ( ApplyRotationCvar( s, tk, scopeLevel, curIndex, reqIndex ) ) {
				continue;
			}

			if ( HandleRotationScopeToken( tk, scopeLevel, curIndex ) ) {
				continue;
			}

			HandleRotationMapToken( tk, reqIndex, curIndex, map, s );
		}

		if ( curIndex == 0 ) // no maps in rotation file
		{
			Com_Printf( S_COLOR_YELLOW "%s: no maps in rotation file.\n", g_rotation.string );
			trap_Cvar_Set( SV_ROTATION, "1" );
			return qfalse;
		}

		if ( !map[0] ) // map at required index not found?
		{
			if ( reqIndex > 1 ) // try to rescan with lower index
			{
				Com_Printf( S_COLOR_CYAN "%i: map at index %i not found, rescan\n", g_rotation.integer, reqIndex );
				reqIndex = 1;
				continue;
			}
			trap_Cvar_Set( SV_ROTATION, "1" );
			return qfalse;
		}

		reqIndex++;
		if ( reqIndex > curIndex )
			reqIndex = 1;

		SetRotationIndex( reqIndex );
		//trap_Cvar_Set( "g_restarted", "1" );
		LoadSelectedOrCurrentMap( map.data() );

		return qtrue;
	}
}
