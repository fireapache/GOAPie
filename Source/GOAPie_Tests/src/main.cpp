#include <cstdio>
#include <cstdlib>
#include <vector>
#include <goapie.h>

// example 1
extern int fundamentals( gie::World& world );
// example 2
extern int openDoor( gie::World& world );
// example 3
extern int cutDownTrees( gie::World& world );
// example 4
extern int treesOnHill( gie::World& world );

extern int visualization( const gie::World& world );

int main( int argc, char** argv )
{
	std::vector< int ( * )( gie::World& world ) > funcs{ fundamentals, openDoor, cutDownTrees, treesOnHill };

	int ex = -1;

	if( argc > 1 )
	{
		ex = std::atoi( argv[ 1 ] );
	}
	else
	{
		while( true )
		{
			std::printf( "Enter valid example number [1..4]: " );
			std::scanf( "%d", &ex );

			if( ex < 1 || ex > funcs.size() )
			{
				std::printf( "Wrong example number!" );
			}
			else
			{
				break;
			}
		}
	}
	
	gie::World world{};

	int exResult = funcs[ ex - 1 ]( world );

	return visualization( world );

	//return exResult;
}