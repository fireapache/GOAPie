#include <cstdio>
#include <cstdlib>
#include <vector>

// example 1
int fundamentals();
// example 2
int openDoor();
// example 3
int cutDownTrees();

int main( int argc, char** argv )
{
	std::vector< int(*)() > funcs { fundamentals, openDoor, cutDownTrees };

	int ex = -1;

	if( argc > 1 )
	{
		ex = std::atoi( argv[ 1 ] );
	}
	else
	{
		while( true )
		{
			std::printf( "Enter valid example number [1..3]: " );
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
	
	int exResult = funcs[ ex - 1 ]();

	return exResult;
}