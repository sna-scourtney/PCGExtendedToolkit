﻿// Copyright 2025 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Paths/PCGExPaths.h"

#include "Data/PCGSplineData.h"
#include "Graph/Probes/PCGExProbeDirection.h"

#define LOCTEXT_NAMESPACE "PCGExPaths"
#define PCGEX_NAMESPACE PCGExPaths

bool FPCGExPathOutputDetails::Validate(const int32 NumPathPoints) const
{
	if (NumPathPoints < 2) { return false; }

	if (bRemoveSmallPaths && NumPathPoints < MinPointCount) { return false; }
	if (bRemoveLargePaths && NumPathPoints > MaxPointCount) { return false; }

	return true;
}

void FPCGExPathEdgeIntersectionDetails::Init()
{
	MaxDot = bUseMinAngle ? PCGExMath::DegreesToDot(MinAngle) : 1;
	MinDot = bUseMaxAngle ? PCGExMath::DegreesToDot(MaxAngle) : -1;
	ToleranceSquared = Tolerance * Tolerance;
}

void FPCGExPathFilterSettings::RegisterBuffersDependencies(FPCGExContext* InContext, PCGExData::FFacadePreloader& FacadePreloader) const
{
}

bool FPCGExPathFilterSettings::Init(FPCGExContext* InContext)
{
	return true;
}

FPCGExPathIntersectionDetails::FPCGExPathIntersectionDetails(const double InTolerance, const double InMinAngle, const double InMaxAngle)
{
	Tolerance = InTolerance;
	MinAngle = InMinAngle;
	MaxAngle = InMaxAngle;
	bUseMinAngle = InMinAngle > 0;
	bUseMaxAngle = InMaxAngle < 90;
}

namespace PCGExPaths
{
	void GetAxisForEntry(const FPCGExStaticMeshComponentDescriptor& InDescriptor, ESplineMeshAxis::Type& OutAxis, int32& OutC1, int32& OutC2, const EPCGExSplineMeshAxis Default)
	{
		EPCGExSplineMeshAxis Axis = InDescriptor.SplineMeshAxis;
		if (Axis == EPCGExSplineMeshAxis::Default) { Axis = Default; }

		switch (Axis)
		{
		default:
		case EPCGExSplineMeshAxis::Default:
		case EPCGExSplineMeshAxis::X:
			OutAxis = ESplineMeshAxis::X;
			OutC1 = 1;
			OutC2 = 2;
			break;
		case EPCGExSplineMeshAxis::Y:
			OutC1 = 0;
			OutC2 = 2;
			OutAxis = ESplineMeshAxis::Y;
			break;
		case EPCGExSplineMeshAxis::Z:
			OutC1 = 1;
			OutC2 = 0;
			OutAxis = ESplineMeshAxis::Z;
			break;
		}
	}

	void SetClosedLoop(UPCGData* InData, const bool bIsClosedLoop)
	{
		FPCGMetadataAttribute<bool>* Attr = PCGEx::TryGetMutableAttribute<bool>(InData, ClosedLoopIdentifier);
		if (!Attr) { Attr = InData->Metadata->CreateAttribute<bool>(ClosedLoopIdentifier, bIsClosedLoop, true, true); }
		PCGExDataHelpers::SetDataValue(Attr, bIsClosedLoop);
	}

	bool GetClosedLoop(const UPCGData* InData)
	{
		const FPCGMetadataAttribute<bool>* Attr = PCGEx::TryGetConstAttribute<bool>(InData, ClosedLoopIdentifier);
		return Attr ? PCGExDataHelpers::ReadDataValue(Attr) : false;
	}

	void FetchPrevNext(const TSharedPtr<PCGExData::FFacade>& InFacade, const TArray<PCGExMT::FScope>& Loops)
	{
		if (Loops.Num() <= 1) { return; }
		// Fetch necessary bits for prev/next data to be valid during parallel processing
		InFacade->Fetch(PCGExMT::FScope(0, 1));
		for (int i = 1; i < Loops.Num(); ++i) { InFacade->Fetch(PCGExMT::FScope(Loops[i - 1].End - 1, 2)); }
	}

