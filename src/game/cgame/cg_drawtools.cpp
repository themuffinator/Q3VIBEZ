// Copyright (C) 1999-2000 Id Software, Inc.
//
// cg_drawtools.c -- helper functions called by cg_draw, cg_scoreboard, cg_info, etc
#include "cg_local.h"

#include <algorithm>
#include <array>

namespace {

void ResetRenderColor() {
	trap_R_SetColor( nullptr );
}

[[nodiscard]] bool IsQColorSequence( const byte *text ) noexcept {
	return *text == Q_COLOR_ESCAPE && text[1] != '\0' && text[1] != '^';
}

[[nodiscard]] float *EvaluateFadeColor( const int startMsec, const int totalMsec, const int fadeMsec, vec4_t color ) noexcept {
	if ( startMsec == 0 ) {
		return nullptr;
	}

	const int elapsedMsec = cg.time - startMsec;
	if ( elapsedMsec >= totalMsec ) {
		return nullptr;
	}

	color[3] = totalMsec - elapsedMsec < fadeMsec ? ( totalMsec - elapsedMsec ) * 1.0f / static_cast<float>( fadeMsec ) : 1.0f;
	color[0] = color[1] = color[2] = 1.0f;
	return color;
}

[[nodiscard]] float NormalizedHealthChannel( const int health, const int minHealth, const int maxHealth ) noexcept {
	if ( health <= minHealth ) {
		return 0.0f;
	}
	if ( health >= maxHealth ) {
		return 1.0f;
	}
	return static_cast<float>( health - minHealth ) / static_cast<float>( maxHealth - minHealth );
}

} // namespace

/*
================
CG_AdjustFrom640

Adjusted for resolution and screen aspect ratio
================
*/
void CG_AdjustFrom640( float *x, float *y, float *w, float *h ) 
{
	// scale for screen sizes
	*x = *x * cgs.screenXScale + cgs.screenXBias;
	*y = *y * cgs.screenYScale + cgs.screenYBias;
	*w *= cgs.screenXScale;
	*h *= cgs.screenYScale;
}


/*
================
CG_FillRect

Coordinates are 640*480 virtual values
=================
*/
void CG_FillRect( float x, float y, float width, float height, const float *color ) {
	trap_R_SetColor( color );

	CG_AdjustFrom640( &x, &y, &width, &height );
	trap_R_DrawStretchPic( x, y, width, height, 0, 0, 0, 0, cgs.media.whiteShader );

	ResetRenderColor();
}


/*
================
CG_FillScreen
================
*/
void CG_FillScreen( const float *color )
{
	trap_R_SetColor( color );
	trap_R_DrawStretchPic( 0, 0, cgs.glconfig.vidWidth, cgs.glconfig.vidHeight, 0, 0, 0, 0, cgs.media.whiteShader );
	ResetRenderColor();
}


/*
================
CG_DrawSides

Coords are virtual 640x480
================
*/
void CG_DrawSides(float x, float y, float w, float h, float size) {
	CG_AdjustFrom640( &x, &y, &w, &h );
	size *= cgs.screenXScale;
	trap_R_DrawStretchPic( x, y, size, h, 0, 0, 0, 0, cgs.media.whiteShader );
	trap_R_DrawStretchPic( x + w - size, y, size, h, 0, 0, 0, 0, cgs.media.whiteShader );
}


void CG_DrawTopBottom(float x, float y, float w, float h, float size) {
	CG_AdjustFrom640( &x, &y, &w, &h );
	size *= cgs.screenYScale;
	trap_R_DrawStretchPic( x, y, w, size, 0, 0, 0, 0, cgs.media.whiteShader );
	trap_R_DrawStretchPic( x, y + h - size, w, size, 0, 0, 0, 0, cgs.media.whiteShader );
}


/*
================
UI_DrawRect

Coordinates are 640*480 virtual values
=================
*/
void CG_DrawRect( float x, float y, float width, float height, float size, const float *color ) {
	trap_R_SetColor( color );

	CG_DrawTopBottom(x, y, width, height, size);
	CG_DrawSides(x, y, width, height, size);

	ResetRenderColor();
}


/*
================
CG_DrawPic

Coordinates are 640*480 virtual values
=================
*/
void CG_DrawPic( float x, float y, float width, float height, qhandle_t hShader ) {
	CG_AdjustFrom640( &x, &y, &width, &height );
	trap_R_DrawStretchPic( x, y, width, height, 0, 0, 1, 1, hShader );
}


/*
===============
CG_DrawChar

Coordinates and size in 640*480 virtual screen size
===============
*/
static void CG_DrawChar( int x, int y, int width, int height, int ch ) {
	int row, col;
	float frow, fcol;
	float size;
	float	ax, ay, aw, ah;

	ch &= 255;

	if ( ch == ' ' ) {
		return;
	}

	ax = x;
	ay = y;
	aw = width;
	ah = height;
	CG_AdjustFrom640( &ax, &ay, &aw, &ah );

	row = ch>>4;
	col = ch&15;

	frow = row*0.0625;
	fcol = col*0.0625;
	size = 0.0625;

	trap_R_DrawStretchPic( ax, ay, aw, ah,
					   fcol, frow, 
					   fcol + size, frow + size, 
					   cgs.media.charsetShader );
}


