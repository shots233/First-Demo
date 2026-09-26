#include "Performance/FirstCombatWarmupSubsystem.h"

#include "Async/TaskGraphInterfaces.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "NiagaraEmitter.h"
#include "NiagaraScript.h"
#include "NiagaraShader.h"
#include "NiagaraSystem.h"
#include "PipelineStateCache.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "PSOPrecacheMaterial.h"
#include "ShaderPipelineCache.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogCombatWarmup, Log, All);

namespace
{
	// Niagara queues GPU simulation separately from its material PSOs. Re-requesting the
	// same shader obtains its existing cache request; it does not run particles or duplicate compilation.
	bool BoostCombatComputePSOs(UNiagaraSystem* System)
	{
		if (!PipelineStateCache::IsPSOPrecachingEnabled())
		{
			return true;
		}
		bool bAllScriptsAvailable = true;
		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			const FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
			if (!Handle.GetIsEnabled() || !Data || Data->SimTarget != ENiagaraSimTarget::GPUComputeSim)
			{
				continue;
			}
			const UNiagaraScript* Script = Data->GetGPUComputeScript();
			const FNiagaraShaderScript* ShaderScript = Script ? Script->GetRenderThreadScript() : nullptr;
			if (!ShaderScript || !Script->DidScriptCompilationSucceed(true))
			{
				bAllScriptsAvailable = false;
				continue;
			}
			for (int32 Permutation = 0; Permutation < ShaderScript->GetNumPermutations(); ++Permutation)
			{
				const auto Shader = ShaderScript->GetShaderGameThread(Permutation);
				if (!Shader.IsValid())
				{
					bAllScriptsAvailable = false;
					continue;
				}
				const FPSOPrecacheRequestResult Request = PipelineStateCache::PrecacheComputePipelineState(
					Shader.GetComputeShader(), TEXT("FirstCombatWarmup"), true);
				if (Request.IsValid())
				{
					PipelineStateCache::BoostPrecachePriority(EPSOPrecachePriority::High, Request.RequestID);
				}
			}
		}
		return bAllScriptsAvailable;
	}
}

bool UFirstCombatWarmupSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningCommandlet() && FApp::CanEverRender() && !GIsAutomationTesting
		&& Super::ShouldCreateSubsystem(Outer);
}

bool UFirstCombatWarmupSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UFirstCombatWarmupSubsystem::PrepareSystem(UNiagaraSystem* System)
{
	if (!IsValid(System) || RequestedSystems.Contains(System))
	{
		return;
	}
	RequestedSystems.Add(System);
	System->PrecachePSOs();
	// These are known combat effects, so place their material work ahead of speculative PSOs.
	BoostPSOPriority(EPSOPrecachePriority::High, System->GetMaterialPSOPrecacheRequestIDs());
	PendingSystems.AddUnique(System);
	BackgroundDeadline = FPlatformTime::Seconds() + FMath::Max(1.f, MaxPreparationSeconds);
}

void UFirstCombatWarmupSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	// A local loading pause must never pause an authoritative multiplayer world.
	if (!bEnableStartupPreparation || InWorld.GetNetMode() != NM_Standalone)
	{
		return;
	}
	APlayerController* Controller = InWorld.GetFirstPlayerController();
	UGameViewportClient* Viewport = InWorld.GetGameViewport();
	if (!Controller || !Controller->IsLocalController() || Controller->IsPaused() || !Viewport)
	{
		return;
	}
	bOwnsPause = Controller->SetPause(true,
		FCanUnpause::CreateUObject(this, &ThisClass::CanEndPreparationPause));
	if (!bOwnsPause)
	{
		return;
	}
	PausingController = Controller;
	bPreparingStartup = true;
	PreparationStartedAt = FPlatformTime::Seconds();
	PreparationStartFrame = GFrameCounter;
	QuietSince = 0.0;
	// The scene still renders behind this overlay, so initial view-dependent pipelines can be built.
	PreparationWidget = SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.015f, 0.018f, 0.025f, 1.f))
		.HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(NSLOCTEXT("First", "PreparingCombatResources", "正在准备战斗资源…"))
			.ColorAndOpacity(FLinearColor::White)
		];
	Viewport->AddViewportWidgetContent(PreparationWidget.ToSharedRef(), 10000);
	UE_LOG(LogCombatWarmup, Display, TEXT("Startup preparation started; pending PSOs: %u."),
		FShaderPipelineCache::NumPrecompilesRemaining());
}

bool UFirstCombatWarmupSubsystem::IsTickable() const
{
	return !IsTemplate() && (bPreparingStartup || !PendingSystems.IsEmpty());
}

