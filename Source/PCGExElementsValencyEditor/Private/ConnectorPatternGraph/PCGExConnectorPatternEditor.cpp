// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "ConnectorPatternGraph/PCGExConnectorPatternEditor.h"

#include "ConnectorPatternGraph/PCGExConnectorPatternGraph.h"
#include "ConnectorPatternGraph/PCGExConnectorPatternGraphNode.h"
#include "ConnectorPatternGraph/PCGExConnectorPatternGraphSchema.h"
#include "Core/PCGExConnectorPatternAsset.h"
#include "Core/PCGExValencyConnectorSet.h"

#include "GraphEditor.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Modules/ModuleManager.h"

const FName FPCGExConnectorPatternEditor::GraphTabId = FName("ConnectorPatternGraphTab");
const FName FPCGExConnectorPatternEditor::DetailsTabId = FName("ConnectorPatternDetailsTab");

#pragma region FPCGExConnectorPatternEditor

FPCGExConnectorPatternEditor::FPCGExConnectorPatternEditor()
{
}

FPCGExConnectorPatternEditor::~FPCGExConnectorPatternEditor()
{
	if (PatternGraph)
	{
		PatternGraph->RemoveOnGraphChangedHandler(OnGraphChangedHandle);
	}
}

void FPCGExConnectorPatternEditor::InitEditor(
	UPCGExConnectorPatternAsset* InAsset,
	const EToolkitMode::Type Mode,
	const TSharedPtr<IToolkitHost>& InitToolkitHost)
{
	EditedAsset = InAsset;

	EnsureGraphExists();

	const TArray<UObject*> ObjectsToEdit = {InAsset};

	// Layout: graph on left (filling), details on right
	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("PCGExConnectorPatternEditor_Layout_v1")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Horizontal)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.7f)
				->AddTab(GraphTabId, ETabState::OpenedTab)
				->SetForegroundTab(GraphTabId)
			)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.3f)
				->AddTab(DetailsTabId, ETabState::OpenedTab)
			)
		);

	InitAssetEditor(EToolkitMode::Standalone, InitToolkitHost, FName("PCGExConnectorPatternEditor"), Layout, true, true, ObjectsToEdit);

	// Toolbar
	TSharedRef<FExtender> ToolbarExtender = MakeShared<FExtender>();
	ToolbarExtender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateSP(this, &FPCGExConnectorPatternEditor::BuildEditorToolbar)
	);
	AddToolbarExtender(ToolbarExtender);
	RegenerateMenusAndToolbars();
}

void FPCGExConnectorPatternEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(GraphTabId, FOnSpawnTab::CreateSP(this, &FPCGExConnectorPatternEditor::SpawnGraphTab))
		.SetDisplayName(INVTEXT("Graph"))
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"));

	InTabManager->RegisterTabSpawner(DetailsTabId, FOnSpawnTab::CreateSP(this, &FPCGExConnectorPatternEditor::SpawnDetailsTab))
		.SetDisplayName(INVTEXT("Details"))
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));
}

void FPCGExConnectorPatternEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	InTabManager->UnregisterTabSpawner(GraphTabId);
	InTabManager->UnregisterTabSpawner(DetailsTabId);
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
}

TSharedRef<SDockTab> FPCGExConnectorPatternEditor::SpawnGraphTab(const FSpawnTabArgs& Args)
{
	check(PatternGraph);

	SGraphEditor::FGraphEditorEvents GraphEvents;
	GraphEvents.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(this, &FPCGExConnectorPatternEditor::OnSelectionChanged);

	GraphEditorWidget = SNew(SGraphEditor)
		.GraphToEdit(PatternGraph)
		.GraphEvents(GraphEvents)
		.IsEditable(true)
		.ShowGraphStateOverlay(false);

	return SNew(SDockTab)
		.TabRole(PanelTab)
		[
			GraphEditorWidget.ToSharedRef()
		];
}

TSharedRef<SDockTab> FPCGExConnectorPatternEditor::SpawnDetailsTab(const FSpawnTabArgs& Args)
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bUpdatesFromSelection = false;
	DetailsArgs.bLockable = false;
	DetailsArgs.bAllowSearch = true;
	DetailsArgs.bHideSelectionTip = true;

	DetailsView = PropertyModule.CreateDetailView(DetailsArgs);

	// Show asset properties by default
	if (UPCGExConnectorPatternAsset* Asset = EditedAsset.Get())
	{
		DetailsView->SetObject(Asset);
	}

	return SNew(SDockTab)
		.TabRole(PanelTab)
		[
			DetailsView.ToSharedRef()
		];
}