/*
==================
CG_DrawStringExt

Draws a multi-colored string with a drop shadow, optionally forcing
to a fixed color.

Coordinates are at 640 by 480 virtual resolution
==================
*/
void CG_DrawStringExt( int x, int y, const char *string, const float *setColor, 
		qboolean forceColor, qboolean shadow, int charWidth, int charHeight, int maxChars ) {
	vec4_t		color;
	const char	*s;
	int			xx;
	int			cnt;

	if (maxChars <= 0)
		maxChars = 32767; // do them all!

	// draw the drop shadow
	if (shadow) {
		color[0] = color[1] = color[2] = 0;
		color[3] = setColor[3];
		trap_R_SetColor( color );
		s = string;
		xx = x;
		cnt = 0;
		while ( *s && cnt < maxChars) {
			if ( Q_IsColorString( s ) ) {
				s += 2;
				continue;
			}
			CG_DrawChar( xx + 2, y + 2, charWidth, charHeight, *s );
			cnt++;
			xx += charWidth;
			s++;
		}
	}

	// draw the colored text
	s = string;
	xx = x;
	cnt = 0;
	trap_R_SetColor( setColor );
	while ( *s && cnt < maxChars) {
		if ( Q_IsColorString( s ) ) {
			if ( !forceColor ) {
				Vector4Copy( g_color_table[ColorIndex(*(s+1))], color );
				color[3] = setColor[3];
				trap_R_SetColor( color );
			}
			s += 2;
			continue;
		}
		CG_DrawChar( xx, y, charWidth, charHeight, *s );
		xx += charWidth;
		cnt++;
		s++;
	}
	ResetRenderColor();
}


// new font renderer

#ifdef USE_NEW_FONT_RENDERER

#define MAX_FONT_SHADERS 4

typedef struct {
	float tc_prop[4];
	float tc_mono[4];
	float space1;
	float space2;
	float width;
} font_metric_t;

typedef struct {
	font_metric_t	metrics[256];
	qhandle_t		shader[ MAX_FONT_SHADERS ];
	int				shaderThreshold[ MAX_FONT_SHADERS ];
	int				shaderCount;
} font_t;

static font_t bigchars;
static font_t numbers;
static const font_t *font = &bigchars;
static const font_metric_t *metrics = &bigchars.metrics[0];

namespace {

constexpr size_t kFontFileBufferSize = 8000;

struct FontShaderEntry {
	std::array<char, MAX_QPATH> name{};
	int threshold = 0;
};

struct GlyphLayout {
	const float *texCoords = nullptr;
	float drawWidth = 0.0f;
	float endX = 0.0f;
};

using FontShaderEntries = std::array<FontShaderEntry, MAX_FONT_SHADERS>;

[[nodiscard]] const char *ParseFontToken( char **text, const qboolean allowLineBreaks ) {
	return COM_ParseExt( text, allowLineBreaks );
}

[[nodiscard]] bool ParseRequiredFontToken( char **text, const qboolean allowLineBreaks, const char *errorMessage, const char *&token ) {
	token = ParseFontToken( text, allowLineBreaks );
	if ( token[0] != '\0' ) {
		return true;
	}

	Com_Printf( "%s\n", errorMessage );
	return false;
}

[[nodiscard]] bool ParseRequiredFontFloat( char **text, const qboolean allowLineBreaks, const char *errorMessage, float &value ) {
	const char *token = nullptr;
	if ( !ParseRequiredFontToken( text, allowLineBreaks, errorMessage, token ) ) {
		return false;
	}

	value = atof( token );
	return true;
}

[[nodiscard]] bool ParsePositiveFontFloat( char **text, const qboolean allowLineBreaks, const char *errorMessage, float &value ) {
	if ( !ParseRequiredFontFloat( text, allowLineBreaks, errorMessage, value ) ) {
		return false;
	}

	if ( value > 0.0f ) {
		return true;
	}

	Com_Printf( "%s\n", errorMessage );
	return false;
}

void SortFontShaderEntries( FontShaderEntries &shaderEntries, const int shaderCount ) {
	auto entries = shaderEntries.begin();
	std::ranges::sort( entries, entries + shaderCount, std::less<>{}, &FontShaderEntry::threshold );

	if ( shaderCount > 0 ) {
		shaderEntries[0].threshold = 0;
	}
}

[[nodiscard]] GlyphLayout LayoutGlyph( const font_metric_t &metric, const float x, const float glyphWidth, const bool proportional ) noexcept {
	if ( proportional ) {
		const float adjustedX = x + metric.space1 * glyphWidth;
		return { metric.tc_prop, metric.width * glyphWidth, adjustedX + metric.space2 * glyphWidth };
	}

	return { metric.tc_mono, glyphWidth, x + glyphWidth };
}

[[nodiscard]] qhandle_t FontShaderForHeight( const float glyphHeight ) noexcept {
	qhandle_t shader = font->shader[0];
	for ( int shaderIndex = 1; shaderIndex < font->shaderCount; ++shaderIndex ) {
		if ( glyphHeight >= font->shaderThreshold[shaderIndex] ) {
			shader = font->shader[shaderIndex];
		}
	}
	return shader;
}

void ApplyTextColorCode( vec4_t color, const byte colorCode, const float alpha ) {
	VectorCopy( g_color_table[ColorIndex( colorCode )], color );
	color[3] = alpha;
}

} // namespace


