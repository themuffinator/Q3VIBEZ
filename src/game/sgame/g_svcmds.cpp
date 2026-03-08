// Copyright (C) 1999-2000 Id Software, Inc.
//

// this file holds commands that can be executed by the server console, but not remote clients

#include "g_local.h"

#include <array>
#include <bit>
#include <cctype>


/*
==============================================================================

PACKET FILTERING
 

You can add or remove addresses from the filter list with:

addip <ip>
removeip <ip>

The ip address is specified in dot format, and you can use '*' to match any value
so you can specify an entire class C network with "addip 192.246.40.*"

Removeip will only remove an address specified exactly the same way.  You cannot addip a subnet, then removeip a single host.

listip
Prints the current list of filters.

g_filterban <0 or 1>

If 1 (the default), then ip addresses matching the current list will be prohibited from entering the game.  This is the default setting.

If 0, then only addresses matching the list will be allowed.  This lets you easily set up a private game, or a game that only allows players from your local network.

TTimo NOTE: for persistence, bans are stored in g_banIPs cvar MAX_CVAR_VALUE_STRING
The size of the cvar string buffer is limiting the banning to around 20 masks
this could be improved by putting some g_banIPs2 g_banIps3 etc. maybe
still, you should rely on PB for banning instead

==============================================================================
*/

typedef struct ipFilter_s
{
	unsigned	mask;
	unsigned	compare;
} ipFilter_t;

#define	MAX_IPFILTERS	1024

static ipFilter_t	ipFilters[MAX_IPFILTERS];
static int			numIPFilters;

namespace {

using IpOctets = std::array<byte, 4>;

static_assert( sizeof( IpOctets ) == sizeof( unsigned ) );

constexpr unsigned InvalidIpFilterCompare = 0xffffffffu;

unsigned PackIpOctets( const IpOctets &octets ) {
	return std::bit_cast<unsigned>( octets );
}

IpOctets UnpackIpOctets( const unsigned value ) {
	return std::bit_cast<IpOctets>( value );
}

bool IsDigitChar( const char value ) {
	return std::isdigit( static_cast<unsigned char>( value ) ) != 0;
}

bool IsFreeIpFilterSlot( const ipFilter_t &filter ) {
	return filter.compare == InvalidIpFilterCompare;
}

const char *EntityTypeName( const int type ) {
	switch ( type ) {
	case ET_GENERAL:
		return "ET_GENERAL          ";
	case ET_PLAYER:
		return "ET_PLAYER           ";
	case ET_ITEM:
		return "ET_ITEM             ";
	case ET_MISSILE:
		return "ET_MISSILE          ";
	case ET_MOVER:
		return "ET_MOVER            ";
	case ET_BEAM:
		return "ET_BEAM             ";
	case ET_PORTAL:
		return "ET_PORTAL           ";
	case ET_SPEAKER:
		return "ET_SPEAKER          ";
	case ET_PUSH_TRIGGER:
		return "ET_PUSH_TRIGGER     ";
	case ET_TELEPORT_TRIGGER:
		return "ET_TELEPORT_TRIGGER ";
	case ET_INVISIBLE:
		return "ET_INVISIBLE        ";
	case ET_GRAPPLE:
		return "ET_GRAPPLE          ";
	default:
		return nullptr;
	}
}

bool IsConnectedClient( const gclient_t &client ) {
	return client.pers.connected != CON_DISCONNECTED;
}

} // namespace

/*
=================
StringToFilter
=================
*/
static qboolean StringToFilter (char *s, ipFilter_t *f)
{
	std::array<char, 128> num{};
	int		i, j;
	IpOctets	b{};
	IpOctets	m{};
	
	for (i=0 ; i<4 ; i++)
	{
		if ( !IsDigitChar( *s ) )
		{
			if (*s == '*') // 'match any'
			{
				// b[i] and m[i] to 0
				s++;
				if (!*s)
					break;
				s++;
				continue;
			}
			G_Printf( "Bad filter address: %s\n", s );
			return qfalse;
		}
		
		j = 0;
		while ( IsDigitChar( *s ) )
		{
			num[j++] = *s++;
		}
		num[j] = '\0';
		b[i] = atoi( num.data() );
		m[i] = 255;

		if (!*s)
			break;
		s++;
	}
	
	f->mask = PackIpOctets( m );
	f->compare = PackIpOctets( b );
	
	return qtrue;
}

