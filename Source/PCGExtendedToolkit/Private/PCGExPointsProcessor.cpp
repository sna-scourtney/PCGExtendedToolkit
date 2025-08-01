﻿// Copyright 2025 Timothé Lapetite and contributors
// Released under the MIT license https://opensource.org/license/MIT/

#include "PCGExPointsProcessor.h"

#include "Styling/SlateStyle.h"
#include "PCGPin.h"
#include "Data/PCGExPointFilter.h"


#include "Helpers/PCGSettingsHelpers.h"
#include "Misc/PCGExMergePoints.h"

#define LOCTEXT_NAMESPACE "PCGExGraphSettings"

#pragma region UPCGSettings interface

#if WITH_EDITOR

#ifndef PCGEX_CUSTOM_PIN_DECL
#define PCGEX_CUSTOM_PIN_DECL
#define PCGEX_CUSTOM_PIN_ICON(_LABEL, _ICON, _TOOLTIP) if(PinLabel == _LABEL){ OutExtraIcon = TEXT("PCGEx.Pin." # _ICON); OutTooltip = FTEXT(_TOOLTIP); return true; }
#endif

bool UPCGExPointsProcessorSettings::GetPinExtraIcon(const UPCGPin* InPin, FName& OutExtraIcon, FText& OutTooltip) const
{
	return GetDefault<UPCGExGlobalSettings>()->GetPinExtraIcon(InPin, OutExtraIcon, OutTooltip, false);
}

bool UPCGExPointsProcessorSettings::IsPinUsedByNodeExecution(const UPCGPin* InPin) const
{
	if (GetDefault<UPCGExGlobalSettings>()->bToneDownOptionalPins && !InPin->Properties.IsRequiredPin() && !InPin->IsOutputPin()) { return InPin->EdgeCount() > 0; }
	return Super::IsPinUsedByNodeExecution(InPin);
}


#endif

TArray<FPCGPinProperties> UPCGExPointsProcessorSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;

	if (!IsInputless())
	{
		if (!GetIsMainTransactional())
		{
			if (GetMainAcceptMultipleData()) { PCGEX_PIN_POINTS(GetMainInputPin(), "The point data to be processed.", Required, {}) }
			else { PCGEX_PIN_POINT(GetMainInputPin(), "The point data to be processed.", Required, {}) }
		}
		else
		{
			if (GetMainAcceptMultipleData()) { PCGEX_PIN_ANY(GetMainInputPin(), "The data to be processed.", Required, {}) }
			else { PCGEX_PIN_ANY(GetMainInputPin(), "The data to be processed.", Required, {}) }
		}
	}

	if (SupportsPointFilters())
	{
		if (RequiresPointFilters()) { PCGEX_PIN_FACTORIES(GetPointFilterPin(), GetPointFilterTooltip(), Required, {}) }
		else { PCGEX_PIN_FACTORIES(GetPointFilterPin(), GetPointFilterTooltip(), Normal, {}) }
	}

	return PinProperties;
}


TArray<FPCGPinProperties> UPCGExPointsProcessorSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PCGEX_PIN_POINTS(GetMainOutputPin(), "The processed input.", Normal, {})
	return PinProperties;
}

PCGExData::EIOInit UPCGExPointsProcessorSettings::GetMainOutputInitMode() const { return PCGExData::EIOInit::NoInit; }

#if WITH_EDITOR
void UPCGExPointsProcessorSettings::EDITOR_OpenNodeDocumentation() const
{
	const FString URL = PCGEx::META_PCGExDocNodeLibraryBaseURL + GetClass()->GetMetaData(*PCGEx::META_PCGExDocURL);
	FPlatformProcess::LaunchURL(*URL, nullptr, nullptr);
}
#endif

bool UPCGExPointsProcessorSettings::ShouldCache() const
{
	if (!IsCacheable()) { return false; }
	PCGEX_GET_OPTION_STATE(CacheData, bDefaultCacheNodeOutput)
}

bool UPCGExPointsProcessorSettings::WantsScopedAttributeGet() const
{
	PCGEX_GET_OPTION_STATE(ScopedAttributeGet, bDefaultScopedAttributeGet)
}

FPCGExPointsProcessorContext::~FPCGExPointsProcessorContext()
{
	PCGEX_TERMINATE_ASYNC

	for (UPCGExInstancedFactory* Op : ProcessorOperations)
	{
		if (InternalOperations.Contains(Op))
		{
			ManagedObjects->Destroy(Op);
		}
	}

	if (MainBatch) { MainBatch->Cleanup(); }
	MainBatch.Reset();
}

bool FPCGExPointsProcessorContext::AdvancePointsIO(const bool bCleanupKeys)
{
	if (bCleanupKeys && CurrentIO) { CurrentIO->ClearCachedKeys(); }

	if (MainPoints->Pairs.IsValidIndex(++CurrentPointIOIndex))
	{
		CurrentIO = MainPoints->Pairs[CurrentPointIOIndex];
		return true;
	}

	CurrentIO = nullptr;
	return false;
}


#pragma endregion

