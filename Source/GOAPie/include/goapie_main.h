#pragma once

#include "goapie.h"

namespace gie
{
	StringRegister& stringRegister()
	{
		static StringRegister instance;
		return instance;
	}
} // namespace gie