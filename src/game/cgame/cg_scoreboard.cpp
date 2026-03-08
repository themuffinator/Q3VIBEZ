// Copyright (C) 1999-2000 Id Software, Inc.
//
// cg_scoreboard -- draw the scoreboard on top of the game screen
#include "cg_local.h"

#include <algorithm>
#include <array>
#include <span>

#define	SCOREBOARD_X		(0)

#define SB_HEADER			86
#define SB_TOP				(SB_HEADER+32)

// Where the status bar starts, so we don't overwrite it
#define SB_STATUSBAR		420

#define SB_NORMAL_HEIGHT	40
#define SB_INTER_HEIGHT		16 // interleaved height

#define SB_MAXCLIENTS_NORMAL  ((SB_STATUSBAR - SB_TOP) / SB_NORMAL_HEIGHT)
#define SB_MAXCLIENTS_INTER   ((SB_STATUSBAR - SB_TOP) / SB_INTER_HEIGHT - 1)

// Used when interleaved



#define SB_LEFT_BOTICON_X	(SCOREBOARD_X+0)
#define SB_LEFT_HEAD_X		(SCOREBOARD_X+32)
#define SB_RIGHT_BOTICON_X	(SCOREBOARD_X+64)
#define SB_RIGHT_HEAD_X		(SCOREBOARD_X+96)
// Normal
#define SB_BOTICON_X		(SCOREBOARD_X+32)
#define SB_HEAD_X			(SCOREBOARD_X+64)

#define SB_SCORELINE_X		112

#define SB_RATING_WIDTH	    (6 * BIGCHAR_WIDTH) // width 6
#define SB_SCORE_X			(SB_SCORELINE_X + BIGCHAR_WIDTH) // width 6
#define SB_RATING_X			(SB_SCORELINE_X + 6 * BIGCHAR_WIDTH) // width 6
#define SB_PING_X			(SB_SCORELINE_X + 12 * BIGCHAR_WIDTH + 8) // width 5
#define SB_TIME_X			(SB_SCORELINE_X + 17 * BIGCHAR_WIDTH + 8) // width 5
#define SB_NAME_X			(SB_SCORELINE_X + 22 * BIGCHAR_WIDTH) // width 15

// The new and improved score board
//
// In cases where the number of clients is high, the score board heads are interleaved
// here's the layout

//
//	0   32   80  112  144   240  320  400   <-- pixel position
//  bot head bot head score ping time name
//  
//  wins/losses are drawn on bot icon now

static qboolean localClient; // true if local client has been displayed

