// Copyright Langerak. All Rights Reserved.

using UnrealBuildTool;

public class EasySessionEditor : ModuleRules
{
	public EasySessionEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		IWYUSupport = IWYUSupport.Full;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"EasySession"
		});
	}
}