void CG_SelectFont( int index ) 
{
	if ( index == 0 )
		font = &bigchars;
	else
		font = &numbers;

	metrics = &font->metrics[0];
}


static qboolean CG_FileExist( const char *file )
{
	fileHandle_t	f;

	if ( !file || !file[0] )
		return qfalse;
	
	trap_FS_FOpenFile( file, &f, FS_READ );
	if ( f == FS_INVALID_HANDLE )
		return qfalse;
	else {
		trap_FS_FCloseFile( f );
		return qtrue;
	}
}


static void CG_LoadFont( font_t *fnt, const char *fontName )
{
	std::array<char, kFontFileBufferSize> buffer{};
	FontShaderEntries shaderEntries{};
	fileHandle_t fileHandle;
	char *text = buffer.data();
	float width = 0.0f;
	float height = 0.0f;
	float charWidth = 0.0f;
	float charHeight = 0.0f;
	const char *token = nullptr;
	int shaderCount = 0;
	int chars = 0;

	*fnt = font_t{};

	int len = trap_FS_FOpenFile( fontName, &fileHandle, FS_READ );
	if ( fileHandle == FS_INVALID_HANDLE ) {
		CG_Printf( S_COLOR_YELLOW "CG_LoadFont: error opening %s\n", fontName );
		return;
	}

	if ( len >= static_cast<int>( buffer.size() ) ) {
		CG_Printf( S_COLOR_YELLOW "CG_LoadFont: font file is too long: %i\n", len );
		len = static_cast<int>( buffer.size() ) - 1;
	}

	trap_FS_Read( buffer.data(), len, fileHandle );
	trap_FS_FCloseFile( fileHandle );
	buffer[len] = '\0';

	COM_BeginParseSession( fontName );

	while ( true ) {
		token = ParseFontToken( &text, qtrue );
		if ( token[0] == '\0' ) {
			Com_Printf( S_COLOR_RED "CG_LoadFont: parse error.\n" );
			return;
		}

		// font image
		if ( strcmp( token, "img" ) == 0 ) {
			if ( shaderCount >= MAX_FONT_SHADERS ) {
				Com_Printf( "CG_LoadFont: too many font images, ignoring.\n" );
				SkipRestOfLine( &text );
				continue;
			}
			if ( !ParseRequiredFontToken( &text, qfalse, "CG_LoadFont: error reading font image.", token ) ) {
				return;
			}
			if ( !CG_FileExist( token ) ) {
				Com_Printf( "CG_LoadFont: font image '%s' doesn't exist.\n", token );
				return;
			}
			Q_strncpyz( shaderEntries[shaderCount].name.data(), token, static_cast<int>( shaderEntries[shaderCount].name.size() ) );

			if ( !ParseRequiredFontToken( &text, qfalse, "CG_LoadFont: error reading image threshold.", token ) ) {
				return;
			}
			shaderEntries[shaderCount].threshold = atoi( token );

			//Com_Printf( S_COLOR_CYAN "img: %s, threshold: %i\n", shaderName[ shaderCount ], shaderThreshold[ shaderCount ] );
			shaderCount++;
			
			SkipRestOfLine( &text );
			continue;
		}

		// font parameters
		if ( strcmp( token, "fnt" ) == 0 ) {
			if ( !ParsePositiveFontFloat( &text, qfalse, "CG_LoadFont: error reading image width.", width ) ) {
				return;
			}
			if ( !ParsePositiveFontFloat( &text, qfalse, "CG_LoadFont: error reading image height.", height ) ) {
				return;
			}
			if ( !ParseRequiredFontFloat( &text, qfalse, "CG_LoadFont: error reading char widht.", charWidth ) ) {
				return;
			}
			if ( !ParseRequiredFontFloat( &text, qfalse, "CG_LoadFont: error reading char height.", charHeight ) ) {
				return;
			}

			break; // parse char metrics
		}
	}

	if ( shaderCount == 0 ) {
		Com_Printf( "CG_LoadFont: no font images specified in %s.\n", fontName );
		return;
	}

	const float reciprocalWidth = 1.0f / width;
	const float reciprocalHeight = 1.0f / height;

	chars = 0;
	for ( ;; ) {
		// char index
		token = ParseFontToken( &text, qtrue );
		if ( !token[0] ) {
			break;
		}

		int charIndex = 0;
		if ( token[0] == '\'' && token[1] && token[2] == '\'' ) // char code in form 'X'
			charIndex = token[1] & 255;
		else // integer code
			charIndex = atoi( token );

		if ( charIndex < 0 || charIndex > 255 ) {
			CG_Printf( S_COLOR_RED "CG_LoadFont: bad char index %i.\n", charIndex );
			return;
		}
		font_metric_t &metric = fnt->metrics[charIndex];
		float x0 = 0.0f;
		float y0 = 0.0f;
		float widthOffset = 0.0f;
		float widthSpan = 0.0f;
		float spaceBefore = 0.0f;
		float spaceAfter = 0.0f;

		// x0
		if ( !ParseRequiredFontFloat( &text, qfalse, "CG_LoadFont: error reading x0.", x0 ) ) {
			return;
		}

		// y0
		if ( !ParseRequiredFontFloat( &text, qfalse, "CG_LoadFont: error reading y0.", y0 ) ) {
			return;
		}

		// w1-offset
		if ( !ParseRequiredFontFloat( &text, qfalse, "CG_LoadFont: error reading x-offset.", widthOffset ) ) {
			return;
		}

		// w2-offset
		if ( !ParseRequiredFontFloat( &text, qfalse, "CG_LoadFont: error reading x-length.", widthSpan ) ) {
			return;
		}

		// space1
		if ( !ParseRequiredFontFloat( &text, qfalse, "CG_LoadFont: error reading space1.", spaceBefore ) ) {
			return;
		}

		// space2
		if ( !ParseRequiredFontFloat( &text, qfalse, "CG_LoadFont: error reading space2.", spaceAfter ) ) {
			return;
		}

		metric.tc_mono[0] = x0 * reciprocalWidth;
		metric.tc_mono[1] = y0 * reciprocalHeight;
		metric.tc_mono[2] = ( x0 + charWidth ) * reciprocalWidth;
		metric.tc_mono[3] = ( y0 + charHeight ) * reciprocalHeight;

		// proportional y-coords is matching with mono
		metric.tc_prop[1] = metric.tc_mono[1];
		metric.tc_prop[3] = metric.tc_mono[3];

		metric.width = widthSpan / charWidth;
		metric.space1 = spaceBefore / charWidth;
		metric.space2 = ( spaceAfter + widthSpan ) / charWidth;
		metric.tc_prop[0] = metric.tc_mono[0] + ( widthOffset * reciprocalWidth );
		metric.tc_prop[2] = metric.tc_prop[0] + ( widthSpan * reciprocalWidth );

		chars++;

		SkipRestOfLine( &text );
	}

	SortFontShaderEntries( shaderEntries, shaderCount );
	
	fnt->shaderCount = shaderCount;
	for ( int shaderIndex = 0; shaderIndex < shaderCount; ++shaderIndex ) {
		fnt->shader[shaderIndex] = trap_R_RegisterShaderNoMip( shaderEntries[shaderIndex].name.data() );
		fnt->shaderThreshold[shaderIndex] = shaderEntries[shaderIndex].threshold;
	}

	CG_Printf( "Font '%s' loaded with %i chars and %i images\n", fontName, chars, shaderCount );
}