	FPathMetrics::FPathMetrics(const FVector& InStart)
	{
		Add(InStart);
	}

	void FPathMetrics::Reset(const FVector& InStart)
	{
		Start = InStart;
		Last = InStart;
		Length = 0;
		Count = 1;
	}

	double FPathMetrics::Add(const FVector& Location)
	{
		if (Length == -1)
		{
			Reset(Location);
			return 0;
		}
		Length += DistToLast(Location);
		Last = Location;
		Count++;
		return Length;
	}

	double FPathMetrics::Add(const FVector& Location, double& OutDistToLast)
	{
		if (Length == -1)
		{
			Reset(Location);
			return 0;
		}
		OutDistToLast = DistToLast(Location);
		Length += OutDistToLast;
		Last = Location;
		Count++;
		return Length;
	}

	void FSplineMeshSegment::ComputeUpVectorFromTangents()
	{
		// Thanks Drakynfly @ https://www.reddit.com/r/unrealengine/comments/kqo6ez/usplinecomponent_twists_in_on_itself/

		const FVector A = Params.StartTangent.GetSafeNormal(0.001);
		const FVector B = Params.EndTangent.GetSafeNormal(0.001);
		if (const float Dot = A | B; Dot > 0.99 || Dot <= -0.99) { UpVector = FVector(A.Y, A.Z, A.X); }
		else { UpVector = A ^ B; }
	}

	void FSplineMeshSegment::ApplySettings(USplineMeshComponent* Component) const
	{
		check(Component)

		Component->SetStartAndEnd(Params.StartPos, Params.StartTangent, Params.EndPos, Params.EndTangent, false);

		Component->SetStartScale(Params.StartScale, false);
		if (bUseDegrees) { Component->SetStartRollDegrees(Params.StartRoll, false); }
		else { Component->SetStartRoll(Params.StartRoll, false); }

		Component->SetEndScale(Params.EndScale, false);
		if (bUseDegrees) { Component->SetEndRollDegrees(Params.EndRoll, false); }
		else { Component->SetEndRoll(Params.EndRoll, false); }

		Component->SetForwardAxis(SplineMeshAxis, false);
		Component->SetSplineUpDir(UpVector, false);

		Component->SetStartOffset(Params.StartOffset, false);
		Component->SetEndOffset(Params.EndOffset, false);

		Component->SplineParams.NaniteClusterBoundsScale = Params.NaniteClusterBoundsScale;

		Component->SplineBoundaryMin = 0;
		Component->SplineBoundaryMax = 0;

		Component->bSmoothInterpRollScale = bSmoothInterpRollScale;

		if (bSetMeshWithSettings) { ApplyMesh(Component); }
	}

	bool FSplineMeshSegment::ApplyMesh(USplineMeshComponent* Component) const
	{
		check(Component)
		UStaticMesh* StaticMesh = MeshEntry->Staging.TryGet<UStaticMesh>(); //LoadSynchronous<UStaticMesh>();

		if (!StaticMesh) { return false; }

		Component->SetStaticMesh(StaticMesh); // Will trigger a force rebuild, so put this last
		MeshEntry->ApplyMaterials(MaterialPick, Component);

		return true;
	}

	FPathEdge::FPathEdge(const int32 InStart, const int32 InEnd, const TConstPCGValueRange<FTransform>& Positions, const double Expansion)
		: Start(InStart), End(InEnd), AltStart(InStart)
	{
		Update(Positions, Expansion);
	}

	void FPathEdge::Update(const TConstPCGValueRange<FTransform>& Positions, const double Expansion)
	{
		FBox Box = FBox(ForceInit);
		Box += Positions[Start].GetLocation();
		Box += Positions[End].GetLocation();
		Bounds = Box.ExpandBy(Expansion);
		Dir = (Positions[End].GetLocation() - Positions[Start].GetLocation()).GetSafeNormal();
	}

	bool FPathEdge::ShareIndices(const FPathEdge& Other) const
	{
		return Start == Other.Start || Start == Other.End || End == Other.Start || End == Other.End;
	}

