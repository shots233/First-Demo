// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "TimerManager.h"
#include "AbilitySystem/Abilities/BOSS/FirstBossGameplayAbility.h"
#include "GA_Boss_Executable.generated.h"

class ADKCharacter;
/**
 * 
 */
UCLASS()
class FIRST_API UGA_Boss_Executable : public UFirstBossGameplayAbility
{
	GENERATED_BODY()
public:
	UGA_Boss_Executable();

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

private:
	UFUNCTION()
	void HandleExecutionStarted(FGameplayEventData Payload);

	UFUNCTION()
	void HandleExecutionAborted(FGameplayEventData Payload);

	UFUNCTION()
	void HandleExecutionFinished(FGameplayEventData Payload);

	UFUNCTION()
	void HandleExecutionApplyDamage(FGameplayEventData Payload);

	UFUNCTION()
	void HandleExecutableExpired(FGameplayEventData Payload);

	UFUNCTION()
	void HandleExecutionGetUpCompleted();

	UFUNCTION()
	void HandleExecutionGetUpInterrupted();

	void StopBossMovement();
	void PlayExecutableLoop();
	void RemoveBeingExecutedTag();
	void NotifyExecutingPlayerAbort();
	void ReturnToExecutableAfterAbort();
	void HandleExecutionSafetyTimeout();
	void StartExecutionGetUp();
	void HandleExecutionGetUpSafetyTimeout();
	void FinishExecutable(bool bWasCancelled);

	UPROPERTY()
	TWeakObjectPtr<ADKCharacter> ExecutingPlayer;

	bool bExecutionStarted = false;
	bool bExecutionDamageApplied = false;
	bool bBossKilledByExecution = false;
	bool bPlayerTerminalEventHandled = false;
	bool bOwnsBeingExecutedTag = false;
	// 防止完成、取消、Montage 中断和安全计时器同时触发多次 EndAbility()。
	bool bExecutionGetUpStarted = false;
	FTimerHandle ExecutionSafetyTimerHandle;
};