void CG_LoadFonts( void ) 
{
	CG_LoadFont( &bigchars, "gfx/2d/bigchars.cfg" );
	CG_LoadFont( &numbers, "gfx/2d/numbers.cfg" );
}


static float DrawStringLength( const char *string, float ax, float aw, float max_ax, int proportional )
{
	if ( !string ) {
		return 0.0f;
	}

	const byte *text = reinterpret_cast<const byte *>( string );
	const float startX = ax;
	const bool useProportionalLayout = proportional != 0;

	while ( *text != '\0' ) {
		if ( IsQColorSequence( text ) ) {
			text += 2;
			continue;
		}

		const GlyphLayout glyph = LayoutGlyph( metrics[*text], ax, aw, useProportionalLayout );
		if ( glyph.endX > max_ax ) {
			break;
		}

		ax = glyph.endX;
		++text;
	}

	return ax - startX;
}


void CG_DrawString( float x, float y, const char *string, const vec4_t setColor, float charWidth, float charHeight, int maxChars, int flags ) 
{
	if ( !string ) {
		return;
	}

	const bool proportional = ( flags & DS_PROPORTIONAL ) != 0;
	const float baseX = x * cgs.screenXScale + cgs.screenXBias;
	const float ay = y * cgs.screenYScale + cgs.screenYBias;
	const float aw = charWidth * cgs.screenXScale;
	const float ah = charHeight * cgs.screenYScale;
	const float maxAx = maxChars <= 0 ? 9999999.0f : baseX + aw * maxChars;
	float ax = baseX;
	vec4_t color{};
	const qhandle_t shader = FontShaderForHeight( ah );
	const byte *text = reinterpret_cast<const byte *>( string );

	if ( flags & ( DS_CENTER | DS_RIGHT ) ) {
		if ( flags & DS_CENTER ) {
			ax -= 0.5f * DrawStringLength( string, ax, aw, maxAx, proportional );
		} else {
			ax -= DrawStringLength( string, ax, aw, maxAx, proportional );
		}
	}

	if ( flags & DS_SHADOW ) { 
		const float shadowBaseX = ax;

		// calculate shadow offsets
		const float shadowScale = charWidth * 0.075f; // charWidth/15
		const float shadowOffsetX = shadowScale * cgs.screenXScale;
		const float shadowOffsetY = shadowScale * cgs.screenYScale;

		color[0] = color[1] = color[2] = 0.0f;
		color[3] = setColor[3] * 0.5f;
		trap_R_SetColor( color );

		while ( *text != '\0' ) {
			if ( IsQColorSequence( text ) ) {
				text += 2;
				continue;
			}

			const GlyphLayout glyph = LayoutGlyph( metrics[*text], ax, aw, proportional );
			const float glyphX = proportional ? glyph.endX - glyph.drawWidth : ax;
			if ( glyph.endX > maxAx || glyphX >= cgs.glconfig.vidWidth ) {
				break;
			}

			trap_R_DrawStretchPic( glyphX + shadowOffsetX, ay + shadowOffsetY, glyph.drawWidth, ah,
				glyph.texCoords[0], glyph.texCoords[1], glyph.texCoords[2], glyph.texCoords[3], shader );

			ax = glyph.endX;
			++text;
		}

		// recover altered parameters
		text = reinterpret_cast<const byte *>( string );
		ax = shadowBaseX;
	}
	
	Vector4Copy( setColor, color );
	trap_R_SetColor( color );
	
	while ( *text != '\0' ) {

		if ( IsQColorSequence( text ) ) {
			if ( !( flags & DS_FORCE_COLOR ) ) {
				ApplyTextColorCode( color, text[1], setColor[3] );
				trap_R_SetColor( color );
			}
			text += 2;
			continue;
		}

		const GlyphLayout glyph = LayoutGlyph( metrics[*text], ax, aw, proportional );
		const float glyphX = proportional ? glyph.endX - glyph.drawWidth : ax;
		if ( glyph.endX > maxAx || glyphX >= cgs.glconfig.vidWidth ) {
			break;
		}

		trap_R_DrawStretchPic( glyphX, ay, glyph.drawWidth, ah,
			glyph.texCoords[0], glyph.texCoords[1], glyph.texCoords[2], glyph.texCoords[3], shader );

		ax = glyph.endX;
		++text;
	}

	ResetRenderColor();
}
#else


