﻿// Copyright 2025 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Graph/Pathfinding/PCGExPathfindingFindAllCells.h"

#define LOCTEXT_NAMESPACE "PCGExFindAllCells"
#define PCGEX_NAMESPACE FindAllCells

TArray<FPCGPinProperties> UPCGExFindAllCellsSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties = Super::InputPinProperties();
	PCGEX_PIN_POINT(PCGExTopology::SourceHolesLabel, "Omit cells that contain any points from this dataset", Normal, {})
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGExFindAllCellsSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_POINTS(PCGExPaths::OutputPathsLabel, "Contours", Required, {})
	//if (bOutputSeeds) { PCGEX_PIN_POINT(PCGExFindAllCells::OutputGoodSeedsLabel, "GoodSeeds", Required, {}) }
	return PinProperties;
}

PCGExData::EIOInit UPCGExFindAllCellsSettings::GetEdgeOutputInitMode() const { return PCGExData::EIOInit::NoInit; }
PCGExData::EIOInit UPCGExFindAllCellsSettings::GetMainOutputInitMode() const { return PCGExData::EIOInit::NoInit; }

PCGEX_INITIALIZE_ELEMENT(FindAllCells)

bool FPCGExFindAllCellsElement::Boot(FPCGExContext* InContext) const
{
	if (!FPCGExEdgesProcessorElement::Boot(InContext)) { return false; }

	PCGEX_CONTEXT_AND_SETTINGS(FindAllCells)

	PCGEX_FWD(Artifacts)
	if (!Context->Artifacts.Init(Context)) { return false; }

	Context->HolesFacade = PCGExData::TryGetSingleFacade(Context, PCGExTopology::SourceHolesLabel, false, false);
	if (Context->HolesFacade && Settings->ProjectionDetails.Method == EPCGExProjectionMethod::Normal)
	{
		Context->Holes = MakeShared<PCGExTopology::FHoles>(Context, Context->HolesFacade.ToSharedRef(), Settings->ProjectionDetails);
	}

	//const TSharedPtr<PCGExData::FPointIO> SeedsPoints = PCGExData::TryGetSingleInput(Context, PCGExGraph::SourceSeedsLabel, true);
	//if (!SeedsPoints) { return false; }

	Context->Paths = MakeShared<PCGExData::FPointIOCollection>(Context);
	Context->Paths->OutputPin = PCGExPaths::OutputPathsLabel;

	/*
	if (Settings->bOutputFilteredSeeds)
	{
		const int32 NumSeeds = SeedsPoints->GetNum();

		Context->SeedQuality.Init(false, NumSeeds);
		PCGEx::InitArray(Context->UdpatedSeedPoints, NumSeeds);

		Context->GoodSeeds = NewPointIO(SeedsPoints.ToSharedRef(), PCGExFindAllCells::OutputGoodSeedsLabel);
		Context->GoodSeeds->InitializeOutput(PCGExData::EIOInit::NewOutput);
		Context->GoodSeeds->GetOut()->GetMutablePoints().Reserve(NumSeeds);

		Context->BadSeeds = NewPointIO(SeedsPoints.ToSharedRef(), PCGExFindAllCells::OutputBadSeedsLabel);
		Context->BadSeeds->InitializeOutput(PCGExData::EIOInit::NewOutput);
		Context->BadSeeds->GetOut()->GetMutablePoints().Reserve(NumSeeds);
	}
	*/

	return true;
}

bool FPCGExFindAllCellsElement::ExecuteInternal(
	FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExFindAllCellsElement::Execute);

	PCGEX_CONTEXT_AND_SETTINGS(FindAllCells)
	PCGEX_EXECUTION_CHECK
	PCGEX_ON_INITIAL_EXECUTION
	{
		if (!Context->StartProcessingClusters<PCGExClusterMT::TBatch<PCGExFindAllCells::FProcessor>>(
			[](const TSharedPtr<PCGExData::FPointIOTaggedEntries>& Entries) { return true; },
			[&](const TSharedPtr<PCGExClusterMT::TBatch<PCGExFindAllCells::FProcessor>>& NewBatch)
			{
				// NewBatch->bRequiresWriteStep = true;
				NewBatch->SetProjectionDetails(Settings->ProjectionDetails);
			}))
		{
			return Context->CancelExecution(TEXT("Could not build any clusters."));
		}
	}

	PCGEX_CLUSTER_BATCH_PROCESSING(PCGEx::State_Done)

	// TODO : Output seeds?

	Context->Paths->StageOutputs();

	return Context->TryComplete();
}


namespace PCGExFindAllCells
{
	FProcessor::~FProcessor()
	{
	}