	bool FPathEdge::Connects(const FPathEdge& Other) const
	{
		return Start == Other.End || End == Other.Start;
	}

	bool FPathEdge::ShareIndices(const FPathEdge* Other) const
	{
		return Start == Other->Start || Start == Other->End || End == Other->Start || End == Other->End;
	}

	double FPathEdge::GetLength(const TConstPCGValueRange<FTransform>& Positions) const
	{
		return FVector::Dist(Positions[Start].GetLocation(), Positions[End].GetLocation());
	}

	void IPathEdgeExtra::ProcessingDone(const IPath* Path)
	{
	}

	void IPath::BuildEdgeOctree()
	{
		if (EdgeOctree) { return; }
		EdgeOctree = MakeUnique<FPathEdgeOctree>(Bounds.GetCenter(), Bounds.GetExtent().Length() + 10);
		for (FPathEdge& Edge : Edges)
		{
			if (!IsEdgeValid(Edge)) { continue; } // Skip zero-length edges
			EdgeOctree->AddElement(&Edge);        // Might be a problem if edges gets reallocated
		}
	}

	void IPath::BuildPartialEdgeOctree(const TArray<int8>& Filter)
	{
		if (EdgeOctree) { return; }
		EdgeOctree = MakeUnique<FPathEdgeOctree>(Bounds.GetCenter(), Bounds.GetExtent().Length() + 10);
		for (int i = 0; i < Edges.Num(); i++)
		{
			FPathEdge& Edge = Edges[i];
			if (!Filter[i] || !IsEdgeValid(Edge)) { continue; } // Skip filtered out & zero-length edges
			EdgeOctree->AddElement(&Edge);                      // Might be a problem if edges gets reallocated
		}
	}

	void IPath::BuildPartialEdgeOctree(const TBitArray<>& Filter)
	{
		if (EdgeOctree) { return; }
		EdgeOctree = MakeUnique<FPathEdgeOctree>(Bounds.GetCenter(), Bounds.GetExtent().Length() + 10);
		for (int i = 0; i < Edges.Num(); i++)
		{
			FPathEdge& Edge = Edges[i];
			if (!Filter[i] || !IsEdgeValid(Edge)) { continue; } // Skip filtered out & zero-length edges
			EdgeOctree->AddElement(&Edge);                      // Might be a problem if edges gets reallocated
		}
	}

	void IPath::UpdateConvexity(const int32 Index)
	{
		if (!bIsConvex) { return; }

		const int32 A = SafePointIndex(Index - 1);
		const int32 B = SafePointIndex(Index + 1);
		if (A == B)
		{
			bIsConvex = false;
			return;
		}

		PCGExMath::CheckConvex(
			Positions[A].GetLocation(), Positions[Index].GetLocation(), Positions[B].GetLocation(),
			bIsConvex, ConvexitySign);
	}

	void IPath::ComputeEdgeExtra(const int32 Index)
	{
		if (NumEdges == 1)
		{
			for (const TSharedPtr<IPathEdgeExtra>& Extra : Extras) { Extra->ProcessSingleEdge(this, Edges[0]); }
		}
		else
		{
			if (Index == 0) { for (const TSharedPtr<IPathEdgeExtra>& Extra : Extras) { Extra->ProcessFirstEdge(this, Edges[0]); } }
			else if (Index == LastEdge) { for (const TSharedPtr<IPathEdgeExtra>& Extra : Extras) { Extra->ProcessLastEdge(this, Edges[LastEdge]); } }
			else { for (const TSharedPtr<IPathEdgeExtra>& Extra : Extras) { Extra->ProcessEdge(this, Edges[Index]); } }
		}
	}

	void IPath::ExtraComputingDone()
	{
		for (const TSharedPtr<IPathEdgeExtra>& Extra : Extras) { Extra->ProcessingDone(this); }
		Extras.Empty(); // So we don't update them anymore
	}