static float DrawStringLen( const char *s, float charWidth ) 
{
	int count;
	count = 0;
	while ( *s ) {
		if ( Q_IsColorString( s ) ) {
			s += 2;
		} else {
			count++;
			s++;
		}
	}
	return count * charWidth;
}


void CG_DrawString( float x, float y, const char *s, const vec4_t color, float charWidth, float charHeight, int maxChars, int flags )
{
	if ( !color ) 
	{
		color = g_color_table[ ColorIndex( COLOR_WHITE ) ];
	}

	if ( flags & ( DS_CENTER | DS_RIGHT ) )
	{
		float w;
		w = DrawStringLen( s, charWidth );
		if ( flags & DS_CENTER )
			x -= w * 0.5f;
		else
			x -= w;
	}

	CG_DrawStringExt( x, y, s, color, flags & DS_FORCE_COLOR, flags & DS_SHADOW, charWidth, charHeight, maxChars );
}
#endif


/*
=================
CG_DrawStrlen

Returns character count, skiping color escape codes
=================
*/
int CG_DrawStrlen( const char *str ) {
	const char *s = str;
	int count = 0;

	while ( *s ) {
		if ( Q_IsColorString( s ) ) {
			s += 2;
		} else {
			count++;
			s++;
		}
	}

	return count;
}


/*
=============
CG_TileClearBox

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.
=============
*/
static void CG_TileClearBox( int x, int y, int w, int h, qhandle_t hShader ) {
	float	s1, t1, s2, t2;

	s1 = x/64.0;
	t1 = y/64.0;
	s2 = (x+w)/64.0;
	t2 = (y+h)/64.0;
	trap_R_DrawStretchPic( x, y, w, h, s1, t1, s2, t2, hShader );
}


/*
==============
CG_TileClear

Clear around a sized down screen
==============
*/
void CG_TileClear( void ) {
	int		top, bottom, left, right;
	int		w, h;

	w = cgs.glconfig.vidWidth;
	h = cgs.glconfig.vidHeight;

	if ( cg.refdef.x == 0 && cg.refdef.y == 0 && 
		cg.refdef.width == w && cg.refdef.height == h ) {
		return;		// full screen rendering
	}

	top = cg.refdef.y;
	bottom = top + cg.refdef.height-1;
	left = cg.refdef.x;
	right = left + cg.refdef.width-1;

	// clear above view screen
	CG_TileClearBox( 0, 0, w, top, cgs.media.backTileShader );

	// clear below view screen
	CG_TileClearBox( 0, bottom, w, h - bottom, cgs.media.backTileShader );

	// clear left of view screen
	CG_TileClearBox( 0, top, left, bottom - top + 1, cgs.media.backTileShader );

	// clear right of view screen
	CG_TileClearBox( right, top, w - right, bottom - top + 1, cgs.media.backTileShader );
}


