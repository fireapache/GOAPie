#include <cstdio>
#include <cstdlib>

// example 1
int fundamentals();
// example 2
int openDoor();
// example 3
int cutDownTrees();

int main( int argc, char** argv )
{
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

			if( ex < 1 || ex > 3 )
			{
				std::printf( "Wrong example number!" );
			}
			else
			{
				break;
			}
		}
	}
	
	int exResult = 1;

	switch( ex )
	{
	case 1:
		exResult = fundamentals();
		break;
	case 2:
		exResult = openDoor();
		break;
	case 3:
		exResult = cutDownTrees();
		break;
	default:
		break;
	}

	return exResult;
}