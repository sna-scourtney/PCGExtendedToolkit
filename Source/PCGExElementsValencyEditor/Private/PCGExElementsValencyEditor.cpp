// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExElementsValencyEditor.h"

#include "AssetTypeActions_Base.h"
#include "PCGExAssetTypesMacros.h"
#include "PropertyEditorModule.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "EditorMode/PCGExValencyEditorModeToolkit.h"
#include "EditorMode/PCGExValencyCageConnectorVisualizer.h"
#include "EditorMode/PCGExConstraintVisualizer.h"
#include "EditorMode/Constraints/PCGExConstraintVis_AngularRange.h"
#include "EditorMode/Constraints/PCGExConstraintVis_SurfaceOffset.h"
#include "EditorMode/Constraints/PCGExConstraintVis_VolumeOffset.h"
#include "EditorMode/Constraints/PCGExConstraintVis_HemisphereOffset.h"
#include "EditorMode/Constraints/PCGExConstraintVis_Preset.h"
#include "EditorMode/Constraints/PCGExConstraintVis_Branch.h"
#include "EditorMode/Constraints/PCGExConstraintVis_ContextCondition.h"
#include "EditorMode/Constraints/PCGExConstraintVis_ConicRange.h"
#include "EditorMode/Constraints/PCGExConstraintVis_ArcSurface.h"
#include "EditorMode/Constraints/PCGExConstraintVis_ArcRepeat.h"
#include "EditorMode/Constraints/PCGExConstraintVis_SnapToGrid.h"
#include "EditorMode/Constraints/PCGExConstraintVis_Probability.h"
#include "EditorMode/Constraints/PCGExConstraintVis_ScaleRamp.h"
#include "EditorMode/Constraints/PCGExConstraintVis_AlignToWorld.h"
#include "EditorMode/Constraints/PCGExConstraintVis_Lattice.h"
#include "EditorMode/Constraints/PCGExConstraintVis_Spiral.h"
#include "Growth/Constraints/PCGExConstraint_AngularRange.h"
#include "Growth/Constraints/PCGExConstraint_SurfaceOffset.h"
#include "Growth/Constraints/PCGExConstraint_VolumeOffset.h"
#include "Growth/Constraints/PCGExConstraint_HemisphereOffset.h"
#include "Growth/Constraints/PCGExConstraintPreset.h"
#include "Growth/Constraints/PCGExConstraint_Branch.h"
#include "Growth/Constraints/PCGExConstraint_ContextCondition.h"
#include "Growth/Constraints/PCGExConstraint_ConicRange.h"
#include "Growth/Constraints/PCGExConstraint_ArcSurface.h"
#include "Growth/Constraints/PCGExConstraint_ArcRepeat.h"
#include "Growth/Constraints/PCGExConstraint_SnapToGrid.h"
#include "Growth/Constraints/PCGExConstraint_Probability.h"
#include "Growth/Constraints/PCGExConstraint_ScaleRamp.h"
#include "Growth/Constraints/PCGExConstraint_AlignToWorld.h"
#include "Growth/Constraints/PCGExConstraint_Lattice.h"
#include "Growth/Constraints/PCGExConstraint_Spiral.h"
#include "Components/PCGExValencyCageConnectorComponent.h"
#include "Core/PCGExConnectorPatternAsset.h"
#include "Details/PCGExPropertyOutputConfigCustomization.h"
#include "Details/PCGExValencyConnectorCompatibilityCustomization.h"

void FPCGExElementsValencyEditorModule::StartupModule()
{
	IPCGExEditorModuleInterface::StartupModule();

	// Register editor mode command bindings
	FValencyEditorCommands::Register();

	// Register connector component visualizer
	if (GUnrealEd)
	{
		GUnrealEd->RegisterComponentVisualizer(
			UPCGExValencyCageConnectorComponent::StaticClass()->GetFName(),
			MakeShareable(new FPCGExValencyCageConnectorVisualizer()));
	}

	// Register constraint visualizers
	{
		FConstraintVisualizerRegistry& Registry = FConstraintVisualizerRegistry::Get();
		Registry.Register<FPCGExConstraint_AngularRange, FAngularRangeVisualizer>();
		Registry.Register<FPCGExConstraint_SurfaceOffset, FSurfaceOffsetVisualizer>();
		Registry.Register<FPCGExConstraint_VolumeOffset, FVolumeOffsetVisualizer>();
		Registry.Register<FPCGExConstraint_HemisphereOffset, FHemisphereOffsetVisualizer>();
		Registry.Register<FPCGExConstraint_Preset, FPresetVisualizer>();
		Registry.Register<FPCGExConstraint_Branch, FBranchVisualizer>();
		Registry.Register<FPCGExConstraint_ContextCondition, FContextConditionVisualizer>();
		Registry.Register<FPCGExConstraint_ConicRange, FConicRangeVisualizer>();
		Registry.Register<FPCGExConstraint_ArcSurface, FArcSurfaceVisualizer>();
		Registry.Register<FPCGExConstraint_ArcRepeat, FArcRepeatVisualizer>();
		Registry.Register<FPCGExConstraint_SnapToGrid, FSnapToGridVisualizer>();
		Registry.Register<FPCGExConstraint_Probability, FProbabilityVisualizer>();
		Registry.Register<FPCGExConstraint_ScaleRamp, FScaleRampVisualizer>();
		Registry.Register<FPCGExConstraint_AlignToWorld, FAlignToWorldVisualizer>();
		Registry.Register<FPCGExConstraint_Lattice, FLatticeVisualizer>();
		Registry.Register<FPCGExConstraint_Spiral, FSpiralVisualizer>();
	}

	// Asset type actions
	PCGEX_ASSET_TYPE_ACTION_BASIC(ConnectorPattern, "PCGEx Valency | Connector Pattern", UPCGExConnectorPatternAsset, FColor(180, 100, 220), EAssetTypeCategories::Misc)

	// Property customizations
	PCGEX_REGISTER_CUSTO_START
	PCGEX_REGISTER_CUSTO("PCGExValencyPropertyOutputConfig", FPCGExPropertyOutputConfigCustomization)
	PCGEX_REGISTER_CUSTO("PCGExValencyConnectorEntry", FPCGExValencyConnectorEntryCustomization)
}

void FPCGExElementsValencyEditorModule::ShutdownModule()
{
	// Unregister connector component visualizer
	if (GUnrealEd)
	{
		GUnrealEd->UnregisterComponentVisualizer(
			UPCGExValencyCageConnectorComponent::StaticClass()->GetFName());
	}

	// Unregister editor mode command bindings
	FValencyEditorCommands::Unregister();

	IPCGExEditorModuleInterface::ShutdownModule();
}

PCGEX_IMPLEMENT_MODULE(FPCGExElementsValencyEditorModule, PCGExElementsValencyEditor)
