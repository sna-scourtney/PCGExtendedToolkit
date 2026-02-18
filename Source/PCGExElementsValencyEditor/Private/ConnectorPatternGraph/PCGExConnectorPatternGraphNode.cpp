// Copyright 2026 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "ConnectorPatternGraph/PCGExConnectorPatternGraphNode.h"

#include "ConnectorPatternGraph/PCGExConnectorPatternGraph.h"
#include "Core/PCGExValencyConnectorSet.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"

#pragma region UPCGExConnectorPatternGraphNode

const FName UPCGExConnectorPatternGraphNode::AnyPinCategory = TEXT("AnyConnector");
const FName UPCGExConnectorPatternGraphNode::ConnectorPinCategory = TEXT("ConnectorType");

FName UPCGExConnectorPatternGraphNode::MakeOutputPinName(int32 TypeId, FName TypeName)
{
	return FName(*FString::Printf(TEXT("CT_%d_%s"), TypeId, *TypeName.ToString()));
}

FName UPCGExConnectorPatternGraphNode::MakeInputPinName(int32 TypeId, FName TypeName)
{
	return FName(*FString::Printf(TEXT("CT_%d_%s_In"), TypeId, *TypeName.ToString()));
}

int32 UPCGExConnectorPatternGraphNode::GetTypeIdFromPinName(FName PinName)
{
	// Parse "CT_{TypeId}_{TypeName}" or "CT_{TypeId}_{TypeName}_In"
	const FString PinStr = PinName.ToString();
	if (!PinStr.StartsWith(TEXT("CT_"))) { return 0; }

	// Extract the substring after "CT_", find the next underscore to isolate the TypeId
	const FString AfterPrefix = PinStr.Mid(3);
	int32 UnderscoreIdx = INDEX_NONE;
	if (AfterPrefix.FindChar(TEXT('_'), UnderscoreIdx))
	{
		return FCString::Atoi(*AfterPrefix.Left(UnderscoreIdx));
	}

	return 0;
}

void UPCGExConnectorPatternGraphNode::AllocateDefaultPins()
{
	// "Any" output pin (wildcard)
	FEdGraphPinType AnyPinType;
	AnyPinType.PinCategory = AnyPinCategory;

	CreatePin(EGPD_Output, AnyPinType, TEXT("AnyOut"))->PinFriendlyName = INVTEXT("Any");
	CreatePin(EGPD_Input, AnyPinType, TEXT("AnyIn"))->PinFriendlyName = INVTEXT("Any");

	// Recreate user-added connector pins
	for (const FPCGExConnectorPinEntry& PinEntry : ConnectorPins)
	{
		FEdGraphPinType ConnPinType;
		ConnPinType.PinCategory = ConnectorPinCategory;
		ConnPinType.PinSubCategory = PinEntry.StoredTypeName;

		const FText DisplayName = FText::FromName(PinEntry.StoredTypeName);
		CreatePin(EGPD_Output, ConnPinType, MakeOutputPinName(PinEntry.StoredTypeId, PinEntry.StoredTypeName))->PinFriendlyName = DisplayName;
		CreatePin(EGPD_Input, ConnPinType, MakeInputPinName(PinEntry.StoredTypeId, PinEntry.StoredTypeName))->PinFriendlyName = DisplayName;
	}
}

FText UPCGExConnectorPatternGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (bIsPatternRoot)
	{
		if (!PatternName.IsNone())
		{
			return FText::FromString(FString::Printf(TEXT("[Root] %s"), *PatternName.ToString()));
		}
		return INVTEXT("[Root] Pattern");
	}

	// Show module names summary
	if (ModuleNames.Num() > 0)
	{
		FString Summary;
		for (int32 i = 0; i < FMath::Min(ModuleNames.Num(), 2); i++)
		{
			if (i > 0) { Summary += TEXT(", "); }
			Summary += ModuleNames[i].ToString();
		}
		if (ModuleNames.Num() > 2)
		{
			Summary += FString::Printf(TEXT(" (+%d)"), ModuleNames.Num() - 2);
		}
		return FText::FromString(Summary);
	}

	return INVTEXT("Entry (Any)");
}

FLinearColor UPCGExConnectorPatternGraphNode::GetNodeTitleColor() const
{
	if (bIsPatternRoot)
	{
		return FLinearColor(0.2f, 0.6f, 1.0f); // Blue for root
	}

	if (!bIsActive)
	{
		return FLinearColor(0.4f, 0.4f, 0.4f); // Gray for inactive/constraint-only
	}

	if (ModuleNames.IsEmpty())
	{
		return FLinearColor(0.6f, 0.6f, 0.6f); // Light gray for wildcard
	}

	return FLinearColor(0.2f, 0.8f, 0.3f); // Green for active with modules
}