/*
=================
UpdateIPBans
=================
*/
static void UpdateIPBans (void)
{
	IpOctets	b{};
	IpOctets	m{};
	int		i,j;
	std::array<char, MAX_CVAR_VALUE_STRING> iplist_final{};
	std::array<char, 64> ip{};

	for (i = 0 ; i < numIPFilters ; i++)
	{
		if ( IsFreeIpFilterSlot( ipFilters[i] ) )
			continue;

		b = UnpackIpOctets( ipFilters[i].compare );
		m = UnpackIpOctets( ipFilters[i].mask );
		ip[0] = '\0';
		for (j = 0 ; j < 4 ; j++)
		{
			if (m[j]!=255)
				Q_strcat( ip.data(), ip.size(), "*" );
			else
				Q_strcat( ip.data(), ip.size(), va("%i", b[j]) );
			Q_strcat( ip.data(), ip.size(), (j<3) ? "." : " " );
		}		
		if ( strlen( iplist_final.data() ) + strlen( ip.data() ) < MAX_CVAR_VALUE_STRING )
		{
			Q_strcat( iplist_final.data(), iplist_final.size(), ip.data() );
		}
		else
		{
			Com_Printf("g_banIPs overflowed at MAX_CVAR_VALUE_STRING\n");
			break;
		}
	}

	trap_Cvar_Set( "g_banIPs", iplist_final.data() );
}

/*
=================
G_FilterPacket
=================
*/
qboolean G_FilterPacket (char *from)
{
	int		i;
	unsigned	in;
	IpOctets m{};
	char *p;

	i = 0;
	p = from;
	while (*p && i < 4) {
		while ( IsDigitChar( *p ) ) {
			m[i] = m[i]*10 + (*p - '0');
			p++;
		}
		if (!*p || *p == ':')
			break;
		i++, p++;
	}
	
	in = PackIpOctets( m );

	for (i=0 ; i<numIPFilters ; i++)
		if ( (in & ipFilters[i].mask) == ipFilters[i].compare)
			return g_filterBan.integer != 0;

	return g_filterBan.integer == 0;
}

/*
=================
AddIP
=================
*/
static void AddIP( char *str )
{
	int		i;

	for (i = 0 ; i < numIPFilters ; i++)
		if ( IsFreeIpFilterSlot( ipFilters[i] ) )
			break;		// free spot
	if (i == numIPFilters)
	{
		if (numIPFilters == MAX_IPFILTERS)
		{
			G_Printf ("IP filter list is full\n");
			return;
		}
		numIPFilters++;
	}
	
	if (!StringToFilter (str, &ipFilters[i]))
		ipFilters[i].compare = InvalidIpFilterCompare;

	UpdateIPBans();
}

/*
=================
G_ProcessIPBans
=================
*/
void G_ProcessIPBans(void) 
{
	char *s, *t;
	std::array<char, MAX_CVAR_VALUE_STRING> str{};

	Q_strncpyz( str.data(), g_banIPs.string, str.size() );

	for (t = s = str.data(); *t; /* */ ) {
		s = strchr(s, ' ');
		if (!s)
			break;
		while (*s == ' ')
			*s++ = 0;
		if (*t)
			AddIP( t );
		t = s;
	}
}


/*
=================
Svcmd_AddIP_f
=================
*/
void Svcmd_AddIP_f (void)
{
	std::array<char, MAX_TOKEN_CHARS> str{};

	if ( trap_Argc() < 2 ) {
		G_Printf("Usage:  addip <ip-mask>\n");
		return;
	}

	trap_Argv( 1, str.data(), str.size() );

	AddIP( str.data() );

}

/*
=================
Svcmd_RemoveIP_f
=================
*/
void Svcmd_RemoveIP_f (void)
{
	ipFilter_t	f;
	int			i;
	std::array<char, MAX_TOKEN_CHARS> str{};

	if ( trap_Argc() < 2 ) {
		G_Printf("Usage:  sv removeip <ip-mask>\n");
		return;
	}

	trap_Argv( 1, str.data(), str.size() );

	if (!StringToFilter (str.data(), &f))
		return;

	for (i=0 ; i<numIPFilters ; i++) {
		if (ipFilters[i].mask == f.mask	&&
			ipFilters[i].compare == f.compare) {
			ipFilters[i].compare = InvalidIpFilterCompare;
			G_Printf ("Removed.\n");

			UpdateIPBans();
			return;
		}
	}

	G_Printf ( "Didn't find %s.\n", str.data() );
}

/*
===================
Svcmd_EntityList_f
===================
*/
void	Svcmd_EntityList_f (void) {
	int			e;
	gentity_t		*check;

	check = g_entities;
	for (e = 0; e < level.num_entities ; e++, check++) {
		if ( !check->inuse ) {
			continue;
		}
		G_Printf("%3i:", e);
		if ( const char *type_name = EntityTypeName( check->s.eType ) ) {
			G_Printf( "%s", type_name );
		} else {
			G_Printf("%3i                 ", check->s.eType);
		}

		if ( check->classname ) {
			G_Printf("%s", check->classname);
		}
		G_Printf("\n");
	}
}