namespace {

[[nodiscard]] auto ScoreEntries() noexcept -> std::span<score_t> {
	return { cg.scores.data(), static_cast<std::size_t>( cg.numScores ) };
}

[[nodiscard]] bool CursorOverScore( const score_t &score ) noexcept {
	return cgs.cursorX >= score.minx && cgs.cursorX <= score.maxx
		&& cgs.cursorY >= score.miny && cgs.cursorY <= score.maxy;
}

void ResetRenderColor() {
	trap_R_SetColor( nullptr );
}

void FormatClientScoreLine( std::span<char> buffer, const score_t &score, const clientInfo_t &clientInfo ) {
	if ( score.ping == -1 ) {
		BG_sprintf( buffer.data(), " connecting" );
		return;
	}

	if ( clientInfo.team == TEAM_SPECTATOR ) {
		BG_sprintf( buffer.data(), " SPECT %3i %4i", score.ping, score.time );
		return;
	}

	BG_sprintf( buffer.data(), "%5i %4i %4i", score.score, score.ping, score.time );
}

void SetLocalClientHighlightColor( vec4_t highlight, const float fade ) {
	int rank = -1;

	if ( cg.snap->ps.persistant[PERS_TEAM] != TEAM_SPECTATOR && cgs.gametype < GT_TEAM ) {
		rank = cg.snap->ps.persistant[PERS_RANK] & ~RANK_TIED_FLAG;
	}

	if ( rank == 0 ) {
		highlight[2] = 0.7f;
	} else if ( rank == 1 ) {
		highlight[0] = 0.7f;
	} else if ( rank == 2 ) {
		highlight[0] = 0.7f;
		highlight[1] = 0.7f;
	} else {
		highlight[0] = 0.7f;
		highlight[1] = 0.7f;
		highlight[2] = 0.7f;
	}

	highlight[3] = fade * 0.7f;
}

void BuildKillerMessage( std::span<char> buffer ) {
	Com_sprintf( buffer.data(), static_cast<int>( buffer.size() ), "Fragged by %s", cg.killerName );
}

void BuildScoreboardStatusMessage( std::span<char> buffer ) {
	if ( cgs.gametype < GT_TEAM ) {
		Com_sprintf(
			buffer.data(),
			static_cast<int>( buffer.size() ),
			"%s place with %i",
			CG_PlaceString( cg.snap->ps.persistant[PERS_RANK] + 1 ),
			cg.snap->ps.persistant[PERS_SCORE] );
		return;
	}

	if ( cg.teamScores[0] == cg.teamScores[1] ) {
		Com_sprintf( buffer.data(), static_cast<int>( buffer.size() ), "Teams are tied at %i", cg.teamScores[0] );
	} else if ( cg.teamScores[0] >= cg.teamScores[1] ) {
		Com_sprintf( buffer.data(), static_cast<int>( buffer.size() ), "Red leads %i to %i", cg.teamScores[0], cg.teamScores[1] );
	} else {
		Com_sprintf( buffer.data(), static_cast<int>( buffer.size() ), "Blue leads %i to %i", cg.teamScores[1], cg.teamScores[0] );
	}
}

void FormatInteger( std::span<char> buffer, const int value ) {
	Com_sprintf( buffer.data(), static_cast<int>( buffer.size() ), "%i", value );
}

void FormatMatchTime( std::span<char> buffer, const int minutes, const int seconds ) {
	Com_sprintf( buffer.data(), static_cast<int>( buffer.size() ), "%i:%02i", minutes, seconds );
}

} // namespace