	void IPath::ComputeAllEdgeExtra()
	{
		if (NumEdges == 1)
		{
			for (const TSharedPtr<IPathEdgeExtra>& Extra : Extras) { Extra->ProcessSingleEdge(this, Edges[0]); }
		}
		else
		{
			for (const TSharedPtr<IPathEdgeExtra>& Extra : Extras) { Extra->ProcessFirstEdge(this, Edges[0]); }
			for (int i = 1; i < LastEdge; i++) { for (const TSharedPtr<IPathEdgeExtra>& Extra : Extras) { Extra->ProcessEdge(this, Edges[i]); } }
			for (const TSharedPtr<IPathEdgeExtra>& Extra : Extras) { Extra->ProcessLastEdge(this, Edges[LastEdge]); }
		}

		ExtraComputingDone();
	}

	void IPath::BuildPath(const double Expansion)
	{
		if (bClosedLoop) { NumEdges = NumPoints; }
		else { NumEdges = LastIndex; }

		LastEdge = NumEdges - 1;

		Edges.SetNumUninitialized(NumEdges);

		for (int i = 0; i < NumEdges; i++)
		{
			const FPathEdge& E = (Edges[i] = FPathEdge(i, (i + 1) % NumPoints, Positions, Expansion));
			TotalLength += E.GetLength(Positions);
			Bounds += E.Bounds.GetBox();
		}
	}

#pragma region Edge extras

#pragma region FPathEdgeLength

	void FPathEdgeLength::ProcessEdge(const IPath* Path, const FPathEdge& Edge)
	{
		const double Dist = FVector::Dist(Path->GetPos_Unsafe(Edge.Start), Path->GetPos_Unsafe(Edge.End));
		GetMutable(Edge.Start) = Dist;
		TotalLength += Dist;
	}

	void FPathEdgeLength::ProcessingDone(const IPath* Path)
	{
		TPathEdgeExtra<double>::ProcessingDone(Path);
		CumulativeLength.SetNumUninitialized(Data.Num());
		CumulativeLength[0] = Data[0];
		for (int i = 1; i < Data.Num(); i++) { CumulativeLength[i] = CumulativeLength[i - 1] + Data[i]; }
	}

#pragma endregion

#pragma region FPathEdgeLength

	void FPathEdgeLengthSquared::ProcessEdge(const IPath* Path, const FPathEdge& Edge)
	{
		const double Dist = FVector::DistSquared(Path->GetPos_Unsafe(Edge.Start), Path->GetPos_Unsafe(Edge.End));
		GetMutable(Edge.Start) = Dist;
	}

#pragma endregion

#pragma region FPathEdgeNormal

	void FPathEdgeNormal::ProcessEdge(const IPath* Path, const FPathEdge& Edge)
	{
		GetMutable(Edge.Start) = FVector::CrossProduct(Up, Edge.Dir).GetSafeNormal();
	}

#pragma endregion

#pragma region FPathEdgeBinormal

	void FPathEdgeBinormal::ProcessFirstEdge(const IPath* Path, const FPathEdge& Edge)
	{
		if (Path->IsClosedLoop())
		{
			ProcessEdge(Path, Edge);
			return;
		}

		const FVector N = FVector::CrossProduct(Up, Edge.Dir).GetSafeNormal();
		Normals[Edge.Start] = N;
		GetMutable(Edge.Start) = N;
	}

	void FPathEdgeBinormal::ProcessEdge(const IPath* Path, const FPathEdge& Edge)
	{
		const FVector N = FVector::CrossProduct(Up, Edge.Dir).GetSafeNormal();
		Normals[Edge.Start] = N;

		const FVector A = Path->DirToPrevPoint(Edge.Start);
		FVector D = FQuat(FVector::CrossProduct(A, Edge.Dir).GetSafeNormal(), FMath::Acos(FVector::DotProduct(A, Edge.Dir)) * 0.5f).RotateVector(A);

		if (FVector::DotProduct(N, D) < 0.0f) { D *= -1; }

		GetMutable(Edge.Start) = D;
	}

#pragma endregion

#pragma region FPathEdgeAvgNormal

