// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/FirstDKGameplayAbility.h"
#include "FirstGA_DKDodge.generated.h"

/**
 * 
 */
UCLASS()
class FIRST_API UFirstGA_DKDodge : public UFirstDKGameplayAbility
{
	GENERATED_BODY()
public:
	// 作用：设置闪避 Tag、互斥规则以及原生体力 Cost GE。
	UFirstGA_DKDodge();

protected:
	// 普通状态可闪避；攻击状态必须拥有 .By.Dodge 权限。
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	
	// 作用：提交体力消耗，按移动输入决定方向，并播放/等待闪避表现。
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

private:
	
	// 仅在 Dodge 已经成功 Commit 后取消当前 Attack 类 Ability。
	void CancelActiveAttackAbilities();
	
	// 作用：Montage 或无动画延时正常结束后清理闪避状态。
	UFUNCTION()
	void HandleDodgeCompleted();

	// 作用：Montage 被取消或打断时也结束 Ability，ActivationOwnedTags 会自动移除。
	UFUNCTION()
	void HandleDodgeCancelled();

	// 作用：集中结束闪避 Ability，防止多个异步回调重复结束。
	void FinishDodge(bool bWasCancelled);
};
