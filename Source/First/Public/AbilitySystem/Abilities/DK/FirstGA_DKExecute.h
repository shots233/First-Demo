// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AbilitySystem/Abilities/FirstDKGameplayAbility.h"
#include "FirstGA_DKExecute.generated.h"

class ABossCharacter;
/**
 * 
 */
UCLASS()
class FIRST_API UFirstGA_DKExecute : public UFirstDKGameplayAbility
{
	GENERATED_BODY()
public:
	UFirstGA_DKExecute();

protected:
	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

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
	ABossCharacter* FindExecutableBoss(const FGameplayAbilityActorInfo* ActorInfo) const;

	UFUNCTION()
	void HandleExecutionCompleted();

	UFUNCTION()
	void HandleExecutionInterrupted();

	UFUNCTION()
	void HandleBossExecutionAborted(FGameplayEventData Payload);

	void SendBossEvent(const FGameplayTag& EventTag);
	void FinishExecution(bool bWasCancelled);

	UPROPERTY(EditDefaultsOnly, Category="Execution", meta=(ClampMin="0.0"))
	float ExecutionRange = 200.f;

	UPROPERTY()
	TWeakObjectPtr<ABossCharacter> TargetBoss;

	bool bTerminalEventSent = false;
	bool bSavedMovementMode = false;
	TEnumAsByte<EMovementMode> SavedMovementMode = MOVE_Walking;
	uint8 SavedCustomMovementMode = 0;

	bool bSavedPlayerPawnResponse = false;
	bool bSavedBossPawnResponse = false;
	TEnumAsByte<ECollisionResponse> SavedPlayerPawnResponse = ECR_Block;
	TEnumAsByte<ECollisionResponse> SavedBossPawnResponse = ECR_Block;
};