/*
================
CG_FadeColor
================
*/
float *CG_FadeColor( int startMsec, int totalMsec ) {
	static vec4_t color{};
	return EvaluateFadeColor( startMsec, totalMsec, FADE_TIME, color );
}


/*
================
CG_FadeColorTime
================
*/
float *CG_FadeColorTime( int startMsec, int totalMsec, int fadeMsec ) {
	static vec4_t color{};
	return EvaluateFadeColor( startMsec, totalMsec, fadeMsec, color );
}


/*
================
CG_TeamColor
================
*/
const float *CG_TeamColor( team_t team ) {
	static const vec4_t red = {1, 0.2f, 0.2f, 1};
	static const vec4_t blue = {0.2f, 0.2f, 1, 1};
	static const vec4_t other = {1, 1, 1, 1};
	static const vec4_t spectator = {0.7f, 0.7f, 0.7f, 1};

	switch ( team ) {
	case TEAM_RED:
		return red;
	case TEAM_BLUE:
		return blue;
	case TEAM_SPECTATOR:
		return spectator;
	default:
		return other;
	}
}



/*
=================
CG_GetColorForHealth
=================
*/
void CG_GetColorForHealth( int health, int armor, vec4_t hcolor ) {
	// calculate the total points of damage that can
	// be sustained at the current health / armor level
	if ( health <= 0 ) {
		VectorClear( hcolor );	// black
		hcolor[3] = 1.0f;
		return;
	}

	const int absorbableArmor = static_cast<int>( health * ARMOR_PROTECTION / ( 1.0f - ARMOR_PROTECTION ) );
	const int effectiveHealth = health + std::min( armor, absorbableArmor );

	// set the color based on health
	hcolor[0] = 1.0;
	hcolor[3] = 1.0;
	hcolor[2] = NormalizedHealthChannel( effectiveHealth, 66, 100 );
	hcolor[1] = NormalizedHealthChannel( effectiveHealth, 30, 60 );
}


/*
=================
CG_ColorForHealth
=================
*/
void CG_ColorForHealth( vec4_t hcolor ) {

	CG_GetColorForHealth( cg.snap->ps.stats[STAT_HEALTH], 
		cg.snap->ps.stats[STAT_ARMOR], hcolor );
}



// bk001205 - code below duplicated in q3_ui/ui-atoms.c
// bk001205 - FIXME: does this belong in ui_shared.c?
// bk001205 - FIXME: HARD_LINKED flags not visible here
#ifndef Q3_STATIC // bk001205 - q_shared defines not visible here 
/*
=================
UI_DrawProportionalString2
=================
*/
static int	propMap[128][3] = {
{0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1},
{0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1},

{0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1},
{0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1},

{0, 0, PROP_SPACE_WIDTH},		// SPACE
{11, 122, 7},	// !
{154, 181, 14},	// "
{55, 122, 17},	// #
{79, 122, 18},	// $
{101, 122, 23},	// %
{153, 122, 18},	// &
{9, 93, 7},		// '
{207, 122, 8},	// (
{230, 122, 9},	// )
{177, 122, 18},	// *
{30, 152, 18},	// +
{85, 181, 7},	// ,
{34, 93, 11},	// -
{110, 181, 6},	// .
{130, 152, 14},	// /

{22, 64, 17},	// 0
{41, 64, 12},	// 1
{58, 64, 17},	// 2
{78, 64, 18},	// 3
{98, 64, 19},	// 4
{120, 64, 18},	// 5
{141, 64, 18},	// 6
{204, 64, 16},	// 7
{162, 64, 17},	// 8
{182, 64, 18},	// 9
{59, 181, 7},	// :
{35,181, 7},	// ;
{203, 152, 14},	// <
{56, 93, 14},	// =
{228, 152, 14},	// >
{177, 181, 18},	// ?

{28, 122, 22},	// @
{5, 4, 18},		// A
{27, 4, 18},	// B
{48, 4, 18},	// C
{69, 4, 17},	// D
{90, 4, 13},	// E
{106, 4, 13},	// F
{121, 4, 18},	// G
{143, 4, 17},	// H
{164, 4, 8},	// I
{175, 4, 16},	// J
{195, 4, 18},	// K
{216, 4, 12},	// L
{230, 4, 23},	// M
{6, 34, 18},	// N
{27, 34, 18},	// O

{48, 34, 18},	// P
{68, 34, 18},	// Q
{90, 34, 17},	// R
{110, 34, 18},	// S
{130, 34, 14},	// T
{146, 34, 18},	// U
{166, 34, 19},	// V
{185, 34, 29},	// W
{215, 34, 18},	// X
{234, 34, 18},	// Y
{5, 64, 14},	// Z
{60, 152, 7},	// [
{106, 151, 13},	// '\'
{83, 152, 7},	// ]
{128, 122, 17},	// ^
{4, 152, 21},	// _

{134, 181, 5},	// '
{5, 4, 18},		// A
{27, 4, 18},	// B
{48, 4, 18},	// C
{69, 4, 17},	// D
{90, 4, 13},	// E
{106, 4, 13},	// F
{121, 4, 18},	// G
{143, 4, 17},	// H
{164, 4, 8},	// I
{175, 4, 16},	// J
{195, 4, 18},	// K
{216, 4, 12},	// L
{230, 4, 23},	// M
{6, 34, 18},	// N
{27, 34, 18},	// O

{48, 34, 18},	// P
{68, 34, 18},	// Q
{90, 34, 17},	// R
{110, 34, 18},	// S
{130, 34, 14},	// T
{146, 34, 18},	// U
{166, 34, 19},	// V
{185, 34, 29},	// W
{215, 34, 18},	// X
{234, 34, 18},	// Y
{5, 64, 14},	// Z
{153, 152, 13},	// {
{11, 181, 5},	// |
{180, 152, 13},	// }
{79, 93, 17},	// ~
{0, 0, -1}		// DEL
};

