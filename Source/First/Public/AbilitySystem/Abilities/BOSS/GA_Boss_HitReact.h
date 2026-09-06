// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/Boss/FirstBossGameplayAbility.h"
#include "GA_Boss_HitReact.generated.h"

/**
 * BOSS 受击反应能力。
 * 由 ABossCharacter 掉血时发送 Boss.Event.HitReact 触发（AbilityTriggers: GameplayEvent）。
 * 激活时：取消当前招式（按 Boss.Ability.Attack 类别）→ 播放受击蒙太奇 → 结束。
 */
UCLASS()
class FIRST_API UGA_Boss_HitReact : public UFirstBossGameplayAbility
{
	GENERATED_BODY()
public:
	UGA_Boss_HitReact();

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

	void FinishHitReact(bool bWasCancelled);
	
	// UE 5.6：基类已移除 CooldownTags，自己声明并覆写 GetCooldownTags。
	FGameplayTagContainer CooldownTags;
	virtual const FGameplayTagContainer* GetCooldownTags() const override;
};
