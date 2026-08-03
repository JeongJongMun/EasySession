// Copyright (c) 2026 Langerak. Licensed under the MIT License.

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
			"DeveloperSettings"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// Public headers only expose OnlineSubsystem types (IOnlineSessionPtr,
			// FOnlineSessionSearchResult); the Online::Get* helpers from
			// OnlineSubsystemUtils are called in Private/ alone.
			"OnlineSubsystemUtils",
			"OnlineBase",
			"InputCore",
			"EnhancedInput"
		});
	}
}
