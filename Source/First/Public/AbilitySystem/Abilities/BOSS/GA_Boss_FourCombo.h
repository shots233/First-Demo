// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/BOSS/FirstBossGameplayAbility.h"
#include "Types/FirstCombatTypes.h"
#include "GA_Boss_FourCombo.generated.h"

/**
 * BOSS 四连斩（一个技能，霸体招式）。
 * 结构克隆自 UGA_Boss_ThreeCombo：单蒙太奇四段自动播放，共享一个 Motion Warping 会话。
 * 霸体机制（见《BOSS四连霸体斩击实现指导.md》）：
 * - 挥砍段由动画里的 ANS_SuperArmorWindow 挂 Boss.Status.Uninterruptible；
 * - 窗口内普通受击不触发打断（GA_Boss_HitReact 被该标签阻挡）；
 * - 弹反照常削韧但不僵直不打断（GA_Boss_ParryStagger 霸体分支）；
 * - 破韧与死亡照旧打断（Executable / Death 按 Boss.Ability.Attack 父标签取消）；
 * - 第四段后摇由动画开放 ANS_HitReactWindow 作为玩家惩罚窗口。
 */
UCLASS()
class FIRST_API UGA_Boss_FourCombo : public UFirstBossGameplayAbility
{
	GENERATED_BODY()
public:
	UGA_Boss_FourCombo();

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