void FPCGExConnectorPatternEditor::EnsureGraphExists()
{
	UPCGExConnectorPatternAsset* Asset = EditedAsset.Get();
	if (!Asset) { return; }

#if WITH_EDITORONLY_DATA
	if (!Asset->PatternGraph)
	{
		Asset->PatternGraph = NewObject<UPCGExConnectorPatternGraph>(Asset, NAME_None, RF_Transactional);
		Asset->PatternGraph->Schema = UPCGExConnectorPatternGraphSchema::StaticClass();

		UPCGExConnectorPatternGraph* Graph = Cast<UPCGExConnectorPatternGraph>(Asset->PatternGraph);
		Graph->OwningAsset = Asset;

		// Build from existing pattern data (legacy/first-open)
		if (Asset->Patterns.Num() > 0)
		{
			Graph->BuildGraphFromAsset();
		}
		else
		{
			// Create default root node
			Graph->GetSchema()->CreateDefaultNodesForGraph(*Graph);
		}

		Asset->MarkPackageDirty();
	}

	PatternGraph = Cast<UPCGExConnectorPatternGraph>(Asset->PatternGraph);
	if (PatternGraph)
	{
		PatternGraph->OwningAsset = Asset;

		// Bind graph change handler
		OnGraphChangedHandle = PatternGraph->AddOnGraphChangedHandler(
			FOnGraphChanged::FDelegate::CreateSP(this, &FPCGExConnectorPatternEditor::OnGraphChanged));

		// Resolve pins from current ConnectorSet
		PatternGraph->RebuildAllNodePins();
	}
#endif
}

void FPCGExConnectorPatternEditor::OnGraphChanged(const FEdGraphEditAction& Action)
{
	if (PatternGraph)
	{
		PatternGraph->CompileGraphToAsset();
	}
}

void FPCGExConnectorPatternEditor::OnSelectionChanged(const TSet<UObject*>& NewSelection)
{
	if (!DetailsView.IsValid()) { return; }

	TArray<UObject*> SelectedObjects = NewSelection.Array();

	if (SelectedObjects.Num() == 1)
	{
		// Show selected node properties
		DetailsView->SetObjects(SelectedObjects);
	}
	else if (SelectedObjects.Num() > 1)
	{
		DetailsView->SetObjects(SelectedObjects);
	}
	else
	{
		// No selection — show asset properties
		if (UPCGExConnectorPatternAsset* Asset = EditedAsset.Get())
		{
			DetailsView->SetObject(Asset);
		}
	}
}

void FPCGExConnectorPatternEditor::BuildEditorToolbar(FToolBarBuilder& ToolbarBuilder)
{
	ToolbarBuilder.BeginSection("PatternActions");
	{
		// Compile button
		ToolbarBuilder.AddToolBarButton(
			FUIAction(
				FExecuteAction::CreateLambda(
					[this]()
					{
						if (PatternGraph)
						{
							PatternGraph->CompileGraphToAsset();
						}
					})
			),
			NAME_None,
			FText::FromString(TEXT("Compile")),
			INVTEXT("Recompile all patterns from graph topology"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "AssetEditor.Apply")
		);

		// Add Root button
		ToolbarBuilder.AddToolBarButton(
			FUIAction(
				FExecuteAction::CreateLambda(
					[this]()
					{
						if (!PatternGraph) { return; }

						const FScopedTransaction Transaction(INVTEXT("Add Pattern Root"));

						FGraphNodeCreator<UPCGExConnectorPatternGraphNode> NodeCreator(*PatternGraph);
						UPCGExConnectorPatternGraphNode* RootNode = NodeCreator.CreateNode(true);
						RootNode->NodePosX = 400;
						RootNode->NodePosY = 0;
						RootNode->bIsPatternRoot = true;
						RootNode->PatternName = FName("NewPattern");
						NodeCreator.Finalize();

						PatternGraph->CompileGraphToAsset();
						PatternGraph->NotifyGraphChanged();
					})
			),
			NAME_None,
			FText::FromString(TEXT("Add Root")),
			INVTEXT("Add a new pattern root node"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Plus")
		);
	}
	ToolbarBuilder.EndSection();
}

void FPCGExConnectorPatternEditor::SaveAsset_Execute()
{
	// Ensure compiled before save
	if (PatternGraph)
	{
		PatternGraph->CompileGraphToAsset();
	}
	FAssetEditorToolkit::SaveAsset_Execute();
}

#pragma endregion
