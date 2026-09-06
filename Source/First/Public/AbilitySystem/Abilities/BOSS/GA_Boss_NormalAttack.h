// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/BOSS/FirstBossGameplayAbility.h"
#include "Types/FirstCombatTypes.h"
#include "GA_Boss_NormalAttack.generated.h"

/**
 * BOSS 普通攻击（单段）。
 */
UCLASS()
class FIRST_API UGA_Boss_NormalAttack : public UFirstBossGameplayAbility
{
	GENERATED_BODY()
public:
	UGA_Boss_NormalAttack();

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
	
	UFUNCTION()
	void HandleMeleeHit(FGameplayEventData Payload);

	UFUNCTION()
	void HandleMontageCompleted();

	UFUNCTION()
	void HandleMontageInterrupted();

	FGameplayTagContainer CooldownTags;
	virtual const FGameplayTagContainer* GetCooldownTags() const override;

private:
	UPROPERTY(EditDefaultsOnly, Category="Combat|Motion Warping")
	FFirstAttackWarpingData AttackWarpingData;
};