/*
=================
CG_DrawScoreboard
=================
*/
static void CG_DrawClientScore( int y, score_t &score, const float *color, float fade, qboolean largeFormat ) {
	std::array<char, 64> string{};
	vec3_t	headAngles;
	clientInfo_t	*ci;
	int iconx, headx;
	vec4_t c;

	if ( score.client < 0 || score.client >= cgs.maxclients ) {
		Com_Printf( "Bad score->client: %i\n", score.client );
		return;
	}
	
	ci = &cgs.clientinfo[score.client];
	if ( !ci->infoValid )
		return;

	iconx = SB_BOTICON_X + (SB_RATING_WIDTH / 2);
	headx = SB_HEAD_X + (SB_RATING_WIDTH / 2);

	ResetRenderColor();

	// draw the handicap or bot skill marker (unless player has flag)
	if ( ci->powerups & ( 1 << PW_NEUTRALFLAG ) ) {
		if( largeFormat ) {
			CG_DrawFlagModel( iconx, y - ( 32 - BIGCHAR_HEIGHT ) / 2, 32, 32, TEAM_FREE, qfalse );
		}
		else {
			CG_DrawFlagModel( iconx, y, 16, 16, TEAM_FREE, qfalse );
		}
	} else if ( ci->powerups & ( 1 << PW_REDFLAG ) ) {
		if( largeFormat ) {
			CG_DrawFlagModel( iconx, y - ( 32 - BIGCHAR_HEIGHT ) / 2, 32, 32, TEAM_RED, qfalse );
		}
		else {
			CG_DrawFlagModel( iconx, y, 16, 16, TEAM_RED, qfalse );
		}
	} else if ( ci->powerups & ( 1 << PW_BLUEFLAG ) ) {
		if( largeFormat ) {
			CG_DrawFlagModel( iconx, y - ( 32 - BIGCHAR_HEIGHT ) / 2, 32, 32, TEAM_BLUE, qfalse );
		}
		else {
			CG_DrawFlagModel( iconx, y, 16, 16, TEAM_BLUE, qfalse );
		}
	} else {
		if ( ci->botSkill > 0 && ci->botSkill <= 5 ) {
			if ( cg_drawIcons.integer ) {
				if( largeFormat ) {
					CG_DrawPic( iconx, y - ( 32 - BIGCHAR_HEIGHT ) / 2, 32, 32, cgs.media.botSkillShaders[ ci->botSkill - 1 ] );
				}
				else {
					CG_DrawPic( iconx, y, 16, 16, cgs.media.botSkillShaders[ ci->botSkill - 1 ] );
				}
			}
		} else if ( ci->handicap < 100 ) {
			BG_sprintf( string.data(), "%i", ci->handicap );
			if ( cgs.gametype == GT_TOURNAMENT )
				CG_DrawString( iconx, y - SMALLCHAR_HEIGHT/2, string.data(), color, SMALLCHAR_WIDTH, SMALLCHAR_HEIGHT, 0, DS_FORCE_COLOR );
			else
				CG_DrawString( iconx, y, string.data(), color, SMALLCHAR_WIDTH, SMALLCHAR_HEIGHT, 0, DS_FORCE_COLOR );
		}

		// draw the wins / losses
		if ( cgs.gametype == GT_TOURNAMENT ) {
			BG_sprintf( string.data(), "%i/%i", ci->wins, ci->losses );
			if( ci->handicap < 100 && !ci->botSkill ) {
				CG_DrawString( iconx, y + SMALLCHAR_HEIGHT/2, string.data(), color, SMALLCHAR_WIDTH, SMALLCHAR_HEIGHT, 0, DS_FORCE_COLOR );
			} else {
				CG_DrawString( iconx, y, string.data(), color, SMALLCHAR_WIDTH, SMALLCHAR_HEIGHT, 0, DS_FORCE_COLOR );
			}
		}
	}

	// draw the face
	VectorClear( headAngles );
	headAngles[YAW] = 180;
	if( largeFormat ) {
		CG_DrawHead( headx, y - ( ICON_SIZE - BIGCHAR_HEIGHT ) / 2, ICON_SIZE, ICON_SIZE, 
			score.client, headAngles );
	}
	else {
		CG_DrawHead( headx, y, 16, 16, score.client, headAngles );
	}

#ifdef MISSIONPACK
	// draw the team task
	if ( ci->teamTask != TEAMTASK_NONE ) {
		if ( ci->teamTask == TEAMTASK_OFFENSE ) {
			CG_DrawPic( headx + 48, y, 16, 16, cgs.media.assaultShader );
		}
		else if ( ci->teamTask == TEAMTASK_DEFENSE ) {
			CG_DrawPic( headx + 48, y, 16, 16, cgs.media.defendShader );
		}
	}
#endif
	// draw the score line
	FormatClientScoreLine( string, score, *ci );

	// highlight your position
	if ( score.client == cg.snap->ps.clientNum ) {
		localClient = qtrue;
		vec4_t hcolor{};
		SetLocalClientHighlightColor( hcolor, fade );
		CG_FillRect( SB_SCORELINE_X + BIGCHAR_WIDTH + (SB_RATING_WIDTH / 2), y, 
			640 - SB_SCORELINE_X - BIGCHAR_WIDTH - (SB_RATING_WIDTH/2),
			BIGCHAR_HEIGHT+1, hcolor );
	}

	VectorSet( c, 1, 1, 1 ); c[3] = fade;
	// score
	CG_DrawString( SB_SCORELINE_X + (SB_RATING_WIDTH / 2), y, string.data(), c, BIGCHAR_WIDTH, BIGCHAR_HEIGHT, 0, DS_SHADOW );
	// name
	CG_DrawString( SB_SCORELINE_X + (SB_RATING_WIDTH / 2) + BIGCHAR_WIDTH*16, y, ci->name, c, BIGCHAR_WIDTH, BIGCHAR_HEIGHT, 0, DS_SHADOW | DS_PROPORTIONAL );

	// add the "ready" marker for intermission exiting
	if ( cg.snap->ps.stats[ STAT_CLIENTS_READY ] & ( 1 << score.client ) ) {
		CG_DrawString( iconx, y, "READY", color, BIGCHAR_WIDTH, BIGCHAR_HEIGHT, 0, DS_SHADOW | DS_FORCE_COLOR );
	}

	// set bounds for scoreboard clicks
	score.minx = SB_SCORELINE_X;
	score.maxx = SCREEN_WIDTH - 8;
	score.miny = y;
	score.maxy = y + BIGCHAR_HEIGHT;
	if ( largeFormat )
	{
		score.miny -= ( ICON_SIZE - BIGCHAR_HEIGHT ) / 2;
		score.maxy += ( ICON_SIZE - BIGCHAR_HEIGHT ) / 2;
	}
}