	void FPathEdgeAvgNormal::ProcessFirstEdge(const IPath* Path, const FPathEdge& Edge)
	{
		if (Path->IsClosedLoop())
		{
			ProcessEdge(Path, Edge);
			return;
		}

		GetMutable(Edge.Start) = FVector::CrossProduct(Up, Edge.Dir).GetSafeNormal();
	}

	void FPathEdgeAvgNormal::ProcessEdge(const IPath* Path, const FPathEdge& Edge)
	{
		const FVector A = FVector::CrossProduct(Up, Path->DirToPrevPoint(Edge.Start) * -1).GetSafeNormal();
		const FVector B = FVector::CrossProduct(Up, Edge.Dir).GetSafeNormal();
		GetMutable(Edge.Start) = FMath::Lerp(A, B, 0.5).GetSafeNormal();
	}

#pragma endregion

#pragma region FPathEdgeHalfAngle

	void FPathEdgeHalfAngle::ProcessFirstEdge(const IPath* Path, const FPathEdge& Edge)
	{
		if (Path->IsClosedLoop())
		{
			ProcessEdge(Path, Edge);
			return;
		}

		GetMutable(Edge.Start) = PI;
	}

	void FPathEdgeHalfAngle::ProcessEdge(const IPath* Path, const FPathEdge& Edge)
	{
		GetMutable(Edge.Start) = FMath::Acos(FVector::DotProduct(Path->DirToPrevPoint(Edge.Start), Edge.Dir));
	}

#pragma endregion

#pragma region FPathEdgeAngle

	void FPathEdgeFullAngle::ProcessFirstEdge(const IPath* Path, const FPathEdge& Edge)
	{
		if (Path->IsClosedLoop())
		{
			ProcessEdge(Path, Edge);
			return;
		}

		GetMutable(Edge.Start) = 0;
	}

	void FPathEdgeFullAngle::ProcessEdge(const IPath* Path, const FPathEdge& Edge)
	{
		GetMutable(Edge.Start) = PCGExMath::GetAngle(Path->DirToPrevPoint(Edge.Start) * -1, Edge.Dir);
	}

	TSharedPtr<IPath> MakePath(const UPCGBasePointData* InPointData, const double Expansion)
	{
		return MakePath(InPointData->GetConstTransformValueRange(), Expansion, GetClosedLoop(InPointData));
	}

	TSharedPtr<IPath> MakePath(const TConstPCGValueRange<FTransform>& InTransforms, const double Expansion, const bool bClosedLoop)
	{
		if (bClosedLoop)
		{
			PCGEX_MAKE_SHARED(P, TPath<true>, InTransforms, Expansion)
			return StaticCastSharedPtr<IPath>(P);
		}

		PCGEX_MAKE_SHARED(P, TPath<false>, InTransforms, Expansion)
		return StaticCastSharedPtr<IPath>(P);
	}

	double GetPathLength(const TSharedPtr<IPath>& InPath)
	{
		FPathMetrics Metrics(InPath->GetPos(0));
		for (int i = 0; i < InPath->NumPoints; i++) { Metrics.Add(InPath->GetPos(i)); }
		if (InPath->IsClosedLoop()) { Metrics.Add(InPath->GetPos(0)); }
		return Metrics.Length;
	}

	FTransform GetClosestTransform(const FPCGSplineStruct& InSpline, const FVector& InLocation, const bool bUseScale)
	{
		return InSpline.GetTransformAtSplineInputKey(InSpline.FindInputKeyClosestToWorldLocation(InLocation), ESplineCoordinateSpace::World, bUseScale);
	}

	FTransform GetClosestTransform(const TSharedPtr<const FPCGSplineStruct>& InSpline, const FVector& InLocation, const bool bUseScale)
	{
		return InSpline->GetTransformAtSplineInputKey(InSpline->FindInputKeyClosestToWorldLocation(InLocation), ESplineCoordinateSpace::World, bUseScale);
	}

