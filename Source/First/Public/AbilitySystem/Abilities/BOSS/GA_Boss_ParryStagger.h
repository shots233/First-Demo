// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/BOSS/FirstBossGameplayAbility.h"
#include "GA_Boss_ParryStagger.generated.h"

/**
 * 
 */
UCLASS()
class FIRST_API UGA_Boss_ParryStagger : public UFirstBossGameplayAbility
{
	GENERATED_BODY()
public:
	UGA_Boss_ParryStagger();

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

private:
	UFUNCTION()
	void HandleStaggerFinished();

	void FinishParryStagger(bool bWasCancelled);
};
