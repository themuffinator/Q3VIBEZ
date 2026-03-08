// Copyright (C) 1999-2000 Id Software, Inc.
//
//
// g_mem.c
//


#include "g_local.h"

#include <array>

#ifdef __cplusplus
#undef G_Alloc
#endif


namespace {

constexpr std::size_t PoolSize = 768 * 1024;
constexpr std::size_t AllocationAlignment = 32;

std::array<char, PoolSize> memoryPool{};
std::size_t allocPoint = 0;

[[nodiscard]] constexpr std::size_t AlignAllocationSize( const std::size_t size ) noexcept {
	return ( size + ( AllocationAlignment - 1 ) ) & ~( AllocationAlignment - 1 );
}

[[nodiscard]] std::size_t RemainingPoolBytes() noexcept {
	return PoolSize - allocPoint;
}

}

void *G_Alloc( size_t size ) {
	const std::size_t alignedSize = AlignAllocationSize( size );

	if ( g_debugAlloc.integer ) {
		const std::size_t remainingAfterAlloc = alignedSize <= RemainingPoolBytes() ? RemainingPoolBytes() - alignedSize : 0;
		G_Printf( "G_Alloc of %i bytes (%i left)\n", static_cast<int>( size ), static_cast<int>( remainingAfterAlloc ) );
	}

	if ( allocPoint + alignedSize > PoolSize ) {
		G_Error( "G_Alloc: failed on allocation of %i bytes", static_cast<int>( size ) );
		return nullptr;
	}

	char *const allocation = memoryPool.data() + allocPoint;
	allocPoint += alignedSize;

	return allocation;
}

void G_InitMemory( void ) {
	allocPoint = 0;
}

void Svcmd_GameMem_f( void ) {
	G_Printf( "Game memory status: %i out of %i bytes allocated\n", static_cast<int>( allocPoint ), static_cast<int>( PoolSize ) );
}