	TSharedPtr<FPCGSplineStruct> MakeSplineFromPoints(const TConstPCGValueRange<FTransform>& InTransforms, const EPCGExSplinePointTypeRedux InPointType, const bool bClosedLoop, const bool bSmoothLinear)
	{
		const int32 NumPoints = InTransforms.Num();
		if (NumPoints < 2) { return nullptr; }

		TArray<FSplinePoint> SplinePoints;
		PCGEx::InitArray(SplinePoints, NumPoints);

		ESplinePointType::Type PointType = ESplinePointType::Linear;
		bool bComputeTangents = false;

		switch (InPointType)
		{
		case EPCGExSplinePointTypeRedux::Linear:
			if (bSmoothLinear)
			{
				PointType = ESplinePointType::CurveCustomTangent;
				bComputeTangents = true;
			}
			break;
		case EPCGExSplinePointTypeRedux::Curve:
			PointType = ESplinePointType::Curve;
			break;
		case EPCGExSplinePointTypeRedux::Constant:
			PointType = ESplinePointType::Constant;
			break;
		case EPCGExSplinePointTypeRedux::CurveClamped:
			PointType = ESplinePointType::CurveClamped;
			break;
		}

		if (bComputeTangents)
		{
			const int32 MaxIndex = NumPoints - 1;

			for (int i = 0; i < NumPoints; i++)
			{
				const FTransform TR = InTransforms[i];
				const FVector PtLoc = TR.GetLocation();

				const FVector PrevDir = (InTransforms[i == 0 ? bClosedLoop ? MaxIndex : 0 : i - 1].GetLocation() - PtLoc) * -1;
				const FVector NextDir = InTransforms[i == MaxIndex ? bClosedLoop ? 0 : i : i + 1].GetLocation() - PtLoc;
				const FVector Tangent = FMath::Lerp(PrevDir, NextDir, 0.5).GetSafeNormal() * 0.01;

				SplinePoints[i] = FSplinePoint(
					static_cast<float>(i),
					TR.GetLocation(),
					Tangent,
					Tangent,
					TR.GetRotation().Rotator(),
					TR.GetScale3D(),
					PointType);
			}
		}
		else
		{
			for (int i = 0; i < NumPoints; i++)
			{
				const FTransform TR = InTransforms[i];
				SplinePoints[i] = FSplinePoint(
					static_cast<float>(i),
					TR.GetLocation(),
					FVector::ZeroVector,
					FVector::ZeroVector,
					TR.GetRotation().Rotator(),
					TR.GetScale3D(),
					PointType);
			}
		}

		PCGEX_MAKE_SHARED(SplineStruct, FPCGSplineStruct)
		SplineStruct->Initialize(SplinePoints, bClosedLoop, FTransform::Identity);
		return SplineStruct;
	}

	TSharedPtr<FPCGSplineStruct> MakeSplineCopy(const FPCGSplineStruct& Original)
	{
		const int32 NumPoints = Original.GetNumberOfPoints();
		if (NumPoints < 1) { return nullptr; }

		const FInterpCurveVector& SplinePositions = Original.GetSplinePointsPosition();
		TArray<FSplinePoint> SplinePoints;

		auto GetPointType = [](const EInterpCurveMode Mode)-> ESplinePointType::Type
		{
			switch (Mode)
			{
			case CIM_Linear: return ESplinePointType::Type::Linear;
			case CIM_CurveAuto: return ESplinePointType::Type::Curve;
			case CIM_Constant: return ESplinePointType::Type::Constant;
			case CIM_CurveUser: return ESplinePointType::Type::CurveCustomTangent;
			case CIM_CurveAutoClamped: return ESplinePointType::Type::CurveClamped;
			default:
			case CIM_Unknown:
			case CIM_CurveBreak:
				return ESplinePointType::Type::Curve;
			}
		};

		PCGEx::InitArray(SplinePoints, NumPoints);

		for (int i = 0; i < NumPoints; i++)
		{
			const FTransform TR = Original.GetTransformAtSplineInputKey(i, ESplineCoordinateSpace::Local);
			SplinePoints[i] = FSplinePoint(
				static_cast<float>(i),
				TR.GetLocation(),
				SplinePositions.Points[i].ArriveTangent,
				SplinePositions.Points[i].LeaveTangent,
				TR.GetRotation().Rotator(),
				TR.GetScale3D(),
				GetPointType(SplinePositions.Points[i].InterpMode));
		}

		PCGEX_MAKE_SHARED(SplineStruct, FPCGSplineStruct)
		SplineStruct->Initialize(SplinePoints, Original.bClosedLoop, Original.GetTransform());
		return SplineStruct;
	}

#pragma endregion

#pragma endregion

