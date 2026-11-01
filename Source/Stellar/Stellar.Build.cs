using UnrealBuildTool;

public class Stellar : ModuleRules
{
	public Stellar(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "ProceduralMeshComponent"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { "RHI" });
	}
}
