// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/BOSS/FirstBossGameplayAbility.h"
#include "Types/FirstCombatTypes.h"
#include "GA_Boss_ThreeCombo.generated.h"

/**
 * BOSS 三连击（一个技能）。
 * 三段蒙太奇按顺序自动播放，不需要玩家输入；每段有效帧命中都结算伤害。
 */
UCLASS()
class FIRST_API UGA_Boss_ThreeCombo : public UFirstBossGameplayAbility
{
	GENERATED_BODY()
public:
	UGA_Boss_ThreeCombo();

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

	// UE 5.6 基类已移除 CooldownTags，需要自己声明并覆写 GetCooldownTags。
	FGameplayTagContainer CooldownTags;
	virtual const FGameplayTagContainer* GetCooldownTags() const override;

private:
	UPROPERTY(EditDefaultsOnly, Category="Combat|Motion Warping")
	FFirstAttackWarpingData AttackWarpingData;
};
