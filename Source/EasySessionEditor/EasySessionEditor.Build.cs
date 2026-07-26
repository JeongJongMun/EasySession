// Copyright (c) 2026 Langerak. Licensed under the MIT License.

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
