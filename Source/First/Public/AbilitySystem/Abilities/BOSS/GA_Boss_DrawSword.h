// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/BOSS/FirstBossGameplayAbility.h"
#include "GA_Boss_DrawSword.generated.h"

/**
 * 拔剑：Boss.Event.CombatStart 触发（首次索敌成功）。
 * 播放拔剑蒙太奇；挂手后添加 Boss.Status.WeaponDrawn（攻击能力的前置条件）。
 */
UCLASS()
class FIRST_API UGA_Boss_DrawSword : public UFirstBossGameplayAbility
{
	GENERATED_BODY()
public:
	UGA_Boss_DrawSword();

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	// 拔剑蒙太奇里的 AttachToHand Notify 触发时挂手。
	UFUNCTION()
	void HandleWeaponAttachEvent(FGameplayEventData Payload);

	UFUNCTION()
	void HandleMontageCompleted();

	UFUNCTION()
	void HandleMontageInterrupted();

	void AttachWeaponToHand();
	void FinishDraw(bool bWasCancelled);

	bool bWeaponAttached = false;
};