	TSharedPtr<IPath> MakePolyPath(
		const TSharedPtr<PCGExData::FPointIO>& PointIO, const double Expansion,
		const FPCGExGeo2DProjectionDetails& Projection, const double ExpansionZ,
		const EPCGExWindingMutation WindingMutation)
	{
		if (GetClosedLoop(PointIO))
		{
			PCGEX_MAKE_SHARED(P, TPolyPath<true>, PointIO, Projection, Expansion, ExpansionZ, WindingMutation)
			return StaticCastSharedPtr<IPath>(P);
		}

		PCGEX_MAKE_SHARED(P, TPolyPath<false>, PointIO, Projection, Expansion, ExpansionZ, WindingMutation)
		return StaticCastSharedPtr<IPath>(P);
	}

	TSharedPtr<IPath> MakePolyPath(
		const TSharedPtr<PCGExData::FFacade>& Facade, const double Expansion,
		const FPCGExGeo2DProjectionDetails& Projection, const double ExpansionZ,
		const EPCGExWindingMutation WindingMutation)
	{
		if (GetClosedLoop(Facade->Source))
		{
			PCGEX_MAKE_SHARED(P, TPolyPath<true>, Facade, Projection, Expansion, ExpansionZ, WindingMutation)
			return StaticCastSharedPtr<IPath>(P);
		}

		PCGEX_MAKE_SHARED(P, TPolyPath<false>, Facade, Projection, Expansion, ExpansionZ, WindingMutation)
		return StaticCastSharedPtr<IPath>(P);
	}

	TSharedPtr<IPath> MakePolyPath(
		const UPCGSplineData* SplineData,
		const double Fidelity, const double Expansion,
		const FPCGExGeo2DProjectionDetails& Projection, double ExpansionZ,
		const EPCGExWindingMutation WindingMutation)
	{
		if (SplineData->IsClosed())
		{
			PCGEX_MAKE_SHARED(P, TPolyPath<true>, SplineData, Fidelity, Projection, Expansion, ExpansionZ, WindingMutation)
			return StaticCastSharedPtr<IPath>(P);
		}

		PCGEX_MAKE_SHARED(P, TPolyPath<false>, SplineData, Fidelity, Projection, Expansion, ExpansionZ, WindingMutation)
		return StaticCastSharedPtr<IPath>(P);
	}

	bool FPathEdgeCrossings::FindSplit(
		const TSharedPtr<IPath>& Path, const FPathEdge& Edge, const TSharedPtr<FPathEdgeLength>& PathLength,
		const TSharedPtr<IPath>& OtherPath, const FPathEdge& OtherEdge, const FPCGExPathEdgeIntersectionDetails& InIntersectionDetails)
	{
		if (!OtherPath->IsEdgeValid(OtherEdge)) { return false; }

		const FVector& A1 = Path->GetPos(Edge.Start);
		const FVector& B1 = Path->GetPos(Edge.End);
		const FVector& A2 = OtherPath->GetPos(OtherEdge.Start);
		const FVector& B2 = OtherPath->GetPos(OtherEdge.End);

		if (A1 == A2 || A1 == B2 || A2 == B1 || B2 == B1) { return false; }

		const FVector CrossDir = OtherEdge.Dir;

		if (InIntersectionDetails.bUseMinAngle || InIntersectionDetails.bUseMaxAngle)
		{
			if (!InIntersectionDetails.CheckDot(FMath::Abs(FVector::DotProduct((B1 - A1).GetSafeNormal(), CrossDir)))) { return false; }
		}

		FVector A;
		FVector B;
		FMath::SegmentDistToSegment(A1, B1, A2, B2, A, B);

		if (A == A1 || A == B1) { return false; } // On local point

		const double Dist = FVector::DistSquared(A, B);
		const bool bColloc = (B == A2 || B == B2); // On crossing point

		if (Dist >= InIntersectionDetails.ToleranceSquared) { return false; }

		Crossings.Emplace(
			PCGEx::H64(OtherEdge.Start, OtherPath->IOIndex),
			FMath::Lerp(A, B, 0.5),
			FVector::Dist(A1, A) / PathLength->Get(Edge),
			bColloc,
			CrossDir);

		return true;
	}