static int propMapB[26][3] = {
{11, 12, 33},
{49, 12, 31},
{85, 12, 31},
{120, 12, 30},
{156, 12, 21},
{183, 12, 21},
{207, 12, 32},

{13, 55, 30},
{49, 55, 13},
{66, 55, 29},
{101, 55, 31},
{135, 55, 21},
{158, 55, 40},
{204, 55, 32},

{12, 97, 31},
{48, 97, 31},
{82, 97, 30},
{118, 97, 30},
{153, 97, 30},
{185, 97, 25},
{213, 97, 30},

{11, 139, 32},
{42, 139, 51},
{93, 139, 32},
{126, 139, 31},
{158, 139, 25},
};

#define PROPB_GAP_WIDTH		4
#define PROPB_SPACE_WIDTH	12
#define PROPB_HEIGHT		36

namespace {

[[nodiscard]] int BannerGlyphWidth( const unsigned char ch ) noexcept {
	if ( ch == ' ' ) {
		return PROPB_SPACE_WIDTH;
	}
	if ( ch >= 'A' && ch <= 'Z' ) {
		return propMapB[ch - 'A'][2];
	}
	return 0;
}

[[nodiscard]] int BannerStringWidth( const char *text ) noexcept {
	int width = 0;
	for ( const unsigned char *cursor = reinterpret_cast<const unsigned char *>( text ); *cursor; ++cursor ) {
		const int glyphWidth = BannerGlyphWidth( *cursor );
		if ( glyphWidth > 0 ) {
			width += glyphWidth + PROPB_GAP_WIDTH;
		}
	}
	return width > 0 ? width - PROPB_GAP_WIDTH : 0;
}

[[nodiscard]] int ProportionalGlyphWidth( const unsigned char ch ) noexcept {
	const int glyphWidth = propMap[ch][2];
	return glyphWidth != -1 ? glyphWidth : 0;
}

[[nodiscard]] int ProportionalStringWidthInternal( const char *text ) noexcept {
	int width = 0;
	for ( const unsigned char *cursor = reinterpret_cast<const unsigned char *>( text ); *cursor; ++cursor ) {
		const int glyphWidth = ProportionalGlyphWidth( *cursor );
		if ( glyphWidth > 0 ) {
			width += glyphWidth + PROP_GAP_WIDTH;
		}
	}
	return width > 0 ? width - PROP_GAP_WIDTH : 0;
}

void SetShadowColor( vec4_t drawColor, const vec4_t color ) noexcept {
	drawColor[0] = 0.0f;
	drawColor[1] = 0.0f;
	drawColor[2] = 0.0f;
	drawColor[3] = color[3];
}

void SetScaledColor( vec4_t drawColor, const vec4_t color, const float scale ) noexcept {
	drawColor[0] = color[0] * scale;
	drawColor[1] = color[1] * scale;
	drawColor[2] = color[2] * scale;
	drawColor[3] = color[3];
}

} // namespace

/*
=================
UI_DrawBannerString
=================
*/
static void UI_DrawBannerString2( int x, int y, const char* str, vec4_t color )
{
	const char* s;
	unsigned char	ch; // bk001204 : array subscript
	float	ax;
	float	ay;
	float	aw;
	float	ah;
	float	frow;
	float	fcol;
	float	fwidth;
	float	fheight;

	// draw the colored text
	trap_R_SetColor( color );
	
	ax = x * cgs.screenXScale + cgs.screenXBias;
	ay = y * cgs.screenYScale + cgs.screenYBias;

	s = str;
	while ( *s )
	{
		ch = *s & 127;
		if ( ch == ' ' ) {
			ax += ((float)PROPB_SPACE_WIDTH + (float)PROPB_GAP_WIDTH)* cgs.screenXScale;
		}
		else if ( ch >= 'A' && ch <= 'Z' ) {
			ch -= 'A';
			fcol = (float)propMapB[ch][0] / 256.0f;
			frow = (float)propMapB[ch][1] / 256.0f;
			fwidth = (float)propMapB[ch][2] / 256.0f;
			fheight = (float)PROPB_HEIGHT / 256.0f;
			aw = (float)propMapB[ch][2] * cgs.screenXScale;
			ah = (float)PROPB_HEIGHT * cgs.screenXScale;
			trap_R_DrawStretchPic( ax, ay, aw, ah, fcol, frow, fcol+fwidth, frow+fheight, cgs.media.charsetPropB );
			ax += (aw + (float)PROPB_GAP_WIDTH * cgs.screenXScale);
		}
		s++;
	}

	ResetRenderColor();
}