/*
=================
CG_ScoreboardClick
=================
*/
void CG_ScoreboardClick( void )
{
	if ( cg.intermissionStarted )
		return;

	if ( !cg.snap || cg.snap->ps.pm_type == PM_INTERMISSION )
		return;

	for ( score_t &score : ScoreEntries() ) {
		if ( score.team >= TEAM_SPECTATOR ) {
			continue;
		}
		if ( !CursorOverScore( score ) ) {
			continue;
		}
		if ( !cgs.clientinfo[ score.client ].infoValid ) {
			continue;
		}

		if ( !cg.demoPlayback ) {
			trap_SendClientCommand( va( "follow %i", score.client ) );
		}
	}
}


/*
=================
CG_TeamScoreboard
=================
*/
static int CG_TeamScoreboard( int y, team_t team, float fade, int maxClients, int lineHeight ) {
	float	color[4] = { 1.0f, 1.0f, 1.0f, fade };
	int		count = 0;

	for ( score_t &score : ScoreEntries() ) {
		clientInfo_t &ci = cgs.clientinfo[ score.client ];

		if ( count >= maxClients ) {
			break;
		}
		if ( team != ci.team || !ci.infoValid ) {
			continue;
		}

		CG_DrawClientScore( y + lineHeight * count, score, color, fade, lineHeight == SB_NORMAL_HEIGHT );
		++count;
	}

	return count;
}

