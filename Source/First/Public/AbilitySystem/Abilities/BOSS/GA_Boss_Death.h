// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/FirstGameplayAbility.h"
#include "GA_Boss_Death.generated.h"

/**
 * 
 */
UCLASS()
class FIRST_API UGA_Boss_Death : public UFirstGameplayAbility
{
	GENERATED_BODY()
public:
	UGA_Boss_Death();

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	UFUNCTION()
	void HandleMontageCompleted();

	UFUNCTION()
	void HandleMontageInterrupted();

	void FinishDeath(bool bWasCancelled);
};