void UPCGExConnectorPatternGraphNode::AddConnectorPin(int32 TypeId, FName TypeName)
{
	// Check if already exists
	for (const FPCGExConnectorPinEntry& Existing : ConnectorPins)
	{
		if (Existing.StoredTypeId == TypeId) { return; }
	}

	FPCGExConnectorPinEntry& NewEntry = ConnectorPins.Emplace_GetRef();
	NewEntry.StoredTypeId = TypeId;
	NewEntry.StoredTypeName = TypeName;

	// Create actual pins
	FEdGraphPinType ConnPinType;
	ConnPinType.PinCategory = ConnectorPinCategory;
	ConnPinType.PinSubCategory = TypeName;

	const FText DisplayName = FText::FromName(TypeName);
	CreatePin(EGPD_Output, ConnPinType, MakeOutputPinName(TypeId, TypeName))->PinFriendlyName = DisplayName;
	CreatePin(EGPD_Input, ConnPinType, MakeInputPinName(TypeId, TypeName))->PinFriendlyName = DisplayName;
}

void UPCGExConnectorPatternGraphNode::RemoveConnectorPin(int32 TypeId)
{
	int32 RemoveIdx = INDEX_NONE;
	for (int32 i = 0; i < ConnectorPins.Num(); i++)
	{
		if (ConnectorPins[i].StoredTypeId == TypeId)
		{
			RemoveIdx = i;
			break;
		}
	}

	if (RemoveIdx == INDEX_NONE) { return; }

	const FPCGExConnectorPinEntry& Entry = ConnectorPins[RemoveIdx];
	const FName OutName = MakeOutputPinName(Entry.StoredTypeId, Entry.StoredTypeName);
	const FName InName = MakeInputPinName(Entry.StoredTypeId, Entry.StoredTypeName);

	// Break links and remove pins
	if (UEdGraphPin* OutPin = FindPin(OutName, EGPD_Output))
	{
		OutPin->BreakAllPinLinks();
		RemovePin(OutPin);
	}
	if (UEdGraphPin* InPin = FindPin(InName, EGPD_Input))
	{
		InPin->BreakAllPinLinks();
		RemovePin(InPin);
	}

	ConnectorPins.RemoveAt(RemoveIdx);
}

void UPCGExConnectorPatternGraphNode::ResolveConnectorPins(const UPCGExValencyConnectorSet* InConnectorSet)
{
	if (!InConnectorSet) { return; }

	for (FPCGExConnectorPinEntry& PinEntry : ConnectorPins)
	{
		const int32 TypeIdx = InConnectorSet->FindConnectorTypeIndexById(PinEntry.StoredTypeId);
		if (TypeIdx != INDEX_NONE)
		{
			PinEntry.StoredTypeName = InConnectorSet->ConnectorTypes[TypeIdx].ConnectorType;

			// Update pin subcategory to match current name
			const FName OutName = MakeOutputPinName(PinEntry.StoredTypeId, PinEntry.StoredTypeName);
			const FName InName = MakeInputPinName(PinEntry.StoredTypeId, PinEntry.StoredTypeName);

			// Find and update existing pins
			for (UEdGraphPin* Pin : Pins)
			{
				if (Pin->PinType.PinCategory == ConnectorPinCategory)
				{
					// Check if this pin's TypeId matches
					const int32 PinTypeId = GetTypeIdFromPinName(Pin->PinName);
					if (PinTypeId == PinEntry.StoredTypeId)
					{
						Pin->PinType.PinSubCategory = PinEntry.StoredTypeName;
					}
				}
			}
		}
	}
}

bool UPCGExConnectorPatternGraphNode::HasConnectorPin(int32 TypeId) const
{
	for (const FPCGExConnectorPinEntry& Entry : ConnectorPins)
	{
		if (Entry.StoredTypeId == TypeId) { return true; }
	}
	return false;
}

UPCGExConnectorPatternGraph* UPCGExConnectorPatternGraphNode::GetPatternGraph() const
{
	return Cast<UPCGExConnectorPatternGraph>(GetGraph());
}

void UPCGExConnectorPatternGraphNode::AutowireNewNode(UEdGraphPin* FromPin)
{
	if (!FromPin) { return; }

	// Try to find a matching pin type on this node
	const EEdGraphPinDirection TargetDirection = (FromPin->Direction == EGPD_Output) ? EGPD_Input : EGPD_Output;

	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin->Direction != TargetDirection) { continue; }

		// Match by category and subcategory
		if (Pin->PinType.PinCategory == FromPin->PinType.PinCategory &&
			Pin->PinType.PinSubCategory == FromPin->PinType.PinSubCategory)
		{
			if (GetSchema()->TryCreateConnection(FromPin, Pin))
			{
				return;
			}
		}
	}

	// Fall back to "Any" pin
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin->Direction != TargetDirection) { continue; }
		if (Pin->PinType.PinCategory == AnyPinCategory)
		{
			GetSchema()->TryCreateConnection(FromPin, Pin);
			return;
		}
	}
}

#if WITH_EDITOR
void UPCGExConnectorPatternGraphNode::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (UPCGExConnectorPatternGraph* PatternGraph = GetPatternGraph())
	{
		PatternGraph->CompileGraphToAsset();
	}
}
#endif

#pragma endregion