/*
=================
CG_DrawScoreboard

Draw the normal in-game scoreboard
=================
*/
qboolean CG_DrawOldScoreboard( void ) {
	int		y, n1, n2;
	float	fade;
	float	*fadeColor;
	std::array<char, 128> statusText{};
	std::array<char, 128> killerText{};
	int maxClients;
	int lineHeight;
	int topBorderSize, bottomBorderSize;

	// don't draw anything if the menu or console is up
	if ( cg_paused.integer ) {
		cg.deferredPlayerLoading = 0;
		return qfalse;
	}

	if ( cgs.gametype == GT_SINGLE_PLAYER && cg.predictedPlayerState.pm_type == PM_INTERMISSION ) {
		cg.deferredPlayerLoading = 0;
		return qfalse;
	}

	// don't draw scoreboard during death while warmup up
	if ( cg.warmup && !cg.showScores ) {
		return qfalse;
	}

	if ( cg.showScores || cg.predictedPlayerState.pm_type == PM_DEAD ||
		 cg.predictedPlayerState.pm_type == PM_INTERMISSION ) {
		fade = 1.0;
		fadeColor = colorWhite;
	} else {
		fadeColor = CG_FadeColor( cg.scoreFadeTime, FADE_TIME );
		
		if ( !fadeColor ) {
			// next time scoreboard comes up, don't print killer
			cg.deferredPlayerLoading = 0;
			cg.killerName[0] = 0;
			return qfalse;
		}
		fade = fadeColor[3];
	}

	// fragged by ... line
	if ( cg.killerName[0] ) {
		BuildKillerMessage( killerText );
		CG_DrawString( 320, 40, killerText.data(), fadeColor, BIGCHAR_WIDTH, BIGCHAR_HEIGHT, 0, DS_SHADOW | DS_CENTER | DS_PROPORTIONAL );
	}

	// current rank
	if ( cgs.gametype < GT_TEAM) {
		if (cg.snap->ps.persistant[PERS_TEAM] != TEAM_SPECTATOR ) {
			BuildScoreboardStatusMessage( statusText );
			CG_DrawString( 320, 60, statusText.data(), fadeColor, BIGCHAR_WIDTH, BIGCHAR_HEIGHT, 0, DS_SHADOW | DS_CENTER | DS_PROPORTIONAL );
		}
	} else {
		BuildScoreboardStatusMessage( statusText );
		CG_DrawString( 320, 60, statusText.data(), fadeColor, BIGCHAR_WIDTH, BIGCHAR_HEIGHT, 0, DS_SHADOW | DS_CENTER | DS_PROPORTIONAL );
	}

	// scoreboard
	y = SB_HEADER;

	CG_DrawPic( SB_SCORE_X + (SB_RATING_WIDTH / 2), y, 64, 32, cgs.media.scoreboardScore );
	CG_DrawPic( SB_PING_X - (SB_RATING_WIDTH / 2), y, 64, 32, cgs.media.scoreboardPing );
	CG_DrawPic( SB_TIME_X - (SB_RATING_WIDTH / 2), y, 64, 32, cgs.media.scoreboardTime );
	CG_DrawPic( SB_NAME_X - (SB_RATING_WIDTH / 2), y, 64, 32, cgs.media.scoreboardName );

	y = SB_TOP;

	// If there are more than SB_MAXCLIENTS_NORMAL, use the interleaved scores
	if ( cg.numScores > SB_MAXCLIENTS_NORMAL ) {
		maxClients = SB_MAXCLIENTS_INTER;
		lineHeight = SB_INTER_HEIGHT;
		topBorderSize = 8;
		bottomBorderSize = 16;
	} else {
		maxClients = SB_MAXCLIENTS_NORMAL;
		lineHeight = SB_NORMAL_HEIGHT;
		topBorderSize = 16;
		bottomBorderSize = 16;
	}

	localClient = qfalse;

	if ( cgs.gametype >= GT_TEAM ) {
		//
		// teamplay scoreboard
		//
		y += lineHeight/2;

		if ( cg.teamScores[0] >= cg.teamScores[1] ) {
			n1 = CG_TeamScoreboard( y, TEAM_RED, fade, maxClients, lineHeight );
			CG_DrawTeamBackground( 0, y - topBorderSize, 640, n1 * lineHeight + bottomBorderSize, 0.33f, TEAM_RED );
			y += (n1 * lineHeight) + BIGCHAR_HEIGHT;
			maxClients -= n1;
			n2 = CG_TeamScoreboard( y, TEAM_BLUE, fade, maxClients, lineHeight );
			CG_DrawTeamBackground( 0, y - topBorderSize, 640, n2 * lineHeight + bottomBorderSize, 0.33f, TEAM_BLUE );
			y += (n2 * lineHeight) + BIGCHAR_HEIGHT;
			maxClients -= n2;
		} else {
			n1 = CG_TeamScoreboard( y, TEAM_BLUE, fade, maxClients, lineHeight );
			CG_DrawTeamBackground( 0, y - topBorderSize, 640, n1 * lineHeight + bottomBorderSize, 0.33f, TEAM_BLUE );
			y += (n1 * lineHeight) + BIGCHAR_HEIGHT;
			maxClients -= n1;
			n2 = CG_TeamScoreboard( y, TEAM_RED, fade, maxClients, lineHeight );
			CG_DrawTeamBackground( 0, y - topBorderSize, 640, n2 * lineHeight + bottomBorderSize, 0.33f, TEAM_RED );
			y += (n2 * lineHeight) + BIGCHAR_HEIGHT;
			maxClients -= n2;
		}
		n1 = CG_TeamScoreboard( y, TEAM_SPECTATOR, fade, maxClients, lineHeight );
		y += (n1 * lineHeight) + BIGCHAR_HEIGHT;

	} else {
		//
		// free for all scoreboard
		//
		n1 = CG_TeamScoreboard( y, TEAM_FREE, fade, maxClients, lineHeight );
		y += (n1 * lineHeight) + BIGCHAR_HEIGHT;
		n2 = CG_TeamScoreboard( y, TEAM_SPECTATOR, fade, maxClients - n1, lineHeight );
		y += (n2 * lineHeight) + BIGCHAR_HEIGHT;
	}

	if (!localClient) {
		const auto scores = ScoreEntries();
		const auto scoreIt = std::find_if( scores.begin(), scores.end(), []( const score_t &score ) {
			return score.client == cg.snap->ps.clientNum;
		} );

		// draw local client at the bottom
		if ( scoreIt != scores.end() ) {
			CG_DrawClientScore( y, *scoreIt, fadeColor, fade, lineHeight == SB_NORMAL_HEIGHT );
		}
	}

	// load any models that have been deferred
	if ( ++cg.deferredPlayerLoading > 10 ) {
		CG_LoadDeferredPlayers();
	}

	return qtrue;
}

