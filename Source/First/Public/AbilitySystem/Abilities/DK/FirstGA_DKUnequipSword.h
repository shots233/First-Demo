// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/FirstDKGameplayAbility.h"
#include "FirstGA_DKUnequipSword.generated.h"

/**
 * 
 */
UCLASS()
class FIRST_API UFirstGA_DKUnequipSword : public UFirstDKGameplayAbility
{
	GENERATED_BODY()
public:
	// 作用：设置卸下技能的身份、实例策略与互斥状态。
	UFirstGA_DKUnequipSword();

protected:
	// 作用：移除剑授予的 AbilitySpec，把剑挂回背部并清除当前装备 Tag。
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

private:
	// 作用：收到 Unequip Montage 的 AttachToBack Event 后调用。
	UFUNCTION()
	void HandleAttachToBack(FGameplayEventData Payload);

	// 作用：真正执行手部剑到背部剑的状态转换。
	bool AttachSwordToBack();

	// 作用：防止重复移除武器授予的攻击 AbilitySpec。
	bool bSwordAttachedToBack = false;
	
	// 作用：卸下 Montage 正常完成时结束 Ability。
	UFUNCTION()
	void HandleMontageCompleted();

	// 作用：卸下 Montage 被取消/打断时结束 Ability，防止 Active 状态残留。
	UFUNCTION()
	void HandleMontageCancelled();

	// 作用：集中处理卸下 Ability 的结束参数。
	void FinishUnequip(bool bWasCancelled);
};