TStatId UFirstCombatWarmupSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UFirstCombatWarmupSubsystem, STATGROUP_Tickables);
}

void UFirstCombatWarmupSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	TRACE_CPUPROFILER_EVENT_SCOPE(FirstCombatWarmup);
	const double Now = FPlatformTime::Seconds();
	for (int32 Index = PendingSystems.Num() - 1; Index >= 0; --Index)
	{
		UNiagaraSystem* System = PendingSystems[Index];
#if WITH_EDITOR
		// Only flush the engine's pending on-demand requests. RequestCompile(false) would
		// also rebuild instances using this asset in other PIE worlds, even when already valid.
		System->PollForCompilationComplete();
#endif
		if (!ComputePriorityBoostedSystems.Contains(System) && BoostCombatComputePSOs(System))
		{
			ComputePriorityBoostedSystems.Add(System);
		}
		// Material collection can still be in flight before it submits RHI PSO jobs.
		const FGraphEventRef& MaterialPrecache = System->GetPrecachePSOsEvent();
		if (System->IsReadyToRun() && (!MaterialPrecache.IsValid() || MaterialPrecache->IsComplete()))
		{
			PendingSystems.RemoveAtSwap(Index);
		}
	}
	// Do not call NiagaraComponent::InitializeSystem: it also starts simulation internally.
	// Only prepare asset compilation and PSOs here; animation notifies still own activation.

	if (bPreparingStartup)
	{
		const bool bWorkRemaining = !PendingSystems.IsEmpty()
			|| FShaderPipelineCache::NumPrecompilesRemaining() != 0;
		// Allow initial rendered views to submit work, then require a short quiet interval.
		if (bWorkRemaining || GFrameCounter - PreparationStartFrame < 8)
		{
			QuietSince = 0.0;
		}
		else if (QuietSince == 0.0)
		{
			QuietSince = Now;
		}
		const bool bTimedOut = Now - PreparationStartedAt >= FMath::Max(1.f, MaxPreparationSeconds);
		if (bTimedOut || (QuietSince != 0.0 && Now - QuietSince >= 0.35))
		{
			FinishStartupPreparation(bTimedOut);
		}
	}
	if (!bPreparingStartup && BackgroundDeadline != 0.0 && Now >= BackgroundDeadline
		&& !PendingSystems.IsEmpty())
	{
		UE_LOG(LogCombatWarmup, Warning, TEXT("Effect preparation timed out (%d systems); normal activation remains available."),
			PendingSystems.Num());
		PendingSystems.Reset();
	}
}

void UFirstCombatWarmupSubsystem::RemovePreparationWidget()
{
	if (PreparationWidget.IsValid())
	{
		if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
		{
			Viewport->RemoveViewportWidgetContent(PreparationWidget.ToSharedRef());
		}
		PreparationWidget.Reset();
	}
}

void UFirstCombatWarmupSubsystem::FinishStartupPreparation(bool bTimedOut)
{
	if (bTimedOut)
	{
		for (UNiagaraSystem* System : PendingSystems)
		{
			const FGraphEventRef& MaterialPrecache = System->GetPrecachePSOsEvent();
			UE_LOG(LogCombatWarmup, Warning, TEXT("Still preparing %s: ready=%d, materialPSOsReady=%d."),
				*System->GetPathName(), System->IsReadyToRun(), !MaterialPrecache.IsValid() || MaterialPrecache->IsComplete());
		}
	}
	RemovePreparationWidget();
	bPreparingStartup = false;
	if (bOwnsPause && PausingController.IsValid())
	{
		PausingController->SetPause(false);
	}
	bOwnsPause = false;
	CSV_EVENT_GLOBAL(TEXT("FirstCombatWarmup/%s"), bTimedOut ? TEXT("Timeout") : TEXT("Ready"));
	UE_LOG(LogCombatWarmup, Display, TEXT("Startup preparation %s after %.2f seconds; pending PSOs: %u, systems: %d."),
		bTimedOut ? TEXT("reached timeout") : TEXT("finished"), FPlatformTime::Seconds() - PreparationStartedAt,
		FShaderPipelineCache::NumPrecompilesRemaining(), PendingSystems.Num());
}

void UFirstCombatWarmupSubsystem::Deinitialize()
{
	// During world teardown do not resume gameplay in a world that is being destroyed.
	RemovePreparationWidget();
	bPreparingStartup = false;
	bOwnsPause = false;
	PendingSystems.Reset();
	RequestedSystems.Reset();
	ComputePriorityBoostedSystems.Reset();
	Super::Deinitialize();
}
