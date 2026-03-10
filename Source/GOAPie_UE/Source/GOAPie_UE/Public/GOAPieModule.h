#pragma once

#include "Modules/ModuleManager.h"

class FGOAPieModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
