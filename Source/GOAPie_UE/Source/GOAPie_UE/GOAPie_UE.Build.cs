using UnrealBuildTool;

public class GOAPie_UE : ModuleRules
{
	public GOAPie_UE(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		// GOAPie core library is header-only — point to its include directory.
		// Adjust this path based on where the plugin is placed relative to the GOAPie source.
		// When used as a git submodule: "$(PluginDir)/../../GOAPie/include"
		// When headers are copied into the plugin: "$(PluginDir)/ThirdParty/GOAPie/include"
		string GOAPieIncludePath = System.IO.Path.Combine(ModuleDirectory, "..", "..", "..", "GOAPie", "include");
		if (System.IO.Directory.Exists(GOAPieIncludePath))
		{
			PublicIncludePaths.Add(GOAPieIncludePath);
		}
		else
		{
			// Fallback: assume headers are vendored inside the plugin
			string VendoredPath = System.IO.Path.Combine(ModuleDirectory, "ThirdParty", "GOAPie", "include");
			if (System.IO.Directory.Exists(VendoredPath))
			{
				PublicIncludePaths.Add(VendoredPath);
			}
		}

		// GLM is required by GOAPie for vec3 types
		string GLMIncludePath = System.IO.Path.Combine(ModuleDirectory, "..", "..", "..", "GLM");
		if (System.IO.Directory.Exists(GLMIncludePath))
		{
			PublicIncludePaths.Add(GLMIncludePath);
		}

		// Disable some warnings from third-party headers
		bEnableUndefinedIdentifierWarnings = false;
	}
}