void UI_DrawBannerString( int x, int y, const char* str, int style, vec4_t color ) {
	vec4_t			drawcolor;

	// find the width of the drawn text
	const int width = BannerStringWidth( str );

	switch( style & UI_FORMATMASK ) {
		case UI_CENTER:
			x -= width / 2;
			break;

		case UI_RIGHT:
			x -= width;
			break;

		case UI_LEFT:
		default:
			break;
	}

	if ( style & UI_DROPSHADOW ) {
		SetShadowColor( drawcolor, color );
		UI_DrawBannerString2( x+2, y+2, str, drawcolor );
	}

	UI_DrawBannerString2( x, y, str, color );
}


int UI_ProportionalStringWidth( const char* str ) {
	return ProportionalStringWidthInternal( str );
}

static void UI_DrawProportionalString2( int x, int y, const char* str, vec4_t color, float sizeScale, qhandle_t charset )
{
	const char* s;
	unsigned char	ch; // bk001204 - unsigned
	float	ax;
	float	ay;
	float	aw;
	float	ah;
	float	frow;
	float	fcol;
	float	fwidth;
	float	fheight;

	// draw the colored text
	trap_R_SetColor( color );
	
	ax = x * cgs.screenXScale + cgs.screenXBias;
	ay = y * cgs.screenYScale + cgs.screenYBias;

	s = str;
	while ( *s )
	{
		ch = *s & 127;
		if ( ch == ' ' ) {
			aw = (float)PROP_SPACE_WIDTH * cgs.screenXScale * sizeScale;
		} else if ( propMap[ch][2] != -1 ) {
			fcol = (float)propMap[ch][0] / 256.0f;
			frow = (float)propMap[ch][1] / 256.0f;
			fwidth = (float)propMap[ch][2] / 256.0f;
			fheight = (float)PROP_HEIGHT / 256.0f;
			aw = (float)propMap[ch][2] * cgs.screenXScale * sizeScale;
			ah = (float)PROP_HEIGHT * cgs.screenXScale * sizeScale;
			trap_R_DrawStretchPic( ax, ay, aw, ah, fcol, frow, fcol+fwidth, frow+fheight, charset );
		} else {
			aw = 0;
		}

		ax += (aw + (float)PROP_GAP_WIDTH * cgs.screenXScale * sizeScale);
		s++;
	}

	ResetRenderColor();
}

/*
=================
UI_ProportionalSizeScale
=================
*/
float UI_ProportionalSizeScale( int style ) {
	if(  style & UI_SMALLFONT ) {
		return 0.75;
	}

	return 1.00;
}


/*
=================
UI_DrawProportionalString
=================
*/
void UI_DrawProportionalString( int x, int y, const char* str, int style, vec4_t color ) {
	vec4_t	drawcolor;
	int		width;
	float	sizeScale;

	sizeScale = UI_ProportionalSizeScale( style );

	switch( style & UI_FORMATMASK ) {
		case UI_CENTER:
			width = static_cast<int>( ProportionalStringWidthInternal( str ) * sizeScale );
			x -= width / 2;
			break;

		case UI_RIGHT:
			width = static_cast<int>( ProportionalStringWidthInternal( str ) * sizeScale );
			x -= width;
			break;

		case UI_LEFT:
		default:
			break;
	}

	if ( style & UI_DROPSHADOW ) {
		SetShadowColor( drawcolor, color );
		UI_DrawProportionalString2( x+2, y+2, str, drawcolor, sizeScale, cgs.media.charsetProp );
	}

	if ( style & UI_INVERSE ) {
		SetScaledColor( drawcolor, color, 0.8f );
		UI_DrawProportionalString2( x, y, str, drawcolor, sizeScale, cgs.media.charsetProp );
		return;
	}

	if ( style & UI_PULSE ) {
		SetScaledColor( drawcolor, color, 0.8f );
		UI_DrawProportionalString2( x, y, str, color, sizeScale, cgs.media.charsetProp );

		Vector4Copy( color, drawcolor );
		drawcolor[3] = 0.5 + 0.5 * sin( ( cg.time % TMOD_075 ) / PULSE_DIVISOR );
		UI_DrawProportionalString2( x, y, str, drawcolor, sizeScale, cgs.media.charsetPropGlow );
		return;
	}

	UI_DrawProportionalString2( x, y, str, color, sizeScale, cgs.media.charsetProp );
}
#endif // Q3STATIC