	bool FPathEdgeCrossings::RemoveCrossing(const int32 EdgeStartIndex, const int32 IOIndex)
	{
		const uint64 H = PCGEx::H64(EdgeStartIndex, IOIndex);
		for (int i = 0; i < Crossings.Num(); i++)
		{
			if (Crossings[i].Hash == H)
			{
				Crossings.RemoveAt(i);
				return true;
			}
		}
		return false;
	}

	bool FPathEdgeCrossings::RemoveCrossing(const TSharedPtr<IPath>& Path, const int32 EdgeStartIndex)
	{
		return RemoveCrossing(EdgeStartIndex, Path->IOIndex);
	}

	bool FPathEdgeCrossings::RemoveCrossing(const TSharedPtr<IPath>& Path, const FPathEdge& Edge)
	{
		return RemoveCrossing(Edge.Start, Path->IOIndex);
	}

	void FPathEdgeCrossings::SortByAlpha()
	{
		if (Crossings.Num() <= 1) { return; }
		Crossings.Sort([&](const FCrossing& A, const FCrossing& B) { return A.Alpha < B.Alpha; });
	}

	void FPathEdgeCrossings::SortByHash()
	{
		if (Crossings.Num() <= 1) { return; }
		Crossings.Sort([&](const FCrossing& A, const FCrossing& B) { return PCGEx::H64A(A.Hash) < PCGEx::H64A(B.Hash); });
	}
}


bool FPCGExSplineMeshMutationDetails::Init(const TSharedPtr<PCGExData::FFacade>& InDataFacade)
{
	if (!bPushStart && !bPushEnd) { return true; }

	if (bPushStart)
	{
		StartAmount = GetValueSettingStartPush();
		if (!StartAmount->Init(InDataFacade)) { return false; }
	}

	if (bPushEnd)
	{
		EndAmount = GetValueSettingEndPush();
		if (!EndAmount->Init(InDataFacade)) { return false; }
	}

	return true;
}

void FPCGExSplineMeshMutationDetails::Mutate(const int32 PointIndex, PCGExPaths::FSplineMeshSegment& InSegment)
{
	if (!bPushStart && !bPushEnd) { return; }

	const double Size = (bPushStart || bPushEnd) && (bRelativeStart || bRelativeEnd) ? FVector::Dist(InSegment.Params.StartPos, InSegment.Params.EndPos) : 1;
	const FVector StartDir = InSegment.Params.StartTangent.GetSafeNormal();
	const FVector EndDir = InSegment.Params.EndTangent.GetSafeNormal();

	if (bPushStart)
	{
		const double Factor = StartAmount->Read(PointIndex);
		const double Dist = bRelativeStart ? Size * Factor : Factor;

		InSegment.Params.StartPos -= StartDir * Dist;
		InSegment.Params.StartTangent = StartDir * (InSegment.Params.StartTangent.Size() + Dist * 3);
	}

	if (bPushEnd)
	{
		const double Factor = EndAmount->Read(PointIndex);
		const double Dist = bRelativeEnd ? Size * Factor : Factor;

		InSegment.Params.EndPos += EndDir * Dist;
		InSegment.Params.EndTangent = EndDir * (InSegment.Params.EndTangent.Size() + Dist * 3);
	}
}

#undef LOCTEXT_NAMESPACE
#undef PCGEX_NAMESPACE
