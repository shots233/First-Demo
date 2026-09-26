#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FirstCombatWarmupSubsystem.generated.h"

class UNiagaraSystem;
class SWidget;
class APlayerController;

/** Prepares registered combat effects without playing them. Standalone startup waits for PSOs. */
UCLASS(Config=Game)
class FIRST_API UFirstCombatWarmupSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual TStatId GetStatId() const override;

	void PrepareSystem(UNiagaraSystem* System);

protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
	// Disabling the startup screen still allows effects to prepare in the background.
	UPROPERTY(Config)
	bool bEnableStartupPreparation = true;

	UPROPERTY(Config)
	float MaxPreparationSeconds = 10.f;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UNiagaraSystem>> PendingSystems;

	TSet<TWeakObjectPtr<UNiagaraSystem>> RequestedSystems;
	TSet<TWeakObjectPtr<UNiagaraSystem>> ComputePriorityBoostedSystems;
	TWeakObjectPtr<APlayerController> PausingController;
	TSharedPtr<SWidget> PreparationWidget;
	double PreparationStartedAt = 0.0;
	double QuietSince = 0.0;
	double BackgroundDeadline = 0.0;
	uint64 PreparationStartFrame = 0;
	bool bPreparingStartup = false;
	bool bOwnsPause = false;

	void FinishStartupPreparation(bool bTimedOut);
	bool CanEndPreparationPause() { return !bPreparingStartup; }
	void RemovePreparationWidget();
};
