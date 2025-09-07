#pragma once

#include "goapie.h"

namespace gie
{
inline StringRegister& stringRegister()
{
static StringRegister instance;
return instance;
}

 // Implementation of Goal::reached method
inline bool Goal::reached( const Simulation& simulation ) const
{
for( auto targetItr = targets.cbegin(); targetItr != targets.cend(); targetItr++ )
{
auto ppt = simulation.context().property( targetItr->first );
if( ppt && ppt->value != targetItr->second )
{
return false;
}
}
return true;
}
} // namespace gie
