#pragma once

namespace gie
{
	class World;
	class Planner;
	class Goal;
}

typedef void ( *ImGuiDrawFunc )( gie::World&, gie::Planner&, gie::Goal&, gie::Guid );

// TODO: Rename ExampleParameters to ProjectParameters (frontend mapping)
struct ExampleParameters
{
gie::World& world;
gie::Planner& planner;
gie::Goal& goal;
ImGuiDrawFunc imGuiDrawFunc{ nullptr };
};