//================================================================================


/*
=================
CG_DrawTourneyScoreboard

Draw the oversize scoreboard for tournements
=================
*/
void CG_DrawOldTourneyScoreboard( void ) {
	const char		*s;
	vec4_t			color{ 0.2f, 0.2f, 0.2f, 1.0f };
	int				min, sec;
	int				y;
	std::array<char, 16> valueText{};

	// request more scores regularly
	if ( cg.scoresRequestTime + 2000 < cg.time ) {
		cg.scoresRequestTime = cg.time;
		trap_SendClientCommand( "score" );
	}

	// draw the dialog background
	CG_FillScreen( color );

	// print the mesage of the day
	s = CG_ConfigString( CS_MOTD );
	if ( !s[0] ) {
		s = "Scoreboard";
	}

	// print optional title
	CG_DrawString( 320, 8, s, colorWhite, GIANT_WIDTH, GIANT_HEIGHT, 0, DS_SHADOW | DS_FORCE_COLOR | DS_CENTER | DS_PROPORTIONAL );

	// print server time
	sec = cg.time / 1000;
	min = sec / 60;
	sec %= 60;

	FormatMatchTime( valueText, min, sec );

	CG_DrawString( 320, 64, valueText.data(), colorWhite, GIANT_WIDTH, GIANT_HEIGHT, 0, DS_SHADOW | DS_FORCE_COLOR | DS_CENTER | DS_PROPORTIONAL );

	// print the two scores

	y = 160;
	if ( cgs.gametype >= GT_TEAM ) {
		//
		// teamplay scoreboard
		//
		CG_DrawString( 8, y, "Red Team", colorWhite, GIANT_WIDTH, GIANT_HEIGHT, 0, DS_SHADOW );
		FormatInteger( valueText, cg.teamScores[0] );
		CG_DrawString( 632, y, valueText.data(), colorWhite, GIANT_WIDTH, GIANT_HEIGHT, 0, DS_SHADOW | DS_RIGHT );
		
		y += 64;

		CG_DrawString( 8, y, "Blue Team", colorWhite, GIANT_WIDTH, GIANT_HEIGHT, 0, DS_SHADOW );
		FormatInteger( valueText, cg.teamScores[1] );
		CG_DrawString( 632, y, valueText.data(), colorWhite, GIANT_WIDTH, GIANT_HEIGHT, 0, DS_SHADOW | DS_RIGHT );

	} else {
		//
		// free for all scoreboard
		//
		for ( clientInfo_t &clientInfo : cgs.clientinfo ) {
			if ( !clientInfo.infoValid ) {
				continue;
			}
			if ( clientInfo.team != TEAM_FREE ) {
				continue;
			}

			CG_DrawString( 8, y, clientInfo.name, colorWhite, GIANT_WIDTH, GIANT_HEIGHT, 0, DS_SHADOW | DS_FORCE_COLOR | DS_PROPORTIONAL );
			FormatInteger( valueText, clientInfo.score );
			CG_DrawString( 632, y, valueText.data(), colorWhite, GIANT_WIDTH, GIANT_HEIGHT, 0, DS_SHADOW | DS_RIGHT );
			y += 64;
		}
	}
}
