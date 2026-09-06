// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/BOSS/FirstBossGameplayAbility.h"
#include "Types/FirstCombatTypes.h"
#include "GA_Boss_RetreatChargedSlash.generated.h"

class ACharacter;

/**
 * BOSS 后撤蓄力斩。
 * 后撤阶段不建立 AttackTarget；ChargeBegin 后预热并跟踪目标，
 * Montage 发出 Commit 事件后刷新固定快照并锁定攻击方向。
 */
UCLASS()
class FIRST_API UGA_Boss_RetreatChargedSlash : public UFirstBossGameplayAbility
{
	GENERATED_BODY()

public:
	UGA_Boss_RetreatChargedSlash();

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

	virtual const FGameplayTagContainer* GetCooldownTags() const override;

	UFUNCTION()
	void HandleChargeBegin(FGameplayEventData Payload);

	UFUNCTION()
	void HandleAttackCommit(FGameplayEventData Payload);

	UFUNCTION()
	void HandleMeleeHit(FGameplayEventData Payload);

	UFUNCTION()
	void HandleMontageCompleted();

	UFUNCTION()
	void HandleMontageInterrupted();

private:
	ACharacter* ResolveCurrentTarget(const ABossCharacter* Boss) const;
	bool IsTargetUsable(const ACharacter* Target) const;
	void SetAttackDirectionLocked(bool bShouldLock);
	void DisableWeaponAttackEffects();

	// —— 蓄力斩 → 接续招式链（攻击侧扩展；行为树不参与，只通过 bIsBusy 感知）——

	// 掷概率并安排延迟接招。只在蒙太奇正常播完（HandleMontageCompleted）时调用；
	// 破韧/死亡等打断路径（HandleMontageInterrupted）绝不接招。
	void TryScheduleChainAttack();

	// 接续招式的身份标签：不硬编码具体招式类，按 Tag 激活（TryActivateAbilitiesByTag）。
	// 想接其它招式时在 GA 类默认值里换标签即可，零代码改动。默认五连斩。
	UPROPERTY(EditDefaultsOnly, Category="Combat|Chain")
	FGameplayTag ChainAttackTag;

	// 接招概率 [0,1]：0.5 = 50%；设 0 关闭接招。方便测试与实际手感调整。
	UPROPERTY(EditDefaultsOnly, Category="Combat|Chain",
		meta=(ClampMin="0.0", ClampMax="1.0"))
	float ChainAttackChance = 0.5f;

	// 蓄力斩播完到接续招式启动的延迟（秒）。延迟期间 BOSS 保持 Boss.Status.Attacking
	//（BT 的 bIsBusy 判定来源），行为树不会在间隙里选新招或恢复移动。
	UPROPERTY(EditDefaultsOnly, Category="Combat|Chain",
		meta=(ClampMin="0.0", Units="s"))
	float ChainAttackDelay = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category="Combat|Motion Warping")
	FFirstAttackWarpingData AttackWarpingData;

	UPROPERTY(EditDefaultsOnly, Category="Combat|Retreat",
		meta=(ClampMin="0.0", Units="cm"))
	float RetreatCheckDistance = 220.f;

	UPROPERTY(EditDefaultsOnly, Category="Combat|Retreat",
		meta=(ClampMin="0.0", Units="cm"))
	float RetreatNavTolerance = 50.f;

	UPROPERTY(EditDefaultsOnly, Category="Combat|Retreat",
		meta=(ClampMin="0.0", Units="cm"))
	float RetreatMaxLandingHeightDelta = 60.f;

	FGameplayTagContainer CooldownTags;
	TWeakObjectPtr<ACharacter> CommittedTarget;

	bool bCommitReceived = false;
	bool bHitResolved = false;
	bool bDirectionLockApplied = false;
};