bool FPCGExPointsProcessorContext::ProcessPointsBatch(const PCGEx::ContextState NextStateId, const bool bIsNextStateAsync)
{
	if (!bBatchProcessingEnabled) { return true; }

	PCGEX_ON_ASYNC_STATE_READY_INTERNAL(PCGExPointsMT::MTState_PointsProcessing)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExPointsProcessorContext::ProcessPointsBatch::InitialProcessingDone);
		BatchProcessing_InitialProcessingDone();

		SetAsyncState(PCGExPointsMT::MTState_PointsCompletingWork);
		if (!MainBatch->bSkipCompletion)
		{
			//GetAsyncManager();
			PCGEX_LAUNCH(
				PCGExMT::FDeferredCallbackTask,
				[WeakHandle = GetOrCreateHandle()]()
				{
				PCGEX_SHARED_TCONTEXT_VOID(MergePoints, WeakHandle)
				SharedContext.Get()->MainBatch->CompleteWork();
				});
			return false;
		}
	}

	PCGEX_ON_ASYNC_STATE_READY_INTERNAL(PCGExPointsMT::MTState_PointsCompletingWork)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExPointsProcessorContext::ProcessPointsBatch::WorkComplete);
		BatchProcessing_WorkComplete();

		if (MainBatch->bRequiresWriteStep)
		{
			SetAsyncState(PCGExPointsMT::MTState_PointsWriting);
			//GetAsyncManager();
			PCGEX_LAUNCH(
				PCGExMT::FDeferredCallbackTask,
				[WeakHandle = GetOrCreateHandle()]()
				{
				PCGEX_SHARED_TCONTEXT_VOID(MergePoints, WeakHandle)
				SharedContext.Get()->MainBatch->Write();
				});
			return false;
		}

		bBatchProcessingEnabled = false;
		if (NextStateId == PCGEx::State_Done) { Done(); }
		if (bIsNextStateAsync) { SetAsyncState(NextStateId); }
		else { SetState(NextStateId); }
	}

	PCGEX_ON_ASYNC_STATE_READY_INTERNAL(PCGExPointsMT::MTState_PointsWriting)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExPointsProcessorContext::ProcessPointsBatch::WritingDone);
		BatchProcessing_WritingDone();

		bBatchProcessingEnabled = false;
		if (NextStateId == PCGEx::State_Done) { Done(); }
		if (bIsNextStateAsync) { SetAsyncState(NextStateId); }
		else { SetState(NextStateId); }
	}

	return false;
}

void FPCGExPointsProcessorContext::BatchProcessing_InitialProcessingDone()
{
}

void FPCGExPointsProcessorContext::BatchProcessing_WorkComplete()
{
}

void FPCGExPointsProcessorContext::BatchProcessing_WritingDone()
{
}

bool FPCGExPointsProcessorElement::PrepareDataInternal(FPCGContext* InContext) const
{
	FPCGExPointsProcessorContext* Context = static_cast<FPCGExPointsProcessorContext*>(InContext);
	check(Context);

	if (!Context->GetInputSettings<UPCGSettings>()->bEnabled)
	{
		Context->bExecutionCancelled = true;
		return true;
	}

	PCGEX_EXECUTION_CHECK_C(Context)

	if (Context->IsState(PCGEx::State_Preparation))
	{
		if (!Boot(Context))
		{
			Context->OutputData.TaggedData.Empty(); // Ensure culling of subsequent nodes if boot fails
			return Context->CancelExecution(TEXT(""));
		}

		// Have operations register their dependencies
		for (UPCGExInstancedFactory* Op : Context->InternalOperations) { Op->RegisterAssetDependencies(Context); }

		Context->RegisterAssetDependencies();
		if (Context->HasAssetRequirements())
		{
			Context->LoadAssets();
			return false;
		}
		// Call it so if there's initialization in there it'll run as a mandatory step
		PostLoadAssetsDependencies(Context);
	}

	PCGEX_ON_ASYNC_STATE_READY(PCGEx::State_LoadingAssetDependencies)
	{
		PostLoadAssetsDependencies(Context);
		PCGEX_EXECUTION_CHECK_C(Context)
	}

	PCGEX_ON_ASYNC_STATE_READY(PCGEx::State_AsyncPreparation)
	{
		PCGEX_EXECUTION_CHECK_C(Context)
	}

	if (!PostBoot(Context))
	{
		return Context->CancelExecution(TEXT("There was a problem during post-data preparation."));
	}

	Context->ReadyForExecution();
	return IPCGElement::PrepareDataInternal(Context);
}

FPCGContext* FPCGExPointsProcessorElement::Initialize(const FPCGInitializeElementParams& InParams)
{
	FPCGExPointsProcessorContext* Context = static_cast<FPCGExPointsProcessorContext*>(IPCGElement::Initialize(InParams));
	OnContextInitialized(Context);
	return Context;
}

bool FPCGExPointsProcessorElement::IsCacheable(const UPCGSettings* InSettings) const
{
	const UPCGExPointsProcessorSettings* Settings = static_cast<const UPCGExPointsProcessorSettings*>(InSettings);
	return Settings->ShouldCache();
}