gclient_t	*ClientForString( const char *s ) {
	gclient_t	*cl;
	int			i;
	int			idnum;

	// numeric values are just slot numbers
	if ( IsDigitChar( s[0] ) ) {
		idnum = atoi( s );
		if ( idnum < 0 || idnum >= level.maxclients ) {
			Com_Printf( "Bad client slot: %i\n", idnum );
			return NULL;
		}

		cl = &level.clients[idnum];
		if ( !IsConnectedClient( *cl ) ) {
			G_Printf( "Client %i is not connected\n", idnum );
			return NULL;
		}
		return cl;
	}

	// check for a name match
	for ( i=0 ; i < level.maxclients ; i++ ) {
		cl = &level.clients[i];
		if ( !IsConnectedClient( *cl ) ) {
			continue;
		}
		if ( !Q_stricmp( cl->pers.netname, s ) ) {
			return cl;
		}
	}

	G_Printf( "User %s is not on the server\n", s );

	return NULL;
}

/*
===================
Svcmd_ForceTeam_f

forceteam <player> <team>
===================
*/
void	Svcmd_ForceTeam_f( void ) {
	gclient_t	*cl;
	std::array<char, MAX_TOKEN_CHARS> str{};

	if ( trap_Argc() < 3 ) {
		G_Printf("Usage: forceteam <player> <team>\n");
		return;
	}

	// find the player
	trap_Argv( 1, str.data(), str.size() );
	cl = ClientForString( str.data() );
	if ( !cl ) {
		return;
	}

	// set the team
	trap_Argv( 2, str.data(), str.size() );
	SetTeam( &g_entities[cl - level.clients], str.data() );
}


void Svcmd_Rotate_f( void ) {
	std::array<char, MAX_TOKEN_CHARS> str{};

	if ( trap_Argc() >= 2 ) {
		trap_Argv( 1, str.data(), str.size() );
		if ( atoi( str.data() ) > 0 ) {
			trap_Cvar_Set( SV_ROTATION, str.data() );
		}
	}

	if ( !ParseMapRotation() ) {
		std::array<char, MAX_CVAR_VALUE_STRING> val{};

		trap_Cvar_VariableStringBuffer( "nextmap", val.data(), val.size() );

		if ( !val[0] || !Q_stricmpn( val.data(), "map_restart ", 12 ) )
			G_LoadMap( NULL );
		else
			trap_SendConsoleCommand( EXEC_APPEND, "vstr nextmap\n" );
	}
}


char	*ConcatArgs( int start );

/*
=================
ConsoleCommand

=================
*/
qboolean	ConsoleCommand( void ) {
	std::array<char, MAX_TOKEN_CHARS> cmd{};

	trap_Argv( 0, cmd.data(), cmd.size() );

	if ( Q_stricmp (cmd.data(), "entitylist") == 0 ) {
		Svcmd_EntityList_f();
		return qtrue;
	}

	if ( Q_stricmp (cmd.data(), "forceteam") == 0 ) {
		Svcmd_ForceTeam_f();
		return qtrue;
	}

	if (Q_stricmp (cmd.data(), "game_memory") == 0) {
		Svcmd_GameMem_f();
		return qtrue;
	}

	if (Q_stricmp (cmd.data(), "addbot") == 0) {
		Svcmd_AddBot_f();
		return qtrue;
	}

	if (Q_stricmp (cmd.data(), "botlist") == 0) {
		Svcmd_BotList_f();
		return qtrue;
	}

	if (Q_stricmp (cmd.data(), "abort_podium") == 0) {
		Svcmd_AbortPodium_f();
		return qtrue;
	}

	if (Q_stricmp (cmd.data(), "addip") == 0) {
		Svcmd_AddIP_f();
		return qtrue;
	}

	if (Q_stricmp (cmd.data(), "removeip") == 0) {
		Svcmd_RemoveIP_f();
		return qtrue;
	}

	if (Q_stricmp (cmd.data(), "listip") == 0) {
		trap_SendConsoleCommand( EXEC_NOW, "g_banIPs\n" );
		return qtrue;
	}

	if (Q_stricmp (cmd.data(), "rotate") == 0) {
		Svcmd_Rotate_f();
		return qtrue;
	}

	if (g_dedicated.integer) {
		if (Q_stricmp (cmd.data(), "say") == 0) {
			G_BroadcastServerCommand( -1, va("print \"server: %s\"", ConcatArgs(1) ) );
			return qtrue;
		}
		// everything else will also be printed as a say command
		G_BroadcastServerCommand( -1, va("print \"server: %s\"", ConcatArgs(0) ) );
		return qtrue;
	}

	return qfalse;
}
