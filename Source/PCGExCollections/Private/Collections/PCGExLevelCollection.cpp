// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "Collections/PCGExLevelCollection.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetData.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Components/PrimitiveComponent.h"
#endif

#include "PCGExLog.h"

// Static-init type registration: TypeId=Level, parent=Base
PCGEX_REGISTER_COLLECTION_TYPE(Level, UPCGExLevelCollection, FPCGExLevelCollectionEntry, "Level Collection", Base)

#pragma region FPCGExLevelCollectionEntry

const UPCGExAssetCollection* FPCGExLevelCollectionEntry::GetSubCollectionPtr() const
{
	return SubCollection;
}

void FPCGExLevelCollectionEntry::ClearSubCollection()
{
	FPCGExAssetCollectionEntry::ClearSubCollection();
	SubCollection = nullptr;
}

bool FPCGExLevelCollectionEntry::Validate(const UPCGExAssetCollection* ParentCollection)
{
	if (!bIsSubCollection)
	{
		if (!Level.ToSoftObjectPath().IsValid() && ParentCollection->bDoNotIgnoreInvalidEntries) { return false; }
	}

	return FPCGExAssetCollectionEntry::Validate(ParentCollection);
}

void FPCGExLevelCollectionEntry::UpdateStaging(const UPCGExAssetCollection* OwningCollection, int32 InInternalIndex, bool bRecursive)
{
	ClearManagedSockets();

	if (bIsSubCollection)
	{
		FPCGExAssetCollectionEntry::UpdateStaging(OwningCollection, InInternalIndex, bRecursive);
		return;
	}

	Staging.Path = Level.ToSoftObjectPath();
	TSharedPtr<FStreamableHandle> Handle = PCGExHelpers::LoadBlocking_AnyThread(Level.ToSoftObjectPath());

#if WITH_EDITOR
	if (const UWorld* World = Level.Get())
	{
		// Compute combined bounds from the level's persistent level actors
		FBox CombinedBounds(ForceInit);

		if (World->PersistentLevel)
		{
			for (AActor* Actor : World->PersistentLevel->Actors)
			{
				if (!Actor) { continue; }

				// Check for primitive components that contribute to bounds
				TArray<UPrimitiveComponent*> PrimitiveComponents;
				Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);

				for (const UPrimitiveComponent* PrimComp : PrimitiveComponents)
				{
					if (!PrimComp || !PrimComp->IsRegistered()) { continue; }
					CombinedBounds += PrimComp->Bounds.GetBox();
				}
			}
		}

		Staging.Bounds = CombinedBounds.IsValid ? CombinedBounds : FBox(ForceInit);
	}
	else
	{
		Staging.Bounds = FBox(ForceInit);
	}
#else
	Staging.Bounds = FBox(ForceInit);
	UE_LOG(LogPCGEx, Error, TEXT("UpdateStaging called in non-editor context."));
#endif

	FPCGExAssetCollectionEntry::UpdateStaging(OwningCollection, InInternalIndex, bRecursive);
	PCGExHelpers::SafeReleaseHandle(Handle);
}

void FPCGExLevelCollectionEntry::SetAssetPath(const FSoftObjectPath& InPath)
{
	FPCGExAssetCollectionEntry::SetAssetPath(InPath);
	Level = TSoftObjectPtr<UWorld>(InPath);
}

#if WITH_EDITOR
void FPCGExLevelCollectionEntry::EDITOR_Sanitize()
{
	FPCGExAssetCollectionEntry::EDITOR_Sanitize();

	if (!bIsSubCollection)
	{
		InternalSubCollection = nullptr;
	}
	else
	{
		InternalSubCollection = SubCollection;
	}
}
#endif

#pragma endregion

#if WITH_EDITOR
void UPCGExLevelCollection::EDITOR_AddBrowserSelectionInternal(const TArray<FAssetData>& InAssetData)
{
	UPCGExAssetCollection::EDITOR_AddBrowserSelectionInternal(InAssetData);

	for (const FAssetData& SelectedAsset : InAssetData)
	{
		// Accept UWorld assets (.umap files)
		if (SelectedAsset.AssetClassPath != UWorld::StaticClass()->GetClassPathName())
		{
			continue;
		}

		TSoftObjectPtr<UWorld> LevelPtr(SelectedAsset.GetSoftObjectPath());

		// Dedup check
		bool bAlreadyExists = false;
		for (const FPCGExLevelCollectionEntry& ExistingEntry : Entries)
		{
			if (ExistingEntry.Level == LevelPtr)
			{
				bAlreadyExists = true;
				break;
			}
		}

		if (bAlreadyExists) { continue; }

		FPCGExLevelCollectionEntry Entry = FPCGExLevelCollectionEntry();
		Entry.Level = LevelPtr;

		Entries.Add(Entry);
	}
}
#endif
