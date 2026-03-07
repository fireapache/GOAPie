#pragma once

#include <string>

namespace gie
{
	class World;
	class Planner;
	class Goal;
}

typedef void ( *ImGuiDrawFunc )( gie::World&, gie::Planner&, gie::Goal&, gie::Guid );
typedef void ( *GLDrawFunc )( gie::World&, gie::Planner& );

// TODO: Rename ExampleParameters to ProjectParameters (frontend mapping)
struct ExampleParameters
{
gie::World& world;
gie::Planner& planner;
gie::Goal& goal;
ImGuiDrawFunc imGuiDrawFunc{ nullptr };
GLDrawFunc glDrawFunc{ nullptr };
bool visualize{ false };
};

// Validation check macro for example validateResult functions.
// Usage: VALIDATE( expr, failMsg ) — if expr is false, sets failMsg and returns 1.
#define VALIDATE( expr, msg ) \
	do { \
		if( !( expr ) ) { \
			failMsg = std::string( msg ) + " (line " + std::to_string( __LINE__ ) + ")"; \
			return 1; \
		} \
	} while( 0 )

// Equality check that prints expected vs actual.
#define VALIDATE_EQ( actual, expected, msg ) \
	do { \
		if( ( actual ) != ( expected ) ) { \
			failMsg = std::string( msg ) + ": expected " + std::to_string( expected ) \
				+ ", got " + std::to_string( actual ) + " (line " + std::to_string( __LINE__ ) + ")"; \
			return 1; \
		} \
	} while( 0 )

// String equality check that prints expected vs actual.
#define VALIDATE_STR_EQ( actual, expected, msg ) \
	do { \
		if( ( actual ) != ( expected ) ) { \
			failMsg = std::string( msg ) + ": expected \"" + std::string( expected ) \
				+ "\", got \"" + std::string( actual ) + "\" (line " + std::to_string( __LINE__ ) + ")"; \
			return 1; \
		} \
	} while( 0 )
