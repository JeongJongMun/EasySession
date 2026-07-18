// Copyright Langerak. All Rights Reserved.

using UnrealBuildTool;

public class EasySession : ModuleRules
{
	public EasySession(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Opt-in to "Include-What-You-Use" as per the engine plugin standard.
		IWYUSupport = IWYUSupport.Full;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"CoreOnline",
			"Engine",
			"OnlineSubsystem",
			"OnlineSubsystemUtils",
			"DeveloperSettings"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"OnlineBase"
		});
	}
}
