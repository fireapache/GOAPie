#pragma once

namespace gie
{
	class World;
	class Planner;
	class Goal;
}

typedef void ( *ImGuiDrawFunc )( gie::World&, gie::Planner&, gie::Goal&, gie::Guid );

struct ExampleParameters
{
	gie::World& world;
	gie::Planner& planner;
	gie::Goal& goal;
	ImGuiDrawFunc imGuiDrawFunc{ nullptr };
};