	bool FProcessor::Process(const TSharedPtr<PCGExMT::FTaskManager>& InAsyncManager)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGExFindAllCells::Process);

		if (!IProcessor::Process(InAsyncManager)) { return false; }

		if (Context->HolesFacade) { Holes = Context->Holes ? Context->Holes : MakeShared<PCGExTopology::FHoles>(Context, Context->HolesFacade.ToSharedRef(), ProjectionDetails); }

		CellsConstraints = MakeShared<PCGExTopology::FCellConstraints>(Settings->Constraints);
		if (Settings->Constraints.bOmitWrappingBounds) { CellsConstraints->BuildWrapperCell(Cluster.ToSharedRef(), *ProjectedVtxPositions.Get()); }
		CellsConstraints->Holes = Holes;

		StartParallelLoopForEdges(32); // Might be overkill low

		return true;
	}

	void FProcessor::ProcessEdges(const PCGExMT::FScope& Scope)
	{
		TArray<PCGExGraph::FEdge>& ClusterEdges = *Cluster->Edges;

		PCGEX_SCOPE_LOOP(Index)
		{
			PCGExGraph::FEdge& Edge = ClusterEdges[Index];

			//Check endpoints
			FindCell(*Cluster->GetEdgeStart(Edge), Edge);
			FindCell(*Cluster->GetEdgeEnd(Edge), Edge);
		}
	}

	bool FProcessor::FindCell(
		const PCGExCluster::FNode& Node,
		const PCGExGraph::FEdge& Edge,
		const bool bSkipBinary)
	{
		if (Node.IsBinary() && bSkipBinary)
		{
			FPlatformAtomics::InterlockedExchange(&LastBinary, Node.Index);
			return false;
		}

		if (!CellsConstraints->bKeepCellsWithLeaves && Node.IsLeaf()) { return false; }

		FPlatformAtomics::InterlockedAdd(&NumAttempts, 1);
		const TSharedPtr<PCGExTopology::FCell> Cell = MakeShared<PCGExTopology::FCell>(CellsConstraints.ToSharedRef());

		const PCGExTopology::ECellResult Result = Cell->BuildFromCluster(PCGExGraph::FLink(Node.Index, Edge.Index), Cluster.ToSharedRef(), *ProjectedVtxPositions.Get());
		if (Result != PCGExTopology::ECellResult::Success) { return false; }

		ProcessCell(Cell);

		return true;
	}

	void FProcessor::ProcessCell(const TSharedPtr<PCGExTopology::FCell>& InCell)
	{
		const TSharedPtr<PCGExData::FPointIO> PathIO = Context->Paths->Emplace_GetRef<UPCGPointArrayData>(VtxDataFacade->Source, PCGExData::EIOInit::New);
		if (!PathIO) { return; }

		PathIO->Tags->Reset();                                          // Tag forwarding handled by artifacts
		PathIO->IOIndex = Cluster->GetEdge(InCell->Seed.Edge)->IOIndex; // Enforce seed order for collection output-ish

		PCGExGraph::CleanupClusterData(PathIO);

		PCGEX_MAKE_SHARED(PathDataFacade, PCGExData::FFacade, PathIO.ToSharedRef())

		TArray<int32> ReadIndices;
		ReadIndices.SetNumUninitialized(InCell->Nodes.Num());

		for (int i = 0; i < InCell->Nodes.Num(); i++) { ReadIndices[i] = Cluster->GetNode(InCell->Nodes[i])->PointIndex; }
		PathIO->InheritPoints(ReadIndices, 0);
		InCell->PostProcessPoints(PathIO->GetOut());

		Context->Artifacts.Process(Cluster, PathDataFacade, InCell);
		PathDataFacade->WriteFastest(AsyncManager);

		// TODO : Create cell centroids here

		FPlatformAtomics::InterlockedIncrement(&OutputPathsNum);
	}

	void FProcessor::EnsureRoamingClosedLoopProcessing()
	{
		if (NumAttempts == 0 && LastBinary != -1)
		{
			PCGEX_MAKE_SHARED(Cell, PCGExTopology::FCell, CellsConstraints.ToSharedRef())
			PCGExGraph::FEdge& Edge = *Cluster->GetEdge(Cluster->GetNode(LastBinary)->Links[0].Edge);
			FindCell(*Cluster->GetEdgeStart(Edge), Edge, false);
		}
	}

	void FProcessor::OnEdgesProcessingComplete()
	{
		EnsureRoamingClosedLoopProcessing();
		TProcessor<FPCGExFindAllCellsContext, UPCGExFindAllCellsSettings>::OnEdgesProcessingComplete();
	}

	void FProcessor::CompleteWork()
	{
		if (!CellsConstraints->WrapperCell) { return; }
		if (OutputPathsNum == 0 && Settings->Constraints.bKeepWrapperIfSolePath) { ProcessCell(CellsConstraints->WrapperCell); }
	}

	void FProcessor::Cleanup()
	{
		TProcessor<FPCGExFindAllCellsContext, UPCGExFindAllCellsSettings>::Cleanup();
		CellsConstraints->Cleanup();
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
