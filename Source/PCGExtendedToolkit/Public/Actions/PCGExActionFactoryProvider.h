﻿// Copyright 2025 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "PCGExFactoryProvider.h"
#include "PCGExOperation.h"
#include "Data/PCGExPointFilter.h"


#include "PCGExActionFactoryProvider.generated.h"

#define PCGEX_BITMASK_TRANSMUTE_CREATE_FACTORY(_NAME, _BODY) \
	UPCGExFactoryData* UPCGEx##_NAME##ProviderSettings::CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const{ \
	UPCGEx##_NAME##Factory* NewFactory = NewObject<UPCGEx##_NAME##Factory>(); _BODY \
	if(!Super::CreateFactory(InContext, NewFactory)){ InContext->ManagedObjects->Destroy(NewFactory); return nullptr; }\
	return NewFactory; }

#define PCGEX_BITMASK_TRANSMUTE_CREATE_OPERATION(_NAME, _BODY) \
	TSharedPtr<FPCGExActionOperation> UPCGEx##_NAME##Factory::CreateOperation(FPCGExContext* InContext) const{ \
	PCGEX_FACTORY_NEW_OPERATION(_NAME##Operation)\
	NewOperation->TypedFactory = const_cast<UPCGEx##_NAME##Factory*>(this); \
	NewOperation->Factory = NewOperation->TypedFactory; \
	_BODY \
	return NewOperation;}

class UPCGExActionFactoryData;

namespace PCGExActions
{
	const FName SourceConditionsFilterLabel = TEXT("Conditions");
	const FName SourceActionsLabel = TEXT("Actions");
	const FName SourceDefaultsLabel = TEXT("Default values");
	const FName OutputActionLabel = TEXT("Action");
}


/**
 * 
 */
class PCGEXTENDEDTOOLKIT_API FPCGExActionOperation : public FPCGExOperation
{
public:
	UPCGExActionFactoryData* Factory = nullptr;

	virtual bool PrepareForData(FPCGExContext* InContext, const TSharedPtr<PCGExData::FFacade>& InPointDataFacade);
	virtual void ProcessPoint(int32 Index);

	virtual void OnMatchSuccess(int32 Index);
	virtual void OnMatchFail(int32 Index);

protected:
	TSharedPtr<PCGExPointFilter::FManager> FilterManager;
};

UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Data")
class PCGEXTENDEDTOOLKIT_API UPCGExActionFactoryData : public UPCGExFactoryData
{
	GENERATED_BODY()

public:
	TSharedPtr<PCGEx::FAttributesInfos> CheckSuccessInfos;
	TSharedPtr<PCGEx::FAttributesInfos> CheckFailInfos;

	UPROPERTY(meta=(PCG_NotOverridable))
	TArray<TObjectPtr<const UPCGExFilterFactoryData>> FilterFactories;

	virtual PCGExFactories::EType GetFactoryType() const override { return PCGExFactories::EType::Action; }
	virtual TSharedPtr<FPCGExActionOperation> CreateOperation(FPCGExContext* InContext) const;

	virtual bool Boot(FPCGContext* InContext);
	virtual bool AppendAndValidate(const TSharedPtr<PCGEx::FAttributesInfos>& InInfos, FString& OutMessage) const;

	virtual void BeginDestroy() override;
};

UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural), Category="PCGEx|Action")
class PCGEXTENDEDTOOLKIT_API UPCGExActionProviderSettings : public UPCGExFactoryProviderSettings
{
	GENERATED_BODY()

public:
	//~Begin UPCGSettings
#if WITH_EDITOR
	PCGEX_NODE_INFOS(ActionAbstract, "Action : Abstract", "Abstract Action Provider.")
	virtual FLinearColor GetNodeTitleColor() const override { return GetDefault<UPCGExGlobalSettings>()->NodeColorMisc; }
#endif

protected:
	virtual bool GetRequiresFilters() const { return true; }
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	//~End UPCGSettings

public:
	virtual FName GetMainOutputPin() const override { return PCGExActions::OutputActionLabel; }
	virtual UPCGExFactoryData* CreateFactory(FPCGExContext* InContext, UPCGExFactoryData* InFactory) const override;

#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
#endif

	/** Priority for transmutation order. Higher values are processed last. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(PCG_Overridable, DisplayPriority=-1), AdvancedDisplay)
	int32 Priority = 0;
};