void FPCGExPointsProcessorElement::DisabledPassThroughData(FPCGContext* Context) const
{
	const UPCGExPointsProcessorSettings* Settings = Context->GetInputSettings<UPCGExPointsProcessorSettings>();
	check(Settings);

	//Forward main points
	TArray<FPCGTaggedData> MainSources = Context->InputData.GetInputsByPin(Settings->GetMainInputPin());
	for (const FPCGTaggedData& TaggedData : MainSources)
	{
		FPCGTaggedData& TaggedDataCopy = Context->OutputData.TaggedData.Emplace_GetRef();
		TaggedDataCopy.Data = TaggedData.Data;
		TaggedDataCopy.Tags.Append(TaggedData.Tags);
		TaggedDataCopy.Pin = Settings->GetMainOutputPin();
	}
}

FPCGContext* FPCGExPointsProcessorElement::CreateContext() { return new FPCGExPointsProcessorContext(); }

void FPCGExPointsProcessorElement::OnContextInitialized(FPCGExPointsProcessorContext* InContext) const
{
	InContext->SetState(PCGEx::State_Preparation);

	const UPCGExPointsProcessorSettings* Settings = InContext->GetInputSettings<UPCGExPointsProcessorSettings>();
	check(Settings);

	InContext->bFlattenOutput = Settings->bFlattenOutput;
	InContext->bScopedAttributeGet = Settings->WantsScopedAttributeGet();
}

bool FPCGExPointsProcessorElement::Boot(FPCGExContext* InContext) const
{
	FPCGExPointsProcessorContext* Context = static_cast<FPCGExPointsProcessorContext*>(InContext);
	PCGEX_SETTINGS(PointsProcessor)

	Context->bQuietCancellationError = Settings->bQuietCancellationError;
	Context->bCleanupConsumableAttributes = Settings->bCleanupConsumableAttributes;

	if (Settings->bCleanupConsumableAttributes)
	{
		for (const TArray<FString> Names = PCGExHelpers::GetStringArrayFromCommaSeparatedList(Settings->CommaSeparatedProtectedAttributesName);
		     const FString& Name : Names)
		{
			Context->AddProtectedAttributeName(FName(Name));
		}

		for (const FName& Name : Settings->ProtectedAttributes)
		{
			Context->AddProtectedAttributeName(FName(Name));
		}
	}

	if (Context->InputData.GetAllInputs().IsEmpty() && !Settings->IsInputless()) { return false; } //Get rid of errors and warning when there is no input

	Context->MainPoints = MakeShared<PCGExData::FPointIOCollection>(Context, Settings->GetIsMainTransactional());
	Context->MainPoints->OutputPin = Settings->GetMainOutputPin();

	if (Settings->GetMainAcceptMultipleData())
	{
		TArray<FPCGTaggedData> Sources = Context->InputData.GetInputsByPin(Settings->GetMainInputPin());
		Context->MainPoints->Initialize(Sources, Settings->GetMainOutputInitMode());
	}
	else
	{
		const TSharedPtr<PCGExData::FPointIO> SingleInput = PCGExData::TryGetSingleInput(Context, Settings->GetMainInputPin(), false, false);
		if (SingleInput) { Context->MainPoints->Add_Unsafe(SingleInput); }
	}

	Context->InitialMainPointsNum = Context->MainPoints->Num();

	if (Context->MainPoints->IsEmpty() && !Settings->IsInputless())
	{
		if (!Settings->bQuietMissingInputError)
		{
			PCGE_LOG(Error, GraphAndLog, FText::Format(FText::FromString(TEXT("Missing {0} inputs (either no data or no points)")), FText::FromName(Settings->GetMainInputPin())));
		}
		return false;
	}

	if (Settings->SupportsPointFilters())
	{
		GetInputFactories(Context, Settings->GetPointFilterPin(), Context->FilterFactories, Settings->GetPointFilterTypes(), false);
		if (Settings->RequiresPointFilters() && Context->FilterFactories.IsEmpty())
		{
			PCGE_LOG(Error, GraphAndLog, FText::Format(FTEXT("Missing {0}."), FText::FromName(Settings->GetPointFilterPin())));
			return false;
		}
	}

	return true;
}

void FPCGExPointsProcessorElement::PostLoadAssetsDependencies(FPCGExContext* InContext) const
{
}

bool FPCGExPointsProcessorElement::PostBoot(FPCGExContext* InContext) const
{
	return true;
}

void FPCGExPointsProcessorElement::AbortInternal(FPCGContext* Context) const
{
	IPCGElement::AbortInternal(Context);

	if (!Context) { return; }

	FPCGExContext* PCGExContext = static_cast<FPCGExContext*>(Context);
	PCGExContext->CancelExecution(TEXT(""));
}

bool FPCGExPointsProcessorElement::CanExecuteOnlyOnMainThread(FPCGContext* Context) const
{
	return false;
}

bool FPCGExPointsProcessorElement::SupportsBasePointDataInputs(FPCGContext* InContext) const
{
	return true;
}

#undef LOCTEXT_NAMESPACE